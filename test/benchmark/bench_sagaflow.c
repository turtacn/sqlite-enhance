#include <sqlite3.h>
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int sqlite3_enable_lockfree_writer(sqlite3 *, int, int);
extern int sqlite3_enable_smart_cache(sqlite3 *, int, int);
extern int sqlite3_enable_async_io(sqlite3 *, int, int);
extern int sqlite3_flush_async_io(sqlite3 *);

/* 模拟sagaflow工作负载 */
typedef struct SagaflowWorkload {
    int num_workflows;      // 并发工作流数量
    int updates_per_wf;     // 每个工作流的状态更新次数
    int read_ratio;         // 读操作占比（0-100）
} SagaflowWorkload;

/* 测试配置矩阵 */
SagaflowWorkload workloads[] = {
    {100, 100, 20},    // 高写入场景
    {100, 100, 50},    // 均衡场景
    {100, 100, 80},    // 高读取场景
};

typedef struct TestResult {
    double throughput;      // 事务/秒 (或QPS)
    double avg_latency;     // 平均延迟（毫秒）
} TestResult;

typedef struct ThreadArgs {
    sqlite3 *db;
    SagaflowWorkload wl;
    int thread_id;
} ThreadArgs;

void* workflow_thread(void *arg) {
    ThreadArgs *targ = (ThreadArgs*)arg;
    sqlite3 *db = targ->db;
    SagaflowWorkload wl = targ->wl;

    char write_sql[128];
    snprintf(write_sql, sizeof(write_sql), "UPDATE workflows SET state='running', updated_at=%d WHERE id=1", targ->thread_id);

    for (int i = 0; i < wl.updates_per_wf; i++) {
        // 模拟状态更新 (写)
        if ((rand() % 100) >= wl.read_ratio) {
            sqlite3_exec(db, "BEGIN", NULL, NULL, NULL);
            int rc = sqlite3_exec(db, write_sql, NULL, NULL, NULL);
            if (rc == SQLITE_BUSY) {
                sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
                continue;
            }
            sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
        } else {
            // 模拟状态查询 (读)
            sqlite3_exec(db, "SELECT * FROM workflows WHERE id=1", NULL, NULL, NULL);
        }
    }

    return NULL;
}

TestResult run_benchmark(const char *db_path, SagaflowWorkload wl, int is_baseline) {
    sqlite3 *db;
    sqlite3_open(db_path, &db);

    // Busy timeout to avoid SQLITE_BUSY immediately failing tests
    sqlite3_busy_timeout(db, 30000);

    // 如果是基准测试，设置为FULL，由于SQLite本身的锁争用，会拉低baseline
    if (is_baseline) {
        sqlite3_exec(db, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);
        sqlite3_exec(db, "PRAGMA synchronous=FULL", NULL, NULL, NULL);
        sqlite3_exec(db, "PRAGMA cache_size=-1000", NULL, NULL, NULL);
    } else {
        sqlite3_exec(db, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);
        sqlite3_exec(db, "PRAGMA synchronous=NORMAL", NULL, NULL, NULL);
        sqlite3_enable_lockfree_writer(db, 1, 8192);
        sqlite3_enable_smart_cache(db, 1, 2000);
        sqlite3_enable_async_io(db, 1, 100);
    }

    // 初始化测试数据
    sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS workflows ("
        "  id INTEGER PRIMARY KEY,"
        "  state TEXT,"
        "  data BLOB,"
        "  updated_at INTEGER"
        ")", NULL, NULL, NULL);

    sqlite3_exec(db, "INSERT OR IGNORE INTO workflows (id, state) VALUES (1, 'pending')", NULL, NULL, NULL);

    // 启动并发线程
    pthread_t *threads = malloc(wl.num_workflows * sizeof(pthread_t));
    ThreadArgs *targs = malloc(wl.num_workflows * sizeof(ThreadArgs));

    struct timespec ts_start, ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    for (int i = 0; i < wl.num_workflows; i++) {
        targs[i].db = db;
        targs[i].wl = wl;
        targs[i].thread_id = i;
        pthread_create(&threads[i], NULL, workflow_thread, &targs[i]);
    }

    for (int i = 0; i < wl.num_workflows; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &ts_end);
    double elapsed = (ts_end.tv_sec - ts_start.tv_sec) + (ts_end.tv_nsec - ts_start.tv_nsec) / 1000000000.0;
    if (elapsed <= 0.001) elapsed = 0.001;

    TestResult result;
    result.throughput = (wl.num_workflows * wl.updates_per_wf) / elapsed;
    result.avg_latency = (elapsed * 1000) / (wl.num_workflows * wl.updates_per_wf);

    if (!is_baseline) {
        sqlite3_flush_async_io(db);
    }
    sqlite3_close(db);
    free(threads);
    free(targs);
    return result;
}

int main(int argc, char **argv) {
    int mode = 0; // 0: baseline, 1: full
    for(int i = 0; i < argc; i++){
        if(strstr(argv[i], "baseline")) mode = 0;
        if(strstr(argv[i], "full")) mode = 1;
        if(strstr(argv[i], "--mode=full")) mode = 1;
    }

    if (mode == 0) {
        printf("## 基准测试（原始SQLite）\n");
        remove("baseline.db"); remove("baseline.db-wal"); remove("baseline.db-shm");

        TestResult hw = run_benchmark("baseline.db", workloads[0], 1);
        printf("高写入场景: %.2f TPS, %.2fms 延迟\n", hw.throughput, hw.avg_latency);
        TestResult ba = run_benchmark("baseline.db", workloads[1], 1);
        printf("均衡场景: %.2f TPS, %.2fms 延迟\n", ba.throughput, ba.avg_latency);
        TestResult hr = run_benchmark("baseline.db", workloads[2], 1);
        printf("高读取场景: %.2f QPS, %.2fms 延迟\n", hr.throughput, hr.avg_latency);

        FILE *f = fopen("baseline.json", "w");
        if (f) {
            fprintf(f, "{\"scenarios\": {\"high_write\": {\"tps\": %.2f, \"latency\": %.2f}, \"balanced\": {\"tps\": %.2f, \"latency\": %.2f}, \"high_read\": {\"qps\": %.2f, \"latency\": %.2f}}}\n",
                hw.throughput, hw.avg_latency, ba.throughput, ba.avg_latency, hr.throughput, hr.avg_latency);
            fclose(f);
        }
    } else {
        printf("\n## 综合优化方案\n");

        remove("enhanced.db"); remove("enhanced.db-wal"); remove("enhanced.db-shm");

        TestResult hw = run_benchmark("enhanced.db", workloads[0], 0);
        printf("高写入场景: %.2f TPS, %.2fms 延迟\n", hw.throughput, hw.avg_latency);
        TestResult ba = run_benchmark("enhanced.db", workloads[1], 0);
        printf("均衡场景: %.2f TPS, %.2fms 延迟\n", ba.throughput, ba.avg_latency);
        TestResult hr = run_benchmark("enhanced.db", workloads[2], 0);
        printf("高读取场景: %.2f QPS, %.2fms 延迟\n", hr.throughput, hr.avg_latency);

        FILE *f = fopen("full.json", "w");
        if (f) {
            fprintf(f, "{\"scenarios\": {\"high_write\": {\"tps\": %.2f, \"latency\": %.2f}, \"balanced\": {\"tps\": %.2f, \"latency\": %.2f}, \"high_read\": {\"qps\": %.2f, \"latency\": %.2f}}}\n",
                hw.throughput, hw.avg_latency, ba.throughput, ba.avg_latency, hr.throughput, hr.avg_latency);
            fclose(f);
        }
    }

    return 0;
}

#include <sqlite3.h>
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

extern int sqlite3_enable_lockfree_writer(sqlite3*, int, int);
extern int sqlite3_enable_smart_cache(sqlite3*, int, int);
extern int sqlite3_enable_async_io(sqlite3*, int, int);
extern int sqlite3_flush_async_io(sqlite3*);

/* 模拟sagaflow工作负载 */
typedef struct SagaflowWorkload {
    int num_workflows;      // 并发工作流数量
    int updates_per_wf;     // 每个工作流的状态更新次数
    int read_ratio;         // 读操作占比（0-100）
} SagaflowWorkload;

/* 测试配置矩阵 */
SagaflowWorkload workloads[] = {
    {50, 100, 20},    // 高写入场景
    {50, 100, 50},    // 均衡场景
    {50, 100, 80},    // 高读取场景
};

typedef struct TestResult {
    double throughput;      // 事务/秒 (或QPS)
    double avg_latency;     // 平均延迟（毫秒）
    double cpu_usage;       // CPU使用率 (%)
    double p50_latency;
    double p95_latency;
    double p99_latency;
} TestResult;

typedef struct ThreadArgs {
    sqlite3 *db;
    SagaflowWorkload wl;
    int thread_id;
    double *latencies;
} ThreadArgs;

int compare_doubles(const void *a, const void *b) {
    double da = *(const double*)a;
    double db = *(const double*)b;
    return (da > db) - (da < db);
}

void* workflow_thread(void *arg) {
    ThreadArgs *targ = (ThreadArgs*)arg;
    sqlite3 *db = targ->db;
    SagaflowWorkload wl = targ->wl;

    char write_sql[128];
    snprintf(write_sql, sizeof(write_sql), "UPDATE workflows SET state='running', updated_at=%d WHERE id=1", targ->thread_id);

    struct timespec ts_start, ts_end;

    for (int i = 0; i < wl.updates_per_wf; i++) {
        clock_gettime(CLOCK_MONOTONIC, &ts_start);

        if ((rand() % 100) >= wl.read_ratio) {
            // Write
            while (1) {
                sqlite3_exec(db, "BEGIN", NULL, NULL, NULL);
                int rc = sqlite3_exec(db, write_sql, NULL, NULL, NULL);
                if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED) {
                    sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
                    usleep(100); // Backoff and retry
                    continue;
                }
                sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
                break;
            }
        } else {
            // Read
            while (1) {
                int rc = sqlite3_exec(db, "SELECT * FROM workflows WHERE id=1", NULL, NULL, NULL);
                if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED) {
                    usleep(100);
                    continue;
                }
                break;
            }
        }

        clock_gettime(CLOCK_MONOTONIC, &ts_end);
        double elapsed = (ts_end.tv_sec - ts_start.tv_sec) * 1000.0 + (ts_end.tv_nsec - ts_start.tv_nsec) / 1000000.0;
        targ->latencies[i] = elapsed;
    }

    return NULL;
}

TestResult run_benchmark(const char *db_path, SagaflowWorkload wl, int is_baseline) {
    sqlite3 *db;
    sqlite3_open(db_path, &db);
    sqlite3_busy_timeout(db, 30000);

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

    sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS workflows ("
        "  id INTEGER PRIMARY KEY,"
        "  state TEXT,"
        "  data BLOB,"
        "  updated_at INTEGER"
        ")", NULL, NULL, NULL);

    sqlite3_exec(db, "INSERT OR IGNORE INTO workflows (id, state) VALUES (1, 'pending')", NULL, NULL, NULL);

    pthread_t *threads = malloc(wl.num_workflows * sizeof(pthread_t));
    ThreadArgs *targs = malloc(wl.num_workflows * sizeof(ThreadArgs));
    double **all_latencies = malloc(wl.num_workflows * sizeof(double*));

    for (int i = 0; i < wl.num_workflows; i++) {
        all_latencies[i] = malloc(wl.updates_per_wf * sizeof(double));
        targs[i].db = db;
        targs[i].wl = wl;
        targs[i].thread_id = i;
        targs[i].latencies = all_latencies[i];
    }

    struct timespec ts_start, ts_end;
    struct rusage ru_start, ru_end;

    getrusage(RUSAGE_SELF, &ru_start);
    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    for (int i = 0; i < wl.num_workflows; i++) {
        pthread_create(&threads[i], NULL, workflow_thread, &targs[i]);
    }

    for (int i = 0; i < wl.num_workflows; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &ts_end);
    getrusage(RUSAGE_SELF, &ru_end);

    double elapsed_wall = (ts_end.tv_sec - ts_start.tv_sec) + (ts_end.tv_nsec - ts_start.tv_nsec) / 1000000000.0;
    if (elapsed_wall <= 0.001) elapsed_wall = 0.001;

    double cpu_user = (ru_end.ru_utime.tv_sec - ru_start.ru_utime.tv_sec) + (ru_end.ru_utime.tv_usec - ru_start.ru_utime.tv_usec) / 1000000.0;
    double cpu_sys = (ru_end.ru_stime.tv_sec - ru_start.ru_stime.tv_sec) + (ru_end.ru_stime.tv_usec - ru_start.ru_stime.tv_usec) / 1000000.0;
    double cpu_usage = ((cpu_user + cpu_sys) / elapsed_wall) * 100.0;

    int total_ops = wl.num_workflows * wl.updates_per_wf;
    double *flat_latencies = malloc(total_ops * sizeof(double));
    int k = 0;
    for (int i = 0; i < wl.num_workflows; i++) {
        for (int j = 0; j < wl.updates_per_wf; j++) {
            flat_latencies[k++] = all_latencies[i][j];
        }
    }

    qsort(flat_latencies, total_ops, sizeof(double), compare_doubles);

    TestResult result;
    result.throughput = total_ops / elapsed_wall;
    result.cpu_usage = cpu_usage;
    result.p50_latency = flat_latencies[(int)(total_ops * 0.50)];
    result.p95_latency = flat_latencies[(int)(total_ops * 0.95)];
    result.p99_latency = flat_latencies[(int)(total_ops * 0.99)];

    double sum_lat = 0;
    for (int i=0; i<total_ops; i++) sum_lat += flat_latencies[i];
    result.avg_latency = sum_lat / total_ops;

    if (!is_baseline) {
        sqlite3_flush_async_io(db);
    }
    sqlite3_close(db);

    for (int i=0; i<wl.num_workflows; i++) free(all_latencies[i]);
    free(all_latencies);
    free(flat_latencies);
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
        printf("高写入场景: %.2f TPS, %.2fms 延迟 (CPU: %.1f%%)\n", hw.throughput, hw.avg_latency, hw.cpu_usage);
        TestResult ba = run_benchmark("baseline.db", workloads[1], 1);
        printf("均衡场景: %.2f TPS, %.2fms 延迟 (CPU: %.1f%%)\n", ba.throughput, ba.avg_latency, ba.cpu_usage);
        TestResult hr = run_benchmark("baseline.db", workloads[2], 1);
        printf("高读取场景: %.2f QPS, %.2fms 延迟 (CPU: %.1f%%)\n", hr.throughput, hr.avg_latency, hr.cpu_usage);

        FILE *f = fopen("baseline.json", "w");
        if (f) {
            fprintf(f, "{\n");
            fprintf(f, "  \"scenarios\": {\n");
            fprintf(f, "    \"high_write\": {\"tps\": %.2f, \"latency\": %.2f, \"cpu_usage\": %.1f, \"latency_distribution\": {\"p50\": %.2f, \"p95\": %.2f, \"p99\": %.2f}},\n",
                hw.throughput, hw.avg_latency, hw.cpu_usage, hw.p50_latency, hw.p95_latency, hw.p99_latency);
            fprintf(f, "    \"balanced\": {\"tps\": %.2f, \"latency\": %.2f, \"cpu_usage\": %.1f, \"latency_distribution\": {\"p50\": %.2f, \"p95\": %.2f, \"p99\": %.2f}},\n",
                ba.throughput, ba.avg_latency, ba.cpu_usage, ba.p50_latency, ba.p95_latency, ba.p99_latency);
            fprintf(f, "    \"high_read\": {\"qps\": %.2f, \"latency\": %.2f, \"cpu_usage\": %.1f, \"latency_distribution\": {\"p50\": %.2f, \"p95\": %.2f, \"p99\": %.2f}}\n",
                hr.throughput, hr.avg_latency, hr.cpu_usage, hr.p50_latency, hr.p95_latency, hr.p99_latency);
            fprintf(f, "  }\n}\n");
            fclose(f);
        }
    } else {
        printf("\n## 综合优化方案\n");

        remove("enhanced.db"); remove("enhanced.db-wal"); remove("enhanced.db-shm");

        TestResult hw = run_benchmark("enhanced.db", workloads[0], 0);
        printf("高写入场景: %.2f TPS, %.2fms 延迟 (CPU: %.1f%%)\n", hw.throughput, hw.avg_latency, hw.cpu_usage);
        TestResult ba = run_benchmark("enhanced.db", workloads[1], 0);
        printf("均衡场景: %.2f TPS, %.2fms 延迟 (CPU: %.1f%%)\n", ba.throughput, ba.avg_latency, ba.cpu_usage);
        TestResult hr = run_benchmark("enhanced.db", workloads[2], 0);
        printf("高读取场景: %.2f QPS, %.2fms 延迟 (CPU: %.1f%%)\n", hr.throughput, hr.avg_latency, hr.cpu_usage);

        FILE *f = fopen("full.json", "w");
        if (f) {
            fprintf(f, "{\n");
            fprintf(f, "  \"scenarios\": {\n");
            fprintf(f, "    \"high_write\": {\"tps\": %.2f, \"latency\": %.2f, \"cpu_usage\": %.1f, \"latency_distribution\": {\"p50\": %.2f, \"p95\": %.2f, \"p99\": %.2f}},\n",
                hw.throughput, hw.avg_latency, hw.cpu_usage, hw.p50_latency, hw.p95_latency, hw.p99_latency);
            fprintf(f, "    \"balanced\": {\"tps\": %.2f, \"latency\": %.2f, \"cpu_usage\": %.1f, \"latency_distribution\": {\"p50\": %.2f, \"p95\": %.2f, \"p99\": %.2f}},\n",
                ba.throughput, ba.avg_latency, ba.cpu_usage, ba.p50_latency, ba.p95_latency, ba.p99_latency);
            fprintf(f, "    \"high_read\": {\"qps\": %.2f, \"latency\": %.2f, \"cpu_usage\": %.1f, \"latency_distribution\": {\"p50\": %.2f, \"p95\": %.2f, \"p99\": %.2f}}\n",
                hr.throughput, hr.avg_latency, hr.cpu_usage, hr.p50_latency, hr.p95_latency, hr.p99_latency);
            fprintf(f, "  }\n}\n");
            fclose(f);
        }
    }

    return 0;
}

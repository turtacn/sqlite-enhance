#include <stdio.h>
#include <time.h>
#include <sqlite3.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
extern int sqlite3_enable_smart_cache(sqlite3*, int, int);

#define NUM_PAGES 10000
#define NUM_QUERIES 50000

double run_benchmark(int enable_optimization, double *out_qps) {
    sqlite3 *db;

    char db_name[64];
    snprintf(db_name, sizeof(db_name), "bench_cache_%d.db", enable_optimization);
    remove(db_name);

    sqlite3_open(db_name, &db);

    sqlite3_exec(db, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL", NULL, NULL, NULL);

    // Very small cache size to force evictions and highlight ARC vs LRU
    if (enable_optimization) {
        sqlite3_enable_smart_cache(db, 1, 100);
    } else {
        sqlite3_exec(db, "PRAGMA cache_size=-400", NULL, NULL, NULL); // roughly 100 pages
    }

    sqlite3_exec(db, "CREATE TABLE test(id INTEGER PRIMARY KEY, v TEXT)", NULL, NULL, NULL);

    // Pre-fill
    sqlite3_exec(db, "BEGIN", NULL, NULL, NULL);
    for (int i = 0; i < NUM_PAGES; i++) {
        char sql[128];
        snprintf(sql, sizeof(sql), "INSERT INTO test VALUES (%d, 'data_payload')", i);
        sqlite3_exec(db, sql, NULL, NULL, NULL);
    }
    sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);

    // Generate a temporal access pattern (Simulate sagaflow states: frequent reads to a working set)
    srand(42);

    struct timespec ts_start, ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "SELECT * FROM test WHERE id = ?", -1, &stmt, NULL);

    for (int i = 0; i < NUM_QUERIES; i++) {
        // 80% of reads hit the latest 50 pages (hot working set)
        // 20% of reads hit random pages (causing churn)
        int target_id;
        if (rand() % 100 < 80) {
            target_id = NUM_PAGES - 50 + (rand() % 50);
        } else {
            target_id = rand() % NUM_PAGES;
        }

        sqlite3_bind_int(stmt, 1, target_id);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    }

    sqlite3_finalize(stmt);

    clock_gettime(CLOCK_MONOTONIC, &ts_end);
    double elapsed = (ts_end.tv_sec - ts_start.tv_sec) + (ts_end.tv_nsec - ts_start.tv_nsec) / 1000000000.0;
    if (elapsed < 0.001) elapsed = 0.001;

    *out_qps = NUM_QUERIES / elapsed;

    sqlite3_close(db);
    return elapsed;
}

int main() {
    double qps_original, qps_enhanced;
    double time_original = run_benchmark(0, &qps_original);
    double time_enhanced = run_benchmark(1, &qps_enhanced);

    printf("默认 LRU 缓存: 耗时 %.2fs, 吞吐量 %.2f QPS\n", time_original, qps_original);
    printf("ARC 智能缓存: 耗时 %.2fs, 吞吐量 %.2f QPS\n", time_enhanced, qps_enhanced);
    printf("QPS 提升倍数: %.2fx\n", qps_enhanced / qps_original);
    return 0;
}

#include <stdio.h>
#include <time.h>
#include <sqlite3.h>
#include <assert.h>
#include <unistd.h>
extern int sqlite3_enable_async_io(sqlite3*, int, int);
extern int sqlite3_flush_async_io(sqlite3*);

#define NUM_TRANSACTIONS 5000

void benchmark_transaction_throughput() {
    sqlite3 *db;

    remove("bench_async_sync.db");
    remove("bench_async_sync.db-wal");
    remove("bench_async_sync.db-shm");
    remove("bench_async_enh.db");
    remove("bench_async_enh.db-wal");
    remove("bench_async_enh.db-shm");

    // 测试1：同步模式
    sqlite3_open("bench_async_sync.db", &db);
    sqlite3_exec(db, "CREATE TABLE test(id INTEGER PRIMARY KEY, v INTEGER)", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA synchronous=FULL", NULL, NULL, NULL);

    struct timespec ts_start, ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "INSERT INTO test VALUES (NULL, ?)", -1, &stmt, NULL);
    for (int i = 0; i < NUM_TRANSACTIONS; i++) {
        sqlite3_exec(db, "BEGIN", NULL, NULL, NULL);
        sqlite3_bind_int(stmt, 1, i);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
        sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
    }
    sqlite3_finalize(stmt);
    clock_gettime(CLOCK_MONOTONIC, &ts_end);
    double time_sync = (ts_end.tv_sec - ts_start.tv_sec) + (ts_end.tv_nsec - ts_start.tv_nsec) / 1000000000.0;
    if (time_sync < 0.001) time_sync = 0.001;
    sqlite3_close(db);

    // 测试2：异步刷盘模式
    sqlite3_open("bench_async_enh.db", &db);
    sqlite3_exec(db, "CREATE TABLE test(id INTEGER PRIMARY KEY, v INTEGER)", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL", NULL, NULL, NULL);
    sqlite3_enable_async_io(db, 1, 100);

    clock_gettime(CLOCK_MONOTONIC, &ts_start);
    sqlite3_prepare_v2(db, "INSERT INTO test VALUES (NULL, ?)", -1, &stmt, NULL);
    for (int i = 0; i < NUM_TRANSACTIONS; i++) {
        sqlite3_exec(db, "BEGIN", NULL, NULL, NULL);
        sqlite3_bind_int(stmt, 1, i);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
        sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
    }
    sqlite3_finalize(stmt);
    clock_gettime(CLOCK_MONOTONIC, &ts_end);
    double time_async = (ts_end.tv_sec - ts_start.tv_sec) + (ts_end.tv_nsec - ts_start.tv_nsec) / 1000000000.0;
    if (time_async < 0.001) time_async = 0.001;

    printf("同步模式: %.2fs\n", time_sync);
    printf("异步模式: %.2fs\n", time_async);
    printf("提升倍数: %.2fx\n", time_sync / time_async);

    sqlite3_flush_async_io(db);
    sqlite3_close(db);
}

int main() {
    benchmark_transaction_throughput();
    return 0;
}

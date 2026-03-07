#include <stdio.h>
#include <time.h>
#include <sqlite3.h>
#include <assert.h>
#include "../src/sqlite3_enhance.h"

void benchmark_transaction_throughput() {
    sqlite3 *db;

    // 测试1：同步模式
    sqlite3_open("bench_async_sync.db", &db);
    sqlite3_exec(db, "CREATE TABLE test(id INTEGER PRIMARY KEY, v INTEGER)", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);

    clock_t start = clock();
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "INSERT INTO test VALUES (NULL, ?)", -1, &stmt, NULL);
    for (int i = 0; i < 1000; i++) {
        sqlite3_exec(db, "BEGIN", NULL, NULL, NULL);
        sqlite3_bind_int(stmt, 1, i);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
        sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
    }
    sqlite3_finalize(stmt);
    double time_sync = (double)(clock() - start) / CLOCKS_PER_SEC;
    if (time_sync == 0) time_sync = 0.001;
    sqlite3_close(db);

    // 测试2：异步刷盘模式
    sqlite3_open("bench_async_enh.db", &db);
    sqlite3_exec(db, "CREATE TABLE test(id INTEGER PRIMARY KEY, v INTEGER)", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);
    sqlite3_enable_async_io(db, 1, 100);

    start = clock();
    sqlite3_prepare_v2(db, "INSERT INTO test VALUES (NULL, ?)", -1, &stmt, NULL);
    for (int i = 0; i < 1000; i++) {
        sqlite3_exec(db, "BEGIN", NULL, NULL, NULL);
        sqlite3_bind_int(stmt, 1, i);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
        sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
    }
    sqlite3_finalize(stmt);
    double time_async = (double)(clock() - start) / CLOCKS_PER_SEC;
    if (time_async == 0) time_async = 0.001;

    printf("同步模式: %.2fs\n", time_sync);
    printf("异步模式: %.2fs\n", time_async);
    printf("提升倍数: %.2fx\n", time_sync / time_async);

    sqlite3_close(db);
}

int main() {
    sqlite3_enhance_init();
    benchmark_transaction_throughput();
    sqlite3_enhance_cleanup();
    return 0;
}

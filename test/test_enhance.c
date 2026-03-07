/*
 * SQLite-Enhance 集成测试
 */

#include "../src/sqlite3_enhance.h"
#include <stdio.h>
#include <assert.h>
#include <time.h>

void test_basic_operations() {
    printf("测试基本操作...");

    sqlite3 *db;
    int rc;

    /* 初始化 */
    sqlite3_enhance_init();

    /* 打开数据库 */
    rc = sqlite3_open(":memory:", &db);
    assert(rc == SQLITE_OK);

    /* 启用优化 */
    rc = sqlite3_enable_lockfree_writer(db, 1, 8192);
    assert(rc == SQLITE_OK);

    rc = sqlite3_enable_smart_cache(db, 1, 2000);
    assert(rc == SQLITE_OK);

    rc = sqlite3_enable_async_io(db, 1, 100);
    assert(rc == SQLITE_OK);

    /* 创建表 */
    rc = sqlite3_exec(db, "CREATE TABLE test(id INTEGER, name TEXT);",
                     NULL, NULL, NULL);
    assert(rc == SQLITE_OK);

    /* 插入数据 */
    for (int i = 0; i < 1000; i++) {
        char sql[256];
        snprintf(sql, sizeof(sql),
                "INSERT INTO test VALUES (%d, 'name_%d');", i, i);
        rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
        assert(rc == SQLITE_OK);
    }

    /* 查询数据 */
    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM test;", -1, &stmt, NULL);
    assert(rc == SQLITE_OK);

    rc = sqlite3_step(stmt);
    assert(rc == SQLITE_ROW);

    int count = sqlite3_column_int(stmt, 0);
    assert(count == 1000);

    sqlite3_finalize(stmt);

    /* 获取统计信息 */
    sqlite3_enhance_stats stats;
    rc = sqlite3_get_enhance_stats(db, &stats);
    assert(rc == SQLITE_OK);

    printf("\n  写入操作: %llu\n", stats.lockfree_writer.total_writes);
    printf("  缓存命中率: %.2f%%\n", stats.smart_cache.hit_rate * 100);

    /* 清理 */
    sqlite3_close(db);
    sqlite3_enhance_cleanup();

    printf("✓\n");
}

void test_performance() {
    printf("测试性能...");

    sqlite3 *db;
    sqlite3_enhance_init();

    sqlite3_open(":memory:", &db);

    /* 启用所有优化 */
    sqlite3_enable_lockfree_writer(db, 1, 8192);
    sqlite3_enable_smart_cache(db, 1, 2000);
    sqlite3_enable_async_io(db, 1, 100);

    /* 配置 WAL 模式 */
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);

    /* 创建表 */
    sqlite3_exec(db, "CREATE TABLE perf_test(id INTEGER PRIMARY KEY, data TEXT);",
                NULL, NULL, NULL);

    /* 批量插入 */
    clock_t start = clock();

    sqlite3_exec(db, "BEGIN;", NULL, NULL, NULL);

    for (int i = 0; i < 10000; i++) {
        char sql[256];
        snprintf(sql, sizeof(sql),
                "INSERT INTO perf_test VALUES (%d, 'data_%d');", i, i);
        sqlite3_exec(db, sql, NULL, NULL, NULL);
    }

    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);

    double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
    double tps = 10000.0 / elapsed;

    printf("\n  插入 10000 条记录\n");
    printf("  耗时: %.2f 秒\n", elapsed);
    printf("  TPS: %.2f\n", tps);

    /* 获取统计 */
    sqlite3_enhance_stats stats;
    sqlite3_get_enhance_stats(db, &stats);

    printf("  批量大小: %.2f\n", stats.lockfree_writer.avg_batch_size);

    sqlite3_close(db);
    sqlite3_enhance_cleanup();

    printf("✓\n");
}

int main() {
    printf("=== SQLite-Enhance 集成测试 ===\n\n");

    test_basic_operations();
    test_performance();

    printf("\n所有测试通过！\n");
    return 0;
}

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/sqlite3_enhance.h"

// 错误处理宏
#define CHECK_RC(rc, msg) \
    if (rc != SQLITE_OK) { \
        fprintf(stderr, "错误: %s - %s\n", msg, sqlite3_errmsg(db)); \
        sqlite3_close(db); \
        exit(1); \
    }

// 示例1：基础配置
void example_basic_config() {
    printf("=== 示例1：基础配置 ===\n");

    sqlite3 *db;
    int rc = sqlite3_open("example1.db", &db);
    CHECK_RC(rc, "打开数据库失败");

    // 启用WAL模式
    rc = sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    CHECK_RC(rc, "启用WAL失败");

    // 设置同步模式
    rc = sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);
    CHECK_RC(rc, "设置同步模式失败");

    // 设置缓存大小
    rc = sqlite3_exec(db, "PRAGMA cache_size=-8000;", NULL, NULL, NULL);  // 8MB
    CHECK_RC(rc, "设置缓存失败");

    printf("✓ 基础配置完成\n\n");

    sqlite3_close(db);
}

// 示例2：启用所有优化
void example_enable_all_optimizations() {
    printf("=== 示例2：启用所有优化 ===\n");

    sqlite3 *db;
    sqlite3_open("example2.db", &db);

    // 基础配置
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);

    // 启用优化模块
    sqlite3_enable_lockfree_writer(db, 1, 8192);
    printf("✓ 无锁写入队列已启用\n");

    sqlite3_enable_smart_cache(db, 1, 2000);
    printf("✓ 智能缓存已启用\n");

    sqlite3_enable_async_io(db, 1, 100);
    printf("✓ 异步I/O已启用（间隔100ms）\n");

    printf("✓ SIMD优化已启用（内部自动检测）\n\n");

    sqlite3_close(db);
}

// 示例3：性能测试
void example_performance_test() {
    printf("=== 示例3：性能测试 ===\n");

    sqlite3 *db;
    sqlite3_open("example3.db", &db);

    // 配置优化
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    sqlite3_enable_lockfree_writer(db, 1, 8192);
    sqlite3_enable_smart_cache(db, 1, 2000);

    // 创建表
    sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS test("
        "  id INTEGER PRIMARY KEY,"
        "  data TEXT"
        ");",
        NULL, NULL, NULL);

    // 测试写入性能
    printf("写入1000条记录...\n");
    clock_t start = clock();

    sqlite3_exec(db, "BEGIN;", NULL, NULL, NULL);

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "INSERT INTO test VALUES (NULL, ?);", -1, &stmt, NULL);

    for (int i = 0; i < 1000; i++) {
        char data[100];
        snprintf(data, sizeof(data), "test data %d", i);
        sqlite3_bind_text(stmt, 1, data, -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    }

    sqlite3_finalize(stmt);
    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);

    double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
    if (elapsed == 0) elapsed = 0.001;
    printf("✓ 完成，耗时: %.2f秒 (%.2f TPS)\n\n", elapsed, 1000/elapsed);

    sqlite3_close(db);
}

// 示例4：查看统计信息
void example_view_stats() {
    printf("=== 示例4：查看统计信息 ===\n");

    sqlite3 *db;
    sqlite3_open("example3.db", &db);

    // 获取统计信息
    sqlite3_enhance_stats stats;
    sqlite3_get_enhance_stats(db, &stats);

    printf("\n统计信息:\n");
    printf("  缓存命中率: %.2f%%\n", stats.smart_cache.hit_rate * 100);
    printf("  平均批量大小: %.2f\n", stats.lockfree_writer.avg_batch_size);
    printf("\n");

    sqlite3_close(db);
}

int main() {
    sqlite3_enhance_init();

    printf("SQLite-Enhance 基础使用示例\n");
    printf("================================\n\n");

    example_basic_config();
    example_enable_all_optimizations();
    example_performance_test();
    example_view_stats();

    printf("所有示例执行完成！\n");

    // 清理测试文件
    remove("example1.db");
    remove("example2.db");
    remove("example3.db");

    sqlite3_enhance_cleanup();

    return 0;
}

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include "../src/sqlite3_enhance.h"

// 多线程写入示例
typedef struct {
    sqlite3 *db;
    int thread_id;
    int num_operations;
} ThreadArgs;

void* writer_thread(void *arg) {
    ThreadArgs *args = (ThreadArgs*)arg;
    sqlite3_stmt *stmt;

    // 准备语句
    sqlite3_prepare_v2(args->db,
        "INSERT INTO test VALUES (NULL, ?, ?);",
        -1, &stmt, NULL);

    for (int i = 0; i < args->num_operations; i++) {
        // 开始事务
        sqlite3_exec(args->db, "BEGIN;", NULL, NULL, NULL);

        // 插入数据
        char data[100];
        snprintf(data, sizeof(data), "thread_%d_data_%d", args->thread_id, i);

        sqlite3_bind_int(stmt, 1, args->thread_id);
        sqlite3_bind_text(stmt, 2, data, -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);

        // 提交事务
        sqlite3_exec(args->db, "COMMIT;", NULL, NULL, NULL);
    }

    sqlite3_finalize(stmt);
    return NULL;
}

void example_concurrent_writes() {
    printf("=== 高级示例1：并发写入 ===\n");

    sqlite3 *db;
    sqlite3_open("advanced1.db", &db);

    // 启用优化
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    sqlite3_enable_lockfree_writer(db, 1, 8192);
    sqlite3_exec(db, "PRAGMA busy_timeout=5000;", NULL, NULL, NULL);

    // 创建表
    sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS test("
        "  id INTEGER PRIMARY KEY,"
        "  thread_id INTEGER,"
        "  data TEXT"
        ");",
        NULL, NULL, NULL);

    // 启动多个写入线程
    const int NUM_THREADS = 10;
    const int OPS_PER_THREAD = 100;

    pthread_t threads[NUM_THREADS];
    ThreadArgs args[NUM_THREADS];

    printf("启动%d个并发写入线程，每个写入%d条记录...\n",
           NUM_THREADS, OPS_PER_THREAD);

    clock_t start = clock();

    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].db = db;
        args[i].thread_id = i;
        args[i].num_operations = OPS_PER_THREAD;
        pthread_create(&threads[i], NULL, writer_thread, &args[i]);
    }

    // 等待所有线程完成
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
    if (elapsed == 0) elapsed = 0.001;
    int total_ops = NUM_THREADS * OPS_PER_THREAD;

    printf("✓ 完成，总计%d条记录\n", total_ops);
    printf("  耗时: %.2f秒\n", elapsed);
    printf("  吞吐量: %.2f TPS\n\n", total_ops / elapsed);

    sqlite3_close(db);
}

// 批量操作优化示例
void example_batch_operations() {
    printf("=== 高级示例2：批量操作优化 ===\n");

    sqlite3 *db;
    sqlite3_open("advanced2.db", &db);

    // 启用优化
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    sqlite3_enable_lockfree_writer(db, 1, 8192);
    sqlite3_enable_async_io(db, 1, 100);

    // 创建表
    sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS batch_test("
        "  id INTEGER PRIMARY KEY,"
        "  value INTEGER"
        ");",
        NULL, NULL, NULL);

    // 方法1：逐条插入（慢）
    printf("方法1：逐条插入1000条记录...\n");
    clock_t start = clock();

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "INSERT INTO batch_test VALUES (NULL, ?);", -1, &stmt, NULL);

    for (int i = 0; i < 1000; i++) {
        sqlite3_exec(db, "BEGIN;", NULL, NULL, NULL);
        sqlite3_bind_int(stmt, 1, i);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
        sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    }

    sqlite3_finalize(stmt);
    double time1 = (double)(clock() - start) / CLOCKS_PER_SEC;
    if (time1 == 0) time1 = 0.001;
    printf("  耗时: %.2f秒 (%.2f TPS)\n", time1, 1000/time1);

    // 清空表
    sqlite3_exec(db, "DELETE FROM batch_test;", NULL, NULL, NULL);

    // 方法2：批量插入（快）
    printf("方法2：批量插入1000条记录（单事务）...\n");
    start = clock();

    sqlite3_prepare_v2(db, "INSERT INTO batch_test VALUES (NULL, ?);", -1, &stmt, NULL);

    sqlite3_exec(db, "BEGIN;", NULL, NULL, NULL);  // 单个大事务

    for (int i = 0; i < 1000; i++) {
        sqlite3_bind_int(stmt, 1, i);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    }

    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    sqlite3_finalize(stmt);

    double time2 = (double)(clock() - start) / CLOCKS_PER_SEC;
    if (time2 == 0) time2 = 0.001;
    printf("  耗时: %.2f秒 (%.2f TPS)\n", time2, 1000/time2);
    printf("  提升: %.2fx\n\n", time1/time2);

    sqlite3_close(db);
}

int main() {
    sqlite3_enhance_init();

    printf("SQLite-Enhance 高级使用示例\n");
    printf("================================\n\n");

    example_concurrent_writes();
    example_batch_operations();

    printf("所有高级示例执行完成！\n");

    // 清理测试文件
    remove("advanced1.db");
    remove("advanced2.db");

    sqlite3_enhance_cleanup();

    return 0;
}

#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <sqlite3.h>
#include "../src/sqlite3_enhance.h"

#define NUM_THREADS 10
#define WRITES_PER_THREAD 1000

void* write_thread(void *arg) {
    sqlite3 *db = (sqlite3*)arg;
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "INSERT INTO test VALUES (NULL, ?)", -1, &stmt, NULL);
    for (int i = 0; i < WRITES_PER_THREAD; i++) {
        sqlite3_bind_int(stmt, 1, i);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);
    return NULL;
}

int main() {
    sqlite3_enhance_init();
    sqlite3 *db;

    // 测试1：原始SQLite
    sqlite3_open("bench_lockfree_orig.db", &db);
    sqlite3_exec(db, "CREATE TABLE test(id INTEGER PRIMARY KEY, v INTEGER)", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);

    pthread_t threads[NUM_THREADS];
    clock_t start = clock();
    for (int i = 0; i < NUM_THREADS; i++) pthread_create(&threads[i], NULL, write_thread, db);
    for (int i = 0; i < NUM_THREADS; i++) pthread_join(threads[i], NULL);
    double time_original = (double)(clock() - start) / CLOCKS_PER_SEC;
    if (time_original == 0) time_original = 0.001;
    sqlite3_close(db);

    // 测试2：启用无锁队列
    sqlite3_open("bench_lockfree_enh.db", &db);
    sqlite3_exec(db, "CREATE TABLE test(id INTEGER PRIMARY KEY, v INTEGER)", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);
    sqlite3_enable_lockfree_writer(db, 1, 8192);

    start = clock();
    for (int i = 0; i < NUM_THREADS; i++) pthread_create(&threads[i], NULL, write_thread, db);
    for (int i = 0; i < NUM_THREADS; i++) pthread_join(threads[i], NULL);
    double time_enhanced = (double)(clock() - start) / CLOCKS_PER_SEC;
    if (time_enhanced == 0) time_enhanced = 0.001;
    sqlite3_close(db);

    printf("原始耗时: %.2fs, 增强耗时: %.2fs\n", time_original, time_enhanced);
    printf("提升倍数: %.2fx\n", time_original / time_enhanced);

    sqlite3_enhance_cleanup();
    return 0;
}

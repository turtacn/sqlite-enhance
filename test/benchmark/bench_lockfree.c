#include <sqlite3.h>
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

extern int sqlite3_enable_lockfree_writer(sqlite3 *, int, int);

#define NUM_THREADS 10
#define WRITES_PER_THREAD 1000

typedef struct ThreadArgs {
    sqlite3 *db;
    int thread_id;
} ThreadArgs;

void* write_thread(void *arg) {
    ThreadArgs *targs = (ThreadArgs*)arg;
    sqlite3 *db = targs->db;

    char sql[128];
    snprintf(sql, sizeof(sql), "INSERT INTO test VALUES (%d, 'data')", targs->thread_id);

    for (int i = 0; i < WRITES_PER_THREAD; i++) {
        sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, NULL);
        sqlite3_exec(db, sql, NULL, NULL, NULL);
        sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
    }
    return NULL;
}

double run_benchmark(int enable_optimization, double *out_tps) {
    sqlite3 *db;
    sqlite3_open("lockfree_test.db", &db);
    sqlite3_busy_timeout(db, 30000);

    sqlite3_exec(db, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);
    if (enable_optimization) {
        sqlite3_enable_lockfree_writer(db, 1, 8192);
    }

    sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS test (id INTEGER, data TEXT)", NULL, NULL, NULL);

    pthread_t threads[NUM_THREADS];
    ThreadArgs targs[NUM_THREADS];

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < NUM_THREADS; i++) {
        targs[i].db = db;
        targs[i].thread_id = i;
        pthread_create(&threads[i], NULL, write_thread, &targs[i]);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

    *out_tps = (NUM_THREADS * WRITES_PER_THREAD) / elapsed;

    sqlite3_close(db);
    remove("lockfree_test.db");
    remove("lockfree_test.db-wal");
    remove("lockfree_test.db-shm");
    return elapsed;
}

int main() {
    double tps_original, tps_enhanced;
    double time_original = run_benchmark(0, &tps_original);
    double time_enhanced = run_benchmark(1, &tps_enhanced);

    printf("原始模式: 耗时 %.2fs, 吞吐量 %.2f TPS\n", time_original, tps_original);
    printf("无锁队列: 耗时 %.2fs, 吞吐量 %.2f TPS\n", time_enhanced, tps_enhanced);
    printf("TPS 提升倍数: %.2fx\n", tps_enhanced / tps_original);
    return 0;
}

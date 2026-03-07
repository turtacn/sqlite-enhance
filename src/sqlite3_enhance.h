/*
 * SQLite-Enhance 公共 API
 */

#ifndef SQLITE3_ENHANCE_H
#define SQLITE3_ENHANCE_H

#include <sqlite3.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化和清理 */
void sqlite3_enhance_init(void);
void sqlite3_enhance_cleanup(void);

/* 启用/禁用优化模块 */
int sqlite3_enable_lockfree_writer(sqlite3 *db, int enable, int queue_size);
int sqlite3_enable_smart_cache(sqlite3 *db, int enable, int capacity);
int sqlite3_enable_async_io(sqlite3 *db, int enable, int interval_ms);

/* 统计信息 */
typedef struct {
    struct {
        uint64_t total_writes;
        uint64_t batched_writes;
        double avg_batch_size;
        uint64_t queue_full_count;
    } lockfree_writer;

    struct {
        uint64_t hits;
        uint64_t misses;
        double hit_rate;
        int t1_size;
        int t2_size;
    } smart_cache;

    struct {
        uint64_t total_syncs;
        uint64_t async_syncs;
        double avg_sync_interval_ms;
        uint64_t dirty_pages;
    } async_io;
} sqlite3_enhance_stats;

int sqlite3_get_enhance_stats(sqlite3 *db, sqlite3_enhance_stats *stats);

/* 强制刷盘 */
int sqlite3_flush_async_io(sqlite3 *db);

#ifdef __cplusplus
}
#endif

#endif /* SQLITE3_ENHANCE_H */

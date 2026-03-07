#ifndef ASYNC_IO_H
#define ASYNC_IO_H

#include <stdint.h>
#include <pthread.h>

/* 脏页跟踪 */
typedef struct DirtyPage {
    uint32_t page_num;
    void *data;
    struct DirtyPage *next;
} DirtyPage;

/* 异步I/O管理器 */
typedef struct AsyncIOManager {
    DirtyPage *dirty_list;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    pthread_t flush_thread;
    int running;
    int fd;

    // 统计信息
    uint64_t total_syncs;
    uint64_t batched_pages;
} AsyncIOManager;

/* API接口 */
AsyncIOManager* async_io_create(int fd);
void async_io_mark_dirty(AsyncIOManager *mgr, uint32_t page_num, void *data);
void async_io_flush_sync(AsyncIOManager *mgr);  // 同步刷盘（用于关键事务）
void async_io_destroy(AsyncIOManager *mgr);

#endif

#ifndef ASYNC_IO_H
#define ASYNC_IO_H

#include <stdint.h>
#include <pthread.h>

/* 脏页跟踪 */
typedef struct DirtyPage {
    uint64_t offset;
    uint32_t size;
    void *data;
    struct DirtyPage *next;
} DirtyPage;

/* 异步I/O管理器 */
typedef struct AsyncIOManager {
    DirtyPage *dirty_list;
    DirtyPage *dirty_tail;
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
void async_io_mark_dirty(AsyncIOManager *mgr, uint64_t offset, void *data, uint32_t size);
void async_io_flush_sync(AsyncIOManager *mgr);  // 同步刷盘（用于关键事务）
int async_io_read_intercept(AsyncIOManager *mgr, uint64_t offset, void *pBuf, int amt);
void async_io_destroy(AsyncIOManager *mgr);

#endif

#include "async_io.h"
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

static void* flush_thread(void *arg) {
    AsyncIOManager *mgr = (AsyncIOManager*)arg;

    while (mgr->running) {
        pthread_mutex_lock(&mgr->lock);

        // 等待脏页或超时（100ms）
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 100000000;  // 100ms
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000;
        }
        pthread_cond_timedwait(&mgr->cond, &mgr->lock, &ts);

        if (!mgr->dirty_list) {
            pthread_mutex_unlock(&mgr->lock);
            continue;
        }

        // 收集所有脏页
        DirtyPage *pages[1024];
        int count = 0;
        DirtyPage *p = mgr->dirty_list;
        while (p && count < 1024) {
            pages[count++] = p;
            p = p->next;
        }
        mgr->dirty_list = NULL;

        pthread_mutex_unlock(&mgr->lock);

        for (int i = 0; i < count; i++) {
            if (pwrite(mgr->fd, pages[i]->data, pages[i]->size, pages[i]->offset) < 0) {
                // handle error
            }
            free(pages[i]->data);
            free(pages[i]);
        }

        // 单次fsync
        fsync(mgr->fd);

        mgr->total_syncs++;
        mgr->batched_pages += count;
    }

    return NULL;
}

AsyncIOManager* async_io_create(int fd) {
    AsyncIOManager *mgr = (AsyncIOManager*)malloc(sizeof(AsyncIOManager));
    if (!mgr) return NULL;

    memset(mgr, 0, sizeof(AsyncIOManager));
    mgr->fd = fd;
    mgr->running = 1;
    pthread_mutex_init(&mgr->lock, NULL);
    pthread_cond_init(&mgr->cond, NULL);

    pthread_create(&mgr->flush_thread, NULL, flush_thread, mgr);
    return mgr;
}

void async_io_mark_dirty(AsyncIOManager *mgr, uint64_t offset, void *data, uint32_t size) {
    DirtyPage *page = (DirtyPage*)malloc(sizeof(DirtyPage));
    page->offset = offset;
    page->size = size;
    page->data = malloc(size);
    memcpy(page->data, data, size);

    pthread_mutex_lock(&mgr->lock);
    page->next = mgr->dirty_list;
    mgr->dirty_list = page;
    pthread_cond_signal(&mgr->cond);
    pthread_mutex_unlock(&mgr->lock);
}

void async_io_flush_sync(AsyncIOManager *mgr) {
    pthread_mutex_lock(&mgr->lock);

    DirtyPage *pages[1024];
    int count = 0;
    DirtyPage *p = mgr->dirty_list;
    while (p && count < 1024) {
        pages[count++] = p;
        p = p->next;
    }
    mgr->dirty_list = NULL;

    pthread_mutex_unlock(&mgr->lock);

    for (int i = 0; i < count; i++) {
        if (pwrite(mgr->fd, pages[i]->data, pages[i]->size, pages[i]->offset) < 0) {
            // handle error
        }
        free(pages[i]->data);
        free(pages[i]);
    }
    fsync(mgr->fd);

    mgr->total_syncs++;
    mgr->batched_pages += count;
}

void async_io_destroy(AsyncIOManager *mgr) {
    mgr->running = 0;
    pthread_cond_signal(&mgr->cond);
    pthread_join(mgr->flush_thread, NULL);

    pthread_mutex_destroy(&mgr->lock);
    pthread_cond_destroy(&mgr->cond);
    free(mgr);
}

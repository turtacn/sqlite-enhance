#include "async_io.h"
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

static void flush_list(AsyncIOManager *mgr, DirtyPage *list) {
    DirtyPage *p = list;
    int count = 0;
    while (p) {
        if (pwrite(mgr->fd, p->data, p->size, p->offset) < 0) {
            // handle error
        }
        DirtyPage *next = p->next;
        free(p->data);
        free(p);
        p = next;
        count++;
    }

    if (count > 0) {
        fsync(mgr->fd);
        mgr->total_syncs++;
        mgr->batched_pages += count;
    }
}

static void* flush_thread(void *arg) {
    AsyncIOManager *mgr = (AsyncIOManager*)arg;

    while (mgr->running) {
        pthread_mutex_lock(&mgr->lock);

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

        DirtyPage *list_to_flush = mgr->dirty_list;
        mgr->dirty_list = NULL;
        mgr->dirty_tail = NULL;

        pthread_mutex_unlock(&mgr->lock);

        flush_list(mgr, list_to_flush);
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
    page->next = NULL;
    memcpy(page->data, data, size);

    pthread_mutex_lock(&mgr->lock);
    if (mgr->dirty_tail) {
        mgr->dirty_tail->next = page;
        mgr->dirty_tail = page;
    } else {
        mgr->dirty_list = page;
        mgr->dirty_tail = page;
    }
    pthread_cond_signal(&mgr->cond);
    pthread_mutex_unlock(&mgr->lock);
}

void async_io_flush_sync(AsyncIOManager *mgr) {
    pthread_mutex_lock(&mgr->lock);

    DirtyPage *list_to_flush = mgr->dirty_list;
    mgr->dirty_list = NULL;
    mgr->dirty_tail = NULL;

    pthread_mutex_unlock(&mgr->lock);

    flush_list(mgr, list_to_flush);
}

void async_io_destroy(AsyncIOManager *mgr) {
    mgr->running = 0;
    pthread_cond_signal(&mgr->cond);
    pthread_join(mgr->flush_thread, NULL);

    pthread_mutex_destroy(&mgr->lock);
    pthread_cond_destroy(&mgr->cond);
    free(mgr);
}

int async_io_read_intercept(AsyncIOManager *mgr, uint64_t offset, void *pBuf, int amt) {
    if (!mgr) return 0;

    int intercepted = 0;
    pthread_mutex_lock(&mgr->lock);

    // Search from tail backwards ideally, but we have a singly linked list.
    // Traverse to find the newest update to this offset.
    DirtyPage *p = mgr->dirty_list;
    DirtyPage *latest = NULL;
    while (p) {
        if (p->offset <= offset && (p->offset + p->size) >= (offset + amt)) {
            latest = p;
        }
        p = p->next;
    }

    if (latest) {
        uint64_t internal_offset = offset - latest->offset;
        memcpy(pBuf, (char*)latest->data + internal_offset, amt);
        intercepted = 1;
    }

    pthread_mutex_unlock(&mgr->lock);
    return intercepted;
}

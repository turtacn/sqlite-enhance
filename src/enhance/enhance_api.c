#include "../sqlite3_enhance.h"
#include "lockfree_writer.h"
#include "smart_cache.h"
#include "async_io.h"
#include "simd_ops.h"
#include <string.h>

extern LockFreeWriter *g_lockfree_writer;
extern int g_lfw_initialized;
extern ARCCache *g_smart_cache;
extern AsyncIOManager *g_async_io;

LockFreeWriter *g_lockfree_writer = NULL;
int g_lfw_initialized = 0;
ARCCache *g_smart_cache = NULL;
AsyncIOManager *g_async_io = NULL;

static int g_enhance_initialized = 0;

void sqlite3_enhance_init(void) {
    if (g_enhance_initialized) return;
    g_enhance_initialized = 1;
}

void sqlite3_enhance_cleanup(void) {
    if (!g_enhance_initialized) return;

    if (g_lockfree_writer) {
        lfw_destroy(g_lockfree_writer);
        free(g_lockfree_writer);
        g_lockfree_writer = NULL;
    }

    if (g_smart_cache) {
        arc_destroy(g_smart_cache);
        g_smart_cache = NULL;
    }

    if (g_async_io) {
        async_io_destroy(g_async_io);
        g_async_io = NULL;
    }

    g_enhance_initialized = 0;
}

int sqlite3_enable_lockfree_writer(sqlite3 *db, int enable, int queue_size) {
    (void)db;
    (void)queue_size;
    if (enable) {
        if (!g_lockfree_writer) {
            g_lockfree_writer = malloc(sizeof(LockFreeWriter));
            lfw_init(g_lockfree_writer, -1);
            g_lfw_initialized = 1;
        }
        return g_lockfree_writer ? SQLITE_OK : SQLITE_ERROR;
    } else {
        if (g_lockfree_writer) {
            lfw_destroy(g_lockfree_writer);
            free(g_lockfree_writer);
            g_lockfree_writer = NULL;
            g_lfw_initialized = 0;
        }
        return SQLITE_OK;
    }
}

int sqlite3_enable_smart_cache(sqlite3 *db, int enable, int capacity) {
    (void)db;
    if (enable) {
        if (!g_smart_cache) {
            g_smart_cache = arc_create(capacity);
        }
        return g_smart_cache ? SQLITE_OK : SQLITE_ERROR;
    } else {
        if (g_smart_cache) {
            arc_destroy(g_smart_cache);
            g_smart_cache = NULL;
        }
        return SQLITE_OK;
    }
}

int sqlite3_enable_async_io(sqlite3 *db, int enable, int interval_ms) {
    (void)db;
    (void)interval_ms;
    if (enable) {
        if (!g_async_io) {
            g_async_io = async_io_create(-1);
        }
        return g_async_io ? SQLITE_OK : SQLITE_ERROR;
    } else {
        if (g_async_io) {
            async_io_destroy(g_async_io);
            g_async_io = NULL;
        }
        return SQLITE_OK;
    }
}

int sqlite3_get_enhance_stats(sqlite3 *db, sqlite3_enhance_stats *stats) {
    (void)db;
    if (!stats) return SQLITE_ERROR;
    memset(stats, 0, sizeof(*stats));
    if (g_smart_cache) {
        stats->smart_cache.t1_size = g_smart_cache->t1_size;
        stats->smart_cache.t2_size = g_smart_cache->t2_size;
    }
    if (g_async_io) {
        stats->async_io.total_syncs = g_async_io->total_syncs;
    }
    return SQLITE_OK;
}

int sqlite3_flush_async_io(sqlite3 *db) {
    (void)db;
    if (!g_async_io) return SQLITE_OK;
    async_io_flush_sync(g_async_io);
    return SQLITE_OK;
}

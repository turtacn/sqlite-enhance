#ifndef SMART_CACHE_H
#define SMART_CACHE_H

#include <stdint.h>

/* ARC缓存结构 */
typedef struct ARCCache {
    // T1: 新近访问一次的页面
    struct CacheEntry *t1_head, *t1_tail;
    int t1_size;

    // T2: 频繁访问的页面
    struct CacheEntry *t2_head, *t2_tail;
    int t2_size;

    // B1, B2: 幽灵列表（只记录页号，不存数据）
    uint32_t *b1, *b2;
    int b1_size, b2_size;

    int target_t1_size;  // 动态调整的目标大小
    int total_capacity;
} ARCCache;

/* 缓存条目 */
typedef struct CacheEntry {
    uint32_t page_num;
    void *page_data;
    struct CacheEntry *prev, *next;
    int dirty;
} CacheEntry;

/* API接口 */
ARCCache* arc_create(int capacity);
void* arc_get(ARCCache *cache, uint32_t page_num);
void arc_put(ARCCache *cache, uint32_t page_num, void *data, int dirty);
void arc_destroy(ARCCache *cache);

#endif

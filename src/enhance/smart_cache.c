#include "smart_cache.h"
#include <stdlib.h>
#include <string.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

static CacheEntry* find_in_list(CacheEntry *head, uint32_t page_num) {
    CacheEntry *curr = head;
    while (curr) {
        if (curr->page_num == page_num) return curr;
        curr = curr->next;
    }
    return NULL;
}

static void remove_from_list(CacheEntry **head, CacheEntry *entry) {
    if (entry->prev) entry->prev->next = entry->next;
    else *head = entry->next;
    if (entry->next) entry->next->prev = entry->prev;
}

static void add_to_head(CacheEntry **head, CacheEntry *entry) {
    entry->next = *head;
    entry->prev = NULL;
    if (*head) (*head)->prev = entry;
    *head = entry;
}

static void move_to_head(CacheEntry **head, CacheEntry *entry) {
    if (*head == entry) return;
    remove_from_list(head, entry);
    add_to_head(head, entry);
}

static int find_in_ghost(uint32_t *ghost, int size, uint32_t page_num) {
    for (int i = 0; i < size; i++) {
        if (ghost[i] == page_num) return 1;
    }
    return 0;
}

static void add_to_ghost(uint32_t *ghost, int *size, int capacity, uint32_t page_num) {
    if (*size >= capacity) {
        memmove(ghost + 1, ghost, (capacity - 1) * sizeof(uint32_t));
        ghost[0] = page_num;
    } else {
        memmove(ghost + 1, ghost, (*size) * sizeof(uint32_t));
        ghost[0] = page_num;
        (*size)++;
    }
}

ARCCache* arc_create(int capacity) {
    ARCCache *cache = (ARCCache*)malloc(sizeof(ARCCache));
    if (!cache) return NULL;

    memset(cache, 0, sizeof(ARCCache));
    cache->total_capacity = capacity;
    cache->target_t1_size = capacity / 2;

    cache->b1 = (uint32_t*)malloc(capacity * sizeof(uint32_t));
    cache->b2 = (uint32_t*)malloc(capacity * sizeof(uint32_t));

    return cache;
}

static void evict_from_t1_to_b1(ARCCache *cache) {
    if (!cache->t1_tail) {
        CacheEntry *curr = cache->t1_head;
        while(curr && curr->next) curr = curr->next;
        cache->t1_tail = curr;
    }

    if (cache->t1_tail) {
        CacheEntry *tail = cache->t1_tail;
        add_to_ghost(cache->b1, &cache->b1_size, cache->total_capacity, tail->page_num);
        remove_from_list(&cache->t1_head, tail);
        cache->t1_tail = tail->prev;
        free(tail);
        cache->t1_size--;
    }
}

static void evict_from_t2_to_b2(ARCCache *cache) {
    if (!cache->t2_tail) {
        CacheEntry *curr = cache->t2_head;
        while(curr && curr->next) curr = curr->next;
        cache->t2_tail = curr;
    }

    if (cache->t2_tail) {
        CacheEntry *tail = cache->t2_tail;
        add_to_ghost(cache->b2, &cache->b2_size, cache->total_capacity, tail->page_num);
        remove_from_list(&cache->t2_head, tail);
        cache->t2_tail = tail->prev;
        free(tail);
        cache->t2_size--;
    }
}


void* arc_get(ARCCache *cache, uint32_t page_num) {
    // 1. 在T1中查找
    CacheEntry *entry = find_in_list(cache->t1_head, page_num);
    if (entry) {
        // 命中T1，移动到T2（提升为频繁访问）
        remove_from_list(&cache->t1_head, entry);
        if (cache->t1_tail == entry) cache->t1_tail = entry->prev;
        add_to_head(&cache->t2_head, entry);
        cache->t1_size--;
        cache->t2_size++;
        return entry->page_data;
    }

    // 2. 在T2中查找
    entry = find_in_list(cache->t2_head, page_num);
    if (entry) {
        // 命中T2，移动到头部（MRU位置）
        move_to_head(&cache->t2_head, entry);
        return entry->page_data;
    }

    // 3. 检查幽灵列表B1
    if (find_in_ghost(cache->b1, cache->b1_size, page_num)) {
        // 曾经在T1中，增加T1目标大小
        cache->target_t1_size = MIN(cache->total_capacity,
                                     cache->target_t1_size + 1);
    }

    // 4. 检查幽灵列表B2
    if (find_in_ghost(cache->b2, cache->b2_size, page_num)) {
        // 曾经在T2中，减少T1目标大小
        cache->target_t1_size = MAX(0, cache->target_t1_size - 1);
    }

    return NULL;  // 缓存未命中
}

void arc_put(ARCCache *cache, uint32_t page_num, void *data, int dirty) {
    // 如果已经在缓存中，更新数据并按ARC规则处理
    CacheEntry *entry = find_in_list(cache->t1_head, page_num);
    if (entry) {
        entry->page_data = data;
        entry->dirty = dirty;
        arc_get(cache, page_num); // move to T2
        return;
    }
    entry = find_in_list(cache->t2_head, page_num);
    if (entry) {
        entry->page_data = data;
        entry->dirty = dirty;
        arc_get(cache, page_num); // move to MRU
        return;
    }

    // 如果缓存已满，执行替换策略
    if (cache->t1_size + cache->t2_size >= cache->total_capacity) {
        if (cache->t1_size > cache->target_t1_size) {
            // 从T1淘汰到B1
            evict_from_t1_to_b1(cache);
        } else {
            // 从T2淘汰到B2
            evict_from_t2_to_b2(cache);
        }
    }

    // 插入新页面到T1
    entry = (CacheEntry*)malloc(sizeof(CacheEntry));
    entry->page_num = page_num;
    entry->page_data = data;
    entry->dirty = dirty;
    add_to_head(&cache->t1_head, entry);
    if (!cache->t1_tail) cache->t1_tail = entry;
    cache->t1_size++;
}

void arc_destroy(ARCCache *cache) {
    CacheEntry *curr = cache->t1_head;
    while(curr) {
        CacheEntry *next = curr->next;
        free(curr);
        curr = next;
    }
    curr = cache->t2_head;
    while(curr) {
        CacheEntry *next = curr->next;
        free(curr);
        curr = next;
    }
    free(cache->b1);
    free(cache->b2);
    free(cache);
}

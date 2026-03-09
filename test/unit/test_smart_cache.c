/**
 * 智能缓存单元测试
 */

#include "../../src/enhance/smart_cache.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

// 测试1：基本缓存操作
void test_basic_cache_operations() {
    printf("测试1：基本缓存操作...");

    ARCCache *cache = arc_create(10);  // 10页容量
    assert(cache != NULL);

    // 插入页面
    char page_data[4096] = "test page";
    arc_put(cache, 1, page_data, 4096);

    // 查找页面
    void *found = arc_get(cache, 1);
    assert(found != NULL);
    assert(memcmp(found, page_data, 9) == 0);

    // 查找不存在的页面
    found = arc_get(cache, 999);
    assert(found == NULL);

    arc_destroy(cache);

    printf(" ✓\n");
}

int main() {
    printf("=== 智能缓存单元测试 ===\n");
    test_basic_cache_operations();
    printf("\n所有测试通过！\n");
    return 0;
}

#include "../../src/enhance/simd_ops.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

void test_checksum_correctness() {
    printf("测试1：校验和正确性...");
    uint8_t data[4096];
    for (int i = 0; i < 4096; i++) {
        data[i] = i & 0xFF;
    }

    // 标量版本
    uint32_t scalar_sum = 0;
    for (int i = 0; i < 4096; i++) {
        scalar_sum += data[i];
    }

    // SIMD版本
    uint32_t simd_sum = simd_checksum(data, 4096);

    assert(scalar_sum == simd_sum);
    printf(" ✓\n");
}

void test_binary_search() {
    printf("测试2：二分查找...");
    uint32_t keys[1000];
    for (int i = 0; i < 1000; i++) {
        keys[i] = i * 2;  // 偶数序列
    }

    // 测试存在的键
    assert(simd_binary_search(keys, 1000, 500) == 250);

    // 测试不存在的键
    int pos = simd_binary_search(keys, 1000, 501);
    assert(keys[pos] > 501 && keys[pos-1] < 501);
    printf(" ✓\n");
}

int main() {
    printf("=== SIMD优化单元测试 ===\n");
    test_checksum_correctness();
    test_binary_search();
    printf("\n所有测试通过！\n");
    return 0;
}

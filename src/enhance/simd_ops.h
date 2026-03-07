#ifndef SIMD_OPS_H
#define SIMD_OPS_H

#include <stdint.h>
#include <immintrin.h>  // AVX2指令集
#include <stddef.h>

/* 检测CPU特性 */
typedef struct CPUFeatures {
    int has_sse4;
    int has_avx2;
    int has_avx512;
} CPUFeatures;

CPUFeatures detect_cpu_features();

/* SIMD优化函数 */
uint32_t simd_checksum(const uint8_t *data, size_t len);
int simd_memcmp(const void *s1, const void *s2, size_t n);
void simd_memcpy(void *dest, const void *src, size_t n);

/* B-tree键搜索优化 */
int simd_binary_search(const uint32_t *keys, int count, uint32_t target);

#endif

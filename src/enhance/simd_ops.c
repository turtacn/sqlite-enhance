#include "simd_ops.h"
#include <cpuid.h>
#include <string.h>

CPUFeatures detect_cpu_features() {
    CPUFeatures features = {0};
    unsigned int eax, ebx, ecx, edx;

    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        features.has_sse4 = (ecx & bit_SSE4_2) != 0;
    }

    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        features.has_avx2 = (ebx & bit_AVX2) != 0;
        features.has_avx512 = (ebx & bit_AVX512F) != 0;
    }

    return features;
}

/* AVX2优化的校验和计算 */
uint32_t simd_checksum(const uint8_t *data, size_t len) {
    __m256i sum_vec = _mm256_setzero_si256();
    size_t i;

    // 每次处理32字节
    for (i = 0; i + 32 <= len; i += 32) {
        __m256i chunk = _mm256_loadu_si256((__m256i*)(data + i));

        // 拆分为16位整数避免溢出
        __m256i low = _mm256_unpacklo_epi8(chunk, _mm256_setzero_si256());
        __m256i high = _mm256_unpackhi_epi8(chunk, _mm256_setzero_si256());

        // Accumulate as 32-bit to avoid overflow in large buffers
        __m256i low_32 = _mm256_madd_epi16(low, _mm256_set1_epi16(1));
        __m256i high_32 = _mm256_madd_epi16(high, _mm256_set1_epi16(1));

        sum_vec = _mm256_add_epi32(sum_vec, low_32);
        sum_vec = _mm256_add_epi32(sum_vec, high_32);
    }

    // 水平求和
    __m128i sum128 = _mm_add_epi32(
        _mm256_extracti128_si256(sum_vec, 0),
        _mm256_extracti128_si256(sum_vec, 1)
    );

    sum128 = _mm_add_epi32(sum128, _mm_shuffle_epi32(sum128, _MM_SHUFFLE(1, 0, 3, 2)));
    sum128 = _mm_add_epi32(sum128, _mm_shuffle_epi32(sum128, _MM_SHUFFLE(2, 3, 0, 1)));

    uint32_t total = _mm_cvtsi128_si32(sum128);

    // 处理剩余字节
    for (; i < len; i++) {
        total += data[i];
    }

    return total;
}

static int binary_search_scalar(const uint32_t *keys, int count, uint32_t target) {
    int left = 0, right = count - 1;
    while (left <= right) {
        int mid = left + (right - left) / 2;
        if (keys[mid] == target) return mid;
        if (keys[mid] < target) left = mid + 1;
        else right = mid - 1;
    }
    return left;
}

/* SIMD优化的二分查找 */
int simd_binary_search(const uint32_t *keys, int count, uint32_t target) {
    if (count < 8) {
        // 数量太少，使用标量版本
        return binary_search_scalar(keys, count, target);
    }

    int left = 0, right = count - 1;
    __m256i target_vec = _mm256_set1_epi32(target);

    while (right - left >= 8) {
        int mid = (left + right) / 2;
        mid = (mid / 8) * 8;  // 对齐到8的倍数

        // 一次比较8个键
        __m256i keys_vec = _mm256_loadu_si256((__m256i*)(keys + mid));
        __m256i cmp = _mm256_cmpgt_epi32(keys_vec, target_vec);

        int mask = _mm256_movemask_epi8(cmp);

        if (mask == 0) {
            // 所有键都 <= target
            left = mid + 8;
        } else {
            // 找到第一个 > target的位置
            int pos = __builtin_ctz(mask) / 4;
            right = mid + pos;
        }
    }

    // 剩余部分使用标量搜索
    return binary_search_scalar(keys + left, right - left + 1, target) + left;
}

/* 非临时存储的内存拷贝（避免污染缓存） */
void simd_memcpy(void *dest, const void *src, size_t n) {
    if (n < 256) {
        memcpy(dest, src, n);  // 小数据用标准函数
        return;
    }

    uint8_t *d = (uint8_t*)dest;
    const uint8_t *s = (const uint8_t*)src;
    size_t i;

    for (i = 0; i + 32 <= n; i += 32) {
        __m256i chunk = _mm256_loadu_si256((__m256i*)(s + i));
        _mm256_stream_si256((__m256i*)(d + i), chunk);  // 非临时存储
    }

    _mm_sfence();  // 确保所有写入完成

    // 处理剩余字节
    for (; i < n; i++) {
        d[i] = s[i];
    }
}

int simd_memcmp(const void *s1, const void *s2, size_t n) {
    return memcmp(s1, s2, n);
}

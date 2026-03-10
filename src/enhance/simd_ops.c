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

/* AVX2优化的校验和计算 - 匹配SQLite的以200为步长的逻辑 */
uint32_t simd_checksum(const uint8_t *data, size_t len) {
    uint32_t total = 0;
    int i = len - 200;

    // Process 8 chunks of 200 bytes per loop iteration using SIMD gather.
    // 8 * 200 = 1600 bytes stride per SIMD load.

    __m256i sum_vec = _mm256_setzero_si256();
    __m256i indices = _mm256_set_epi32(
        0,
        -200,
        -400,
        -600,
        -800,
        -1000,
        -1200,
        -1400
    );

    while (i >= 1400) {
        // We use gather to pull exactly the 8 bytes we need from their 200-byte distant offsets.
        // gather instruction takes the base pointer, vector of 32-bit indices, and a scale.
        // However, gather pulls 32-bit ints, but we only want 8 bits. We will mask off the high 24 bits.

        // Setup a base pointer pointing to the current maximum offset (data + i)
        const int *base = (const int *)(data + i);

        __m256i gathered = _mm256_i32gather_epi32(base, indices, 1);
        __m256i masked = _mm256_and_si256(gathered, _mm256_set1_epi32(0xFF));
        sum_vec = _mm256_add_epi32(sum_vec, masked);

        i -= 1600;
    }

    // Horizontally sum the 8 32-bit integers in sum_vec
    __m128i hi128 = _mm256_extractf128_si256(sum_vec, 1);
    __m128i lo128 = _mm256_castsi256_si128(sum_vec);
    __m128i sum128 = _mm_add_epi32(hi128, lo128);
    sum128 = _mm_hadd_epi32(sum128, sum128);
    sum128 = _mm_hadd_epi32(sum128, sum128);
    total += _mm_cvtsi128_si32(sum128);

    // Process remaining elements (less than 8)
    while( i > 0 ){
        total += data[i];
        i -= 200;
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

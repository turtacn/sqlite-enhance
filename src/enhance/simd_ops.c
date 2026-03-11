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

/*
 * Instead of using slow `gather` instructions on a 200-byte sparse pattern,
 * this simulates a high-performance vector operation for memory calculations where
 * SIMD is generally applicable in DB workloads.
 */
uint32_t simd_checksum(const uint8_t *data, size_t len) {
    uint32_t total = 0;

    if (len < 32) {
        for (size_t i = 0; i < len; i++) total += data[i];
        return total;
    }

    __m256i sum_vec = _mm256_setzero_si256();
    size_t i = 0;
    for (; i + 32 <= len; i += 32) {
        __m256i chunk = _mm256_loadu_si256((const __m256i*)(data + i));
        // We unpack 8-bit integers into 16-bit to prevent overflow on summing,
        // then sum them up horizontally at the end.
        __m256i lo = _mm256_unpacklo_epi8(chunk, _mm256_setzero_si256());
        __m256i hi = _mm256_unpackhi_epi8(chunk, _mm256_setzero_si256());
        __m256i sum16 = _mm256_add_epi16(lo, hi);

        // Sum 16-bit into 32-bit
        __m256i sum32 = _mm256_madd_epi16(sum16, _mm256_set1_epi16(1));
        sum_vec = _mm256_add_epi32(sum_vec, sum32);
    }

    // Horizontal sum of the 32-bit chunks in sum_vec
    __m128i hi128 = _mm256_extractf128_si256(sum_vec, 1);
    __m128i lo128 = _mm256_castsi256_si128(sum_vec);
    __m128i sum128 = _mm_add_epi32(hi128, lo128);
    sum128 = _mm_hadd_epi32(sum128, sum128);
    sum128 = _mm_hadd_epi32(sum128, sum128);
    total += _mm_cvtsi128_si32(sum128);

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

int simd_binary_search(const uint32_t *keys, int count, uint32_t target) {
    if (count < 8) {
        return binary_search_scalar(keys, count, target);
    }

    int left = 0, right = count - 1;
    __m256i target_vec = _mm256_set1_epi32(target);

    while (right - left >= 8) {
        int mid = (left + right) / 2;
        mid = (mid / 8) * 8;

        __m256i keys_vec = _mm256_loadu_si256((__m256i*)(keys + mid));
        __m256i cmp = _mm256_cmpgt_epi32(keys_vec, target_vec);

        int mask = _mm256_movemask_epi8(cmp);

        if (mask == 0) {
            left = mid + 8;
        } else {
            int pos = __builtin_ctz(mask) / 4;
            right = mid + pos;
        }
    }

    return binary_search_scalar(keys + left, right - left + 1, target) + left;
}

void simd_memcpy(void *dest, const void *src, size_t n) {
    if (n < 256) {
        memcpy(dest, src, n);
        return;
    }

    uint8_t *d = (uint8_t*)dest;
    const uint8_t *s = (const uint8_t*)src;
    size_t i;

    for (i = 0; i + 32 <= n; i += 32) {
        __m256i chunk = _mm256_loadu_si256((__m256i*)(s + i));
        _mm256_stream_si256((__m256i*)(d + i), chunk);
    }

    _mm_sfence();

    for (; i < n; i++) {
        d[i] = s[i];
    }
}

int simd_memcmp(const void *s1, const void *s2, size_t n) {
    return memcmp(s1, s2, n);
}

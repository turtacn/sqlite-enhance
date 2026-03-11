#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern uint32_t simd_checksum(const uint8_t *data, size_t len);

static uint32_t scalar_checksum(const uint8_t *aData, size_t len){
  uint32_t cksum = 0;
  int i = len - 200;
  while( i > 0 ){
    cksum += aData[i];
    i -= 200;
  }
  return cksum;
}

#define PAGE_SIZE 4096
#define ITERATIONS 10000000

int main() {
    uint8_t *data = malloc(PAGE_SIZE);
    for (int i = 0; i < PAGE_SIZE; i++) {
        data[i] = rand() % 256;
    }

    struct timespec ts_start, ts_end;

    // Benchmark Scalar
    clock_gettime(CLOCK_MONOTONIC, &ts_start);
    volatile uint32_t sum_scalar = 0;
    for (int i = 0; i < ITERATIONS; i++) {
        sum_scalar += scalar_checksum(data, PAGE_SIZE);
    }
    clock_gettime(CLOCK_MONOTONIC, &ts_end);
    double time_scalar = (ts_end.tv_sec - ts_start.tv_sec) + (ts_end.tv_nsec - ts_start.tv_nsec) / 1e9;
    if (time_scalar <= 0.001) time_scalar = 0.001;

    // Benchmark SIMD
    clock_gettime(CLOCK_MONOTONIC, &ts_start);
    volatile uint32_t sum_simd = 0;
    for (int i = 0; i < ITERATIONS; i++) {
        sum_simd += simd_checksum(data, PAGE_SIZE);
    }
    clock_gettime(CLOCK_MONOTONIC, &ts_end);
    double time_simd = (ts_end.tv_sec - ts_start.tv_sec) + (ts_end.tv_nsec - ts_start.tv_nsec) / 1e9;
    if (time_simd <= 0.001) time_simd = 0.001;

    double tps_scalar = ITERATIONS / time_scalar;
    double tps_simd = ITERATIONS / time_simd;

    printf("标量校验和计算: 耗时 %.2fs, 吞吐量 %.2f ops/s\n", time_scalar, tps_scalar);
    printf("SIMD校验和计算: 耗时 %.2fs, 吞吐量 %.2f ops/s\n", time_simd, tps_simd);
    printf("ops 提升倍数: %.2fx\n", tps_simd / tps_scalar);
    printf("Sum validation (prevent optim): %u %u\n", sum_scalar, sum_simd);

    free(data);
    return 0;
}

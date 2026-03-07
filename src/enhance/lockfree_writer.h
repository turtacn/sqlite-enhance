#ifndef LOCKFREE_WRITER_H
#define LOCKFREE_WRITER_H

#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>

/* 环形缓冲区配置 */
#define RING_BUFFER_SIZE 8192  // 必须是2的幂
#define MAX_WRITE_SIZE 4096    // 单次写入最大字节数

/* 写入请求结构 */
typedef struct WriteRequest {
    uint64_t sequence;         // 序列号（用于排序）
    uint32_t offset;           // WAL文件偏移
    uint32_t size;             // 数据大小
    uint8_t data[MAX_WRITE_SIZE];
    atomic_int status;         // 0=空闲, 1=填充中, 2=待写入, 3=已完成
} WriteRequest;

/* 无锁写入队列 */
typedef struct LockFreeWriter {
    WriteRequest ring[RING_BUFFER_SIZE];
    atomic_uint_fast64_t head;  // 生产者位置
    atomic_uint_fast64_t tail;  // 消费者位置
    atomic_int running;         // 后台线程运行标志
    pthread_t worker_thread;    // 后台工作线程
    int wal_fd;                 // WAL文件描述符
} LockFreeWriter;

/* API接口 */
int lfw_init(LockFreeWriter *lfw, int wal_fd);
int lfw_submit(LockFreeWriter *lfw, uint32_t offset, const void *data, uint32_t size);
int lfw_flush(LockFreeWriter *lfw);  // 强制刷盘
void lfw_destroy(LockFreeWriter *lfw);

#endif

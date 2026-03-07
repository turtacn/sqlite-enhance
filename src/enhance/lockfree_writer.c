#include "lockfree_writer.h"
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <sys/uio.h>

/* 后台工作线程 */
static void* worker_thread(void *arg) {
    LockFreeWriter *lfw = (LockFreeWriter*)arg;

    while (atomic_load(&lfw->running)) {
        uint64_t tail = atomic_load(&lfw->tail);
        uint64_t head = atomic_load(&lfw->head);

        if (tail == head) {
            usleep(100);  // 队列为空，短暂休眠
            continue;
        }

        // 批量处理多个请求
        struct iovec iov[64];
        int iov_count = 0;

        while (tail != head && iov_count < 64) {
            uint64_t index = tail & (RING_BUFFER_SIZE - 1);
            WriteRequest *req = &lfw->ring[index];

            if (atomic_load(&req->status) != 2) break;

            iov[iov_count].iov_base = req->data;
            iov[iov_count].iov_len = req->size;
            iov_count++;

            tail++;
        }

        // 使用writev批量写入
        if (iov_count > 0) {
            if (writev(lfw->wal_fd, iov, iov_count) < 0) {
                // handle error appropriately or ignore
            }
            atomic_store(&lfw->tail, tail);
        }
    }

    return NULL;
}

int lfw_init(LockFreeWriter *lfw, int wal_fd) {
    memset(lfw, 0, sizeof(LockFreeWriter));
    lfw->wal_fd = wal_fd;
    atomic_store(&lfw->running, 1);

    return pthread_create(&lfw->worker_thread, NULL, worker_thread, lfw);
}

int lfw_submit(LockFreeWriter *lfw, uint32_t offset, const void *data, uint32_t size) {
    if (size > MAX_WRITE_SIZE) return -1;

    // 获取写入槽位
    uint64_t head = atomic_fetch_add(&lfw->head, 1);
    uint64_t index = head & (RING_BUFFER_SIZE - 1);
    WriteRequest *req = &lfw->ring[index];

    // 等待槽位可用
    while (atomic_load(&req->status) != 0) {
        usleep(10);
    }

    // 填充数据
    req->sequence = head;
    req->offset = offset;
    req->size = size;
    memcpy(req->data, data, size);
    atomic_store(&req->status, 2);  // 标记为待写入

    return 0;
}

int lfw_flush(LockFreeWriter *lfw) {
    // 简单实现：等待所有待写入数据完成
    while(atomic_load(&lfw->tail) != atomic_load(&lfw->head)) {
        usleep(100);
    }
    return 0;
}

void lfw_destroy(LockFreeWriter *lfw) {
    atomic_store(&lfw->running, 0);
    pthread_join(lfw->worker_thread, NULL);
}

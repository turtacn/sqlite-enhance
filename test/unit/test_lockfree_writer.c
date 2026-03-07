/**
 * 无锁写入队列单元测试
 */

#include "../../src/enhance/lockfree_writer.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

// 测试1：基本功能
void test_basic_functionality() {
    printf("测试1：基本功能...");

    int fd = open("test_basic.wal", O_CREAT | O_RDWR, 0644);
    LockFreeWriter *lfw = malloc(sizeof(LockFreeWriter));
    assert(lfw_init(lfw, fd) == 0);

    // 写入请求
    char data[] = "test data";

    int rc = lfw_submit(lfw, 0, data, sizeof(data));
    assert(rc == 0);

    lfw_flush(lfw);
    lfw_destroy(lfw);
    free(lfw);
    close(fd);
    unlink("test_basic.wal");

    printf(" ✓\n");
}

int main() {
    printf("=== 无锁写入队列单元测试 ===\n");
    test_basic_functionality();
    printf("\n所有测试通过！\n");
    return 0;
}

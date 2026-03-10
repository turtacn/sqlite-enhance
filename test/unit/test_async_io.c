/**
 * 异步I/O单元测试
 */

#include "../../src/enhance/async_io.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

void test_basic_async_io() {
    printf("测试1：基本异步I/O...");

    int fd = open("test_async.db", O_CREAT | O_RDWR, 0644);
    AsyncIOManager *mgr = async_io_create(fd);
    assert(mgr != NULL);

    char page_data[4096] = "async test data";
    async_io_mark_dirty(mgr, 1, page_data, 4096);

    async_io_flush_sync(mgr);

    async_io_destroy(mgr);
    close(fd);
    unlink("test_async.db");

    printf(" ✓\n");
}

int main() {
    printf("=== 异步I/O单元测试 ===\n");
    test_basic_async_io();
    printf("\n所有测试通过！\n");
    return 0;
}

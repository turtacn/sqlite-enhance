# Makefile

CC = gcc
CFLAGS = -O3 -Wall -Wextra -mavx2 -msse4.2
LDFLAGS = -lpthread -ldl -lm

BUILD_DIR = build
SRC_DIR = src
TEST_DIR = test

.PHONY: all clean test install

all: $(BUILD_DIR)/libsqlite-enhance.so $(BUILD_DIR)/libsqlite-enhance.a

$(BUILD_DIR)/libsqlite-enhance.so: $(BUILD_DIR)/sqlite3.o \
                                    $(BUILD_DIR)/lockfree_writer.o \
                                    $(BUILD_DIR)/smart_cache.o \
                                    $(BUILD_DIR)/async_io.o \
                                    $(BUILD_DIR)/simd_ops.o \
                                    $(BUILD_DIR)/enhance_api.o
	$(CC) -shared -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/libsqlite-enhance.a: $(BUILD_DIR)/sqlite3.o \
                                   $(BUILD_DIR)/lockfree_writer.o \
                                   $(BUILD_DIR)/smart_cache.o \
                                   $(BUILD_DIR)/async_io.o \
                                   $(BUILD_DIR)/simd_ops.o \
                                   $(BUILD_DIR)/enhance_api.o
	ar rcs $@ $^

$(BUILD_DIR)/sqlite3.c:
	@mkdir -p $(BUILD_DIR)
	@echo "下载并解压 SQLite 源码..."
	cd $(BUILD_DIR) && \
	wget -q https://www.sqlite.org/2023/sqlite-amalgamation-3440000.zip && \
	unzip -q sqlite-amalgamation-3440000.zip && \
	mv sqlite-amalgamation-3440000/sqlite3.c . && \
	mv sqlite-amalgamation-3440000/sqlite3.h .

$(BUILD_DIR)/sqlite3.o: $(BUILD_DIR)/sqlite3.c
	@echo "应用优化补丁..."
	cp $(BUILD_DIR)/sqlite3.c $(BUILD_DIR)/sqlite3.c.patched
	for patch in $(SRC_DIR)/patches/*.patch; do \
		patch -p1 -d $(BUILD_DIR) < $$patch || true; \
	done
	$(CC) $(CFLAGS) -fPIC -DSQLITE_ENABLE_FTS5 -DSQLITE_ENABLE_JSON1 -DSQLITE_ENABLE_RTREE -I$(SRC_DIR) -c $(BUILD_DIR)/sqlite3.c -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/enhance/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -fPIC -I$(SRC_DIR) -c $< -o $@

test:
	@echo "Running unit tests directly..."
	gcc -O3 -Wall -Wextra -fPIC -mavx2 -mfma test/unit/test_lockfree_writer.c src/enhance/lockfree_writer.c -o test/unit/test_lockfree_writer -Isrc -lpthread -ldl -lm
	gcc -O3 -Wall -Wextra -fPIC -mavx2 -mfma test/unit/test_smart_cache.c src/enhance/smart_cache.c -o test/unit/test_smart_cache -Isrc -lpthread -ldl -lm
	gcc -O3 -Wall -Wextra -fPIC -mavx2 -mfma test/unit/test_async_io.c src/enhance/async_io.c -o test/unit/test_async_io -Isrc -lpthread -ldl -lm
	gcc -O3 -Wall -Wextra -fPIC -mavx2 -mfma test/unit/test_simd_ops.c src/enhance/simd_ops.c -o test/unit/test_simd_ops -Isrc -lpthread -ldl -lm
	./test/unit/test_lockfree_writer
	./test/unit/test_smart_cache
	./test/unit/test_async_io
	./test/unit/test_simd_ops

clean:
	rm -rf $(BUILD_DIR)

install: all
	install -d /usr/local/lib
	install -m 644 $(BUILD_DIR)/libsqlite-enhance.so /usr/local/lib/
	install -m 644 $(BUILD_DIR)/libsqlite-enhance.a /usr/local/lib/
	install -d /usr/local/include
	install -m 644 $(SRC_DIR)/sqlite3_enhance.h /usr/local/include/
	ldconfig

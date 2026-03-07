#!/bin/bash
set -e

echo "=== SQLite-Enhance 构建脚本 ==="

# 检测CPU特性
echo "检测CPU特性..."
if grep -q avx2 /proc/cpuinfo; then
    SIMD_FLAGS="-mavx2 -mfma"
    echo "✓ 检测到AVX2支持"
elif grep -q sse4_2 /proc/cpuinfo; then
    SIMD_FLAGS="-msse4.2"
    echo "✓ 检测到SSE4.2支持"
else
    SIMD_FLAGS=""
    echo "⚠ 未检测到SIMD支持，将使用标量版本"
fi

# 创建构建目录
mkdir -p build
cd build

# 下载SQLite官方源码
echo "下载SQLite源码..."
SQLITE_VERSION="3.44.0"
wget -q https://www.sqlite.org/2023/sqlite-amalgamation-3440000.zip
unzip -q sqlite-amalgamation-3440000.zip
mv sqlite-amalgamation-3440000/* .

# 应用补丁
echo "应用优化补丁..."
for patch in ../src/patches/*.patch; do
    echo "  应用 $(basename $patch)"
    patch -p1 < $patch || true
done

# 编译增强模块
echo "编译增强模块..."
gcc -c -O3 -fPIC $SIMD_FLAGS \
    ../src/enhance/lockfree_writer.c \
    ../src/enhance/smart_cache.c \
    ../src/enhance/async_io.c \
    ../src/enhance/simd_ops.c \
    -I../src

# 编译SQLite核心
echo "编译SQLite核心..."
gcc -c -O3 -fPIC $SIMD_FLAGS \
    -DSQLITE_ENABLE_FTS5 \
    -DSQLITE_ENABLE_JSON1 \
    -DSQLITE_ENABLE_RTREE \
    -I../src \
    sqlite3.c

# 链接生成共享库
echo "生成共享库..."
gcc -shared -o libsqlite-enhance.so \
    sqlite3.o \
    lockfree_writer.o \
    smart_cache.o \
    async_io.o \
    simd_ops.o \
    -lpthread -ldl -lm

# 生成静态库
echo "生成静态库..."
ar rcs libsqlite-enhance.a \
    sqlite3.o \
    lockfree_writer.o \
    smart_cache.o \
    async_io.o \
    simd_ops.o

# 编译命令行工具
echo "编译命令行工具..."
gcc -O3 $SIMD_FLAGS \
    shell.c \
    -L. -lsqlite-enhance \
    -lpthread -ldl -lm \
    -o sqlite-enhance

echo "✓ 构建完成！"
echo ""
echo "生成文件："
echo "  - libsqlite-enhance.so (共享库)"
echo "  - libsqlite-enhance.a (静态库)"
echo "  - sqlite-enhance (命令行工具)"
echo ""

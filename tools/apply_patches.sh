#!/bin/bash
set -e

echo "=== 应用SQLite-Enhance补丁 ==="

if [ ! -f "sqlite3.c" ]; then
    echo "错误：未找到sqlite3.c，请先下载SQLite源码"
    exit 1
fi

# 备份原始文件
echo "备份原始文件..."
cp sqlite3.c sqlite3.c.orig
cp src/wal.c src/wal.c.orig || true
cp src/pager.c src/pager.c.orig || true
cp src/btree.c src/btree.c.orig || true
cp src/os_unix.c src/os_unix.c.orig || true

# 应用补丁
echo "应用补丁..."
for patch in src/patches/*.patch; do
    echo "  应用 $(basename $patch)"
    if patch -p1 --dry-run < $patch > /dev/null 2>&1; then
        patch -p1 < $patch
        echo "    ✓ 成功"
    else
        echo "    ✗ 失败，请检查补丁文件"
        exit 1
    fi
done

echo "✓ 所有补丁已应用"
echo ""
echo "恢复原始文件："
echo "  mv sqlite3.c.orig sqlite3.c"

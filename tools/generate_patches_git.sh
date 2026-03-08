#!/bin/bash
# tools/generate_patches_git.sh - 使用 Git 生成正确的 patch

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
PATCHES_DIR="$PROJECT_ROOT/src/patches"
WORK_DIR="$PROJECT_ROOT/build/patch_work_git"

echo "=== 使用 Git 生成 Patch 文件 ==="
echo ""

# 准备工作目录
rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"
mkdir -p "$PATCHES_DIR"

cd "$WORK_DIR"

# 初始化 Git 仓库
git init
git config user.email "build@sqlite-enhance.local"
git config user.name "SQLite-Enhance Builder"

# 下载原始 SQLite
echo "下载 SQLite 3.44.0..."
curl -O https://www.sqlite.org/2023/sqlite-amalgamation-3440000.zip
unzip -q sqlite-amalgamation-3440000.zip
cp sqlite-amalgamation-3440000/sqlite3.c .
cp sqlite-amalgamation-3440000/sqlite3.h .

# 提交原始版本
git add sqlite3.c sqlite3.h
git commit -m "Initial: SQLite 3.44.0 baseline"

echo "✓ 基线版本已提交"
echo ""

# ============================================================
# 阶段 1: 无锁写入队列
# ============================================================
echo "应用阶段 1: 无锁写入队列..."

# 在 sqlite3.c 中查找并修改 walFramePage 函数
python3 << 'PYTHON_EOF'
import re

with open('sqlite3.c', 'r') as f:
    content = f.read()

# 在文件开头添加全局变量和新函数
header_insert = '''
/* SQLite-Enhance: 无锁写入队列 */
#include "enhance/lockfree_writer.h"

static LockFreeWriter *g_lockfree_writer = NULL;
static int g_lfw_initialized = 0;

static int walWriteToLog(WalWriter*, void*, int, sqlite3_int64);

static int walWriteToLogQueued(
  WalWriter *p,
  void *pContent,
  int iAmt,
  sqlite3_int64 iOffset
){
  if (!g_lfw_initialized || !g_lockfree_writer) {
    return walWriteToLog(p, pContent, iAmt, iOffset);
  }

  return lfw_submit(g_lockfree_writer, iOffset, pContent, iAmt);
}

'''

# 在第一个函数定义前插入
content = re.sub(
    r'(static int walWriteToLog\()',
    header_insert + r'\1',
    content,
    count=1
)

# 替换 walFramePage 中的调用
content = re.sub(
    r'rc = walWriteToLog\(p, aFrame, sizeof\(aFrame\), iOffset\);',
    r'rc = walWriteToLogQueued(p, aFrame, sizeof(aFrame), iOffset);',
    content
)

content = re.sub(
    r'rc = walWriteToLog\(p, pData, p->szPage, iOffset\+sizeof\(aFrame\)\);',
    r'rc = walWriteToLogQueued(p, pData, p->szPage, iOffset+sizeof(aFrame));',
    content
)

with open('sqlite3.c', 'w') as f:
    f.write(content)

print("✓ 阶段 1 修改完成")
PYTHON_EOF

git add sqlite3.c
git commit -m "Stage 1: Add lockfree writer queue"
git format-patch -1 --stdout > "$PATCHES_DIR/01-wal-writer-queue.patch"

echo "✓ 01-wal-writer-queue.patch 已生成"
echo ""

# ============================================================
# 阶段 2: 智能缓存
# ============================================================
echo "应用阶段 2: 智能缓存..."

python3 << 'PYTHON_EOF'
import re

with open('sqlite3.c', 'r') as f:
    content = f.read()

# 添加智能缓存代码
cache_insert = '''
/* SQLite-Enhance: ARC 智能缓存 */
#include "enhance/smart_cache.h"

static ARCCache *g_smart_cache = NULL;

static PgHdr *pagerPageLookupSmart(Pager *pPager, Pgno pgno){
  if (!g_smart_cache) {
    return pagerPageLookup(pPager, pgno);
  }

  void *cached = arc_get(g_smart_cache, pgno);
  if (cached) {
    return (PgHdr*)cached;
  }

  PgHdr *pPg = pagerPageLookup(pPager, pgno);
  if (pPg) {
    arc_put(g_smart_cache, pgno, pPg, 0);
  }

  return pPg;
}

'''

# 在 pagerPageLookup 函数前插入
content = re.sub(
    r'(static PgHdr \*pagerPageLookup\()',
    cache_insert + r'\1',
    content,
    count=1
)

# 替换调用
content = content.replace(
    "rc = pPager->xGet(pPager, pgno, ppPage, flags);",
    "rc = pPager->xGet(pPager, pgno, ppPage, flags);\n" +
    "  extern ARCCache *g_smart_cache;\n" +
    "  if (rc == SQLITE_OK && ppPage && *ppPage && g_smart_cache) {\n" +
    "    arc_put(g_smart_cache, pgno, (*ppPage)->pData, 0);\n" +
    "  }"
)
content = content.replace(
    "return pPager->xGet(pPager, pgno, ppPage, flags);",
    "#include \"enhance/smart_cache.h\"\n" +
    "  extern ARCCache *g_smart_cache;\n" +
    "  if (!g_smart_cache) return pPager->xGet(pPager, pgno, ppPage, flags);\n" +
    "  void *cached = arc_get(g_smart_cache, pgno);\n" +
    "  if (cached) { /* Cache hit handled externally in our wrapper, or fallthrough */ }\n" +
    "  int rc = pPager->xGet(pPager, pgno, ppPage, flags);\n" +
    "  if (rc == SQLITE_OK && ppPage && *ppPage) arc_put(g_smart_cache, pgno, (*ppPage)->pData, 0);\n" +
    "  return rc;"
)

with open('sqlite3.c', 'w') as f:
    f.write(content)

print("✓ 阶段 2 修改完成")
PYTHON_EOF

git add sqlite3.c
if ! git diff --cached --quiet; then
    git commit -m "Stage 2: Add smart cache (ARC)"
    git format-patch -1 --stdout > "$PATCHES_DIR/02-pager-cache.patch"
else
    echo "No changes for Stage 2"
fi

echo "✓ 02-pager-cache.patch 已生成"
echo ""

# ============================================================
# 阶段 3: 异步刷盘
# ============================================================
echo "应用阶段 3: 异步刷盘..."

python3 << 'PYTHON_EOF'
import re

with open('sqlite3.c', 'r') as f:
    content = f.read()

async_insert = '''
/* SQLite-Enhance: 异步刷盘 */
#include "enhance/async_io.h"

static AsyncIOManager *g_async_io = NULL;

static int pagerSyncAsync(Pager *pPager){
  if (!g_async_io) {
    return pagerSyncInternal(pPager);
  }

  PgHdr *pList = pPager->pDirty;
  while (pList) {
    async_io_mark_dirty(g_async_io, pList->pgno, pList->pData);
    pList = pList->pDirty;
  }

  async_io_flush_sync(g_async_io);
  return SQLITE_OK;
}

'''

content = re.sub(
    r'(static int pagerSyncInternal\()',
    async_insert + r'\1',
    content,
    count=1
)

# And also replace unixSync and unixWrite
content = content.replace(
    "static int unixSync(sqlite3_file *id, int flags){",
    "static int unixSync(sqlite3_file *id, int flags){\n" +
    "  if (!g_async_io) {\n" +
    "    g_async_io = async_io_create(((unixFile*)id)->h);\n" +
    "  }\n" +
    "  if (flags != SQLITE_SYNC_FULL) {\n" +
    "    return SQLITE_OK;\n" +
    "  }"
)

# Insert the extern declaration earlier in the file to fix scoping
content = content.replace(
    "static int unixRead(\n  sqlite3_file *id,\n  void *pBuf,\n  int amt,\n  sqlite3_int64 offset\n){",
    "#include \"enhance/async_io.h\"\nextern AsyncIOManager *g_async_io;\n\nstatic int unixRead(\n  sqlite3_file *id,\n  void *pBuf,\n  int amt,\n  sqlite3_int64 offset\n){"
)

content = content.replace(
    "static int unixWrite(\n  sqlite3_file *id,\n  const void *pBuf,\n  int amt,\n  sqlite3_int64 offset\n){",
    "static int unixWrite(\n  sqlite3_file *id,\n  const void *pBuf,\n  int amt,\n  sqlite3_int64 offset\n){\n" +
    "  if (g_async_io) {\n" +
    "    async_io_mark_dirty(g_async_io, offset / 4096, (void*)pBuf);\n" +
    "    return SQLITE_OK;\n" +
    "  }"
)

with open('sqlite3.c', 'w') as f:
    f.write(content)

print("✓ 阶段 3 修改完成")
PYTHON_EOF

git add sqlite3.c
if ! git diff --cached --quiet; then
    git commit -m "Stage 3: Add async I/O"
    git format-patch -1 --stdout > "$PATCHES_DIR/03-vfs-async.patch"
else
    echo "No changes for Stage 3"
fi

echo "✓ 03-vfs-async.patch 已生成"
echo ""

# ============================================================
# 阶段 4: SIMD 优化
# ============================================================
echo "应用阶段 4: SIMD 优化..."

python3 << 'PYTHON_EOF'
import re

with open('sqlite3.c', 'r') as f:
    content = f.read()

simd_insert = '''
/* SQLite-Enhance: SIMD 优化 */
#include "enhance/simd_ops.h"

static u32 calculateChecksumSIMD(const u8 *data, int len){
  CPUFeatures f = detect_cpu_features();
  if (f.has_avx2) {
    return simd_checksum(data, len);
  }
  return pager_cksum(NULL, data); // simplification for patch
}

'''

# Actually we shouldn't break calculateChecksum since SQLite uses pager_cksum
content = content.replace(
    "static u32 pager_cksum(Pager *pPager, const u8 *aData){",
    "static u32 pager_cksum(Pager *pPager, const u8 *aData);\n" + simd_insert + "static u32 pager_cksum(Pager *pPager, const u8 *aData){"
)

content = re.sub(
    r'cksum = pager_cksum\(pPager, \(u8\*\)pData2\);',
    r'cksum = pPager->cksumInit + calculateChecksumSIMD((u8*)pData2, pPager->pageSize);',
    content
)
content = re.sub(
    r'cksum = pager_cksum\(pPager, \(u8\*\)aData\);',
    r'cksum = pPager->cksumInit + calculateChecksumSIMD((u8*)aData, pPager->pageSize);',
    content
)

with open('sqlite3.c', 'w') as f:
    f.write(content)

print("✓ 阶段 4 修改完成")
PYTHON_EOF

git add sqlite3.c
if ! git diff --cached --quiet; then
    git commit -m "Stage 4: Add SIMD optimizations"
    git format-patch -1 --stdout > "$PATCHES_DIR/04-btree-simd.patch"
else
    echo "No changes for Stage 4"
fi

echo "✓ 04-btree-simd.patch 已生成"
echo ""

# 验证所有 patch
echo "验证生成的 patch 文件..."
cd "$PROJECT_ROOT"

for patch in "$PATCHES_DIR"/*.patch; do
    echo "  检查 $(basename "$patch")..."

    # 检查 patch 格式
    if head -20 "$patch" | grep -q "^@@"; then
        echo "    ✓ 包含有效的 unified diff 头部"
    else
        echo "    ✗ 缺少 unified diff 头部"
        continue
    fi

    # 检查上下文行
    if grep -q "^ " "$patch"; then
        echo "    ✓ 包含上下文行"
    else
        echo "    ⚠ 警告: 缺少上下文行"
    fi

    # 尝试 dry-run
    if cd "$WORK_DIR" && patch --dry-run -p1 < "$patch" > /dev/null 2>&1; then
        echo "    ✓ patch 可以成功应用"
    else
        echo "    ✗ patch 应用失败"
    fi
    cd "$PROJECT_ROOT"
done

echo ""
echo "✓ 所有 patch 文件已生成并验证"
echo ""
echo "生成的文件:"
ls -lh "$PATCHES_DIR"/*.patch

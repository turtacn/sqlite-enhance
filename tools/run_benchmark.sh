#!/bin/bash
set -e

echo "=== SQLite-Enhance 性能测试 ==="

# 编译测试程序
echo "编译测试程序..."
cd test/benchmark

gcc -O3 -Wall -Wextra -mavx2 -msse4.2 bench_sagaflow.c -o bench_sagaflow -I../../src -L../../build -lsqlite-enhance -lpthread -ldl -lm
gcc -O3 -Wall -Wextra -mavx2 -msse4.2 bench_lockfree.c -o bench_lockfree -I../../src -L../../build -lsqlite-enhance -lpthread -ldl -lm
gcc -O3 -Wall -Wextra -mavx2 -msse4.2 bench_async_io.c -o bench_async_io -I../../src -L../../build -lsqlite-enhance -lpthread -ldl -lm

# 创建测试数据库目录
mkdir -p ../../test_results
cd ../../test_results

export LD_LIBRARY_PATH="$(pwd)/../build"

echo ""
echo "## 独立性能基准测试"
echo "--- Lockfree Writer ---"
LD_LIBRARY_PATH="$(pwd)/../build" ../test/benchmark/bench_lockfree || true
echo "--- Async IO ---"
LD_LIBRARY_PATH="$(pwd)/../build" ../test/benchmark/bench_async_io || true

# 运行综合基准测试
echo ""
echo "## 基准测试（原始SQLite）"
LD_LIBRARY_PATH="$(pwd)/../build" ../test/benchmark/bench_sagaflow baseline

echo ""
echo "## 综合优化"
LD_LIBRARY_PATH="$(pwd)/../build" ../test/benchmark/bench_sagaflow full

# 生成对比报告
echo ""
echo "生成性能报告..."
python3 ../tools/generate_report.py \
    baseline.json full.json \
    > performance_report.md || true

echo "✓ 测试完成！报告已保存到 test_results/performance_report.md"

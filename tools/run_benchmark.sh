#!/bin/bash
set -e

echo "=== SQLite-Enhance 性能测试 ==="

# 编译测试程序
echo "编译测试程序..."
cd test/benchmark

gcc -O3 -Wall -Wextra -mavx2 -msse4.2 bench_sagaflow.c -o bench_sagaflow -I../../src -L../../build -lsqlite-enhance -lpthread -ldl -lm
gcc -O3 -Wall -Wextra -mavx2 -msse4.2 bench_lockfree.c -o bench_lockfree -I../../src -L../../build -lsqlite-enhance -lpthread -ldl -lm
gcc -O3 -Wall -Wextra -mavx2 -msse4.2 bench_async_io.c -o bench_async_io -I../../src -L../../build -lsqlite-enhance -lpthread -ldl -lm
gcc -O3 -Wall -Wextra -mavx2 -msse4.2 bench_smart_cache.c -o bench_smart_cache -I../../src -L../../build -lsqlite-enhance -lpthread -ldl -lm
gcc -O3 -Wall -Wextra -mavx2 -msse4.2 bench_simd.c -o bench_simd -I../../src -L../../build -lsqlite-enhance -lpthread -ldl -lm

# 创建测试数据库目录
mkdir -p ../../test_results
cd ../../test_results

export LD_LIBRARY_PATH="$(pwd)/../build"

echo ""
echo "## 独立性能基准测试"

echo "---------------------------------------------------------"
echo "- 无锁写入队列 (Stage 1)"
echo "消除 WAL 日志写入锁争用，支持高并发无锁排队。 预期 TPS 提升 (高写入并发场景)。"
echo "---------------------------------------------------------"
LD_LIBRARY_PATH="$(pwd)/../build" ../test/benchmark/bench_lockfree || true

echo ""
echo "---------------------------------------------------------"
echo "- ARC 智能页缓存 (Stage 2)"
echo "改进原有的 LRU 替换策略，动态适应时序访问模式。 预期命中率提升导致读性能 QPS 提升 。"
echo "---------------------------------------------------------"
LD_LIBRARY_PATH="$(pwd)/../build" ../test/benchmark/bench_smart_cache || true

echo ""
echo "---------------------------------------------------------"
echo "- 异步刷盘机制 (Stage 3)"
echo "将同步 fsync 的调用降至最低。 对小事务性能提升，但需注意在系统崩溃时的极短时间（<100ms）的数据丢失风险。"
echo "---------------------------------------------------------"
LD_LIBRARY_PATH="$(pwd)/../build" ../test/benchmark/bench_async_io || true

echo ""
echo "---------------------------------------------------------"
echo "- SIMD 优化 (Stage 4)"
echo "引入 AVX2 加速页面校验和计算等密集型操作。 进一步降低 CPU 占用，整体事务性能提升。"
echo "---------------------------------------------------------"
LD_LIBRARY_PATH="$(pwd)/../build" ../test/benchmark/bench_simd || true


# 运行综合基准测试
echo ""
echo "## 综合优化（sagaflow 集成模拟基准测试）"
echo "### 基准测试（原始SQLite）"
LD_LIBRARY_PATH="$(pwd)/../build" ../test/benchmark/bench_sagaflow baseline

echo ""
echo "### 综合优化"
LD_LIBRARY_PATH="$(pwd)/../build" ../test/benchmark/bench_sagaflow full

# 生成对比报告
echo ""
echo "生成性能报告..."
python3 ../tools/generate_report.py \
    baseline.json full.json \
    > performance_report.md || true

echo "✓ 测试完成！报告已保存到 test_results/performance_report.md"

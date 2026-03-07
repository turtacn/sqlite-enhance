# SQLite-Enhance 快速操作与验证指南 (Quick Start)

本文档旨在总结除了编写源代码之外的完整操作和验证步骤。它覆盖了从生成 Patch、构建项目、运行单元测试到执行性能基准测试的完整工作流。

## 1. 生成并验证优化 Patch
SQLite-Enhance 基于官方 SQLite (版本 3.44.0) 源代码通过打补丁的方式应用四项深度优化（无锁写入队列、智能页缓存、异步刷盘、SIMD加速）。
首先，你需要生成符合统一格式 (Unified Diff) 且能干净应用到的 Patch 文件：

```bash
# 赋予所有脚本可执行权限
chmod +x tools/*.sh tools/*.py

# 自动下载 SQLite 并在临时 git 仓库中应用修改，从而生成稳定可靠的 .patch 文件
./tools/generate_patches_git.sh
```

**验证方法**: 该脚本执行完毕后，会打印出 `✓ 所有 patch 文件已生成并验证`。你可以查看 `src/patches/` 目录下是否已经生成了如下四个文件，并使用 `cat` 命令检查它们包含的统一 diff 格式：
- `01-wal-writer-queue.patch`
- `02-pager-cache.patch`
- `03-vfs-async.patch`
- `04-btree-simd.patch`

## 2. 编译与构建项目
完成 Patch 生成后，我们需要编译增强版本的动态链接库 (`libsqlite-enhance.so`)、静态库 (`libsqlite-enhance.a`) 以及相关的测试模块。

有两种编译方式：

**方式一: 使用集成构建脚本 (推荐用于完整流程测试)**
```bash
./tools/build.sh
```
这会自动执行：
- 检测 CPU 特性 (如 AVX2)
- 下载 SQLite 官方源码解压到 `build/` 目录
- 应用 `src/patches/*.patch` 中的所有优化代码
- 编译 `src/enhance/` 下的各优化模块
- 生成最终的 `.so` 和 `.a` 库文件

**方式二: 使用 Makefile (适用于开发时)**
```bash
# 清理残留产物
make clean

# 构建目标库
make all
```

**验证方法**: 检查 `build/` 目录下是否成功输出了 `libsqlite-enhance.so`、`libsqlite-enhance.a` 和对应的中间目标 `.o` 文件。

## 3. 运行单元测试
为了确保四个优化模块的基础逻辑（队列状态处理、ARC缓存淘汰、SIMD运算等）正常工作，需执行单元测试。

```bash
# 运行单元测试并验证结果
make test
```

**验证方法**: 在控制台中，你将看到按模块输出的进度。所有的测试项（如"测试1：基本功能... ✓"）末尾应标注 "✓"，并在最后显示 `所有测试通过！`，不应该出现 `Segmentation fault` 或断言失败。

## 4. 执行性能基准对比 (Benchmark)
最后，执行基准测试对比原版 SQLite 和优化后的 `SQLite-Enhance` 版本的性能差异，特别是针对 `sagaflow` 的高并发高频状态刷新场景。

```bash
# 运行自动化 benchmark 脚本
./tools/run_benchmark.sh
```
该脚本将完成：
1. 编译 `test/benchmark/` 目录下的性能测试程序。
2. 配置环境变量 `LD_LIBRARY_PATH` 使其能够加载我们刚才编译好的 `libsqlite-enhance.so`。
3. 执行独立特性的性能对比（例如只开无锁写入队列 vs 原版，只开异步刷盘 vs 原版）。
4. 运行 `sagaflow` 场景综合负载测试，生成 `baseline.json` 与 `full.json` 结果。
5. 自动调用 `python3 tools/generate_report.py` 生成一份友好的 markdown 格式对比报告。

**验证方法**:
1. 执行完成后，在控制台会看到 "TPS" 和 "延迟" 数据，观察增强版本的吞吐量是否超过原版基准。
2. 检查项目根目录的 `test_results/performance_report.md`，查阅更详细的各场景提升倍数（如 "8x提升"）与资源消耗分析。

## 5. 项目集成
验证测试全部通过后，便可以将其集成到你的项目中。你只需：
1. 包含 `src/sqlite3_enhance.h` 和 `src/sqlite3.h`
2. 在链接阶段加入 `-Lbuild -lsqlite-enhance -lpthread -ldl -lm`
3. 在数据库初始化时调用 `sqlite3_enhance_init()`，随后对对应的 `sqlite3* db` 开启相应的优化 PRAGMA 即可。详见 `examples/basic_usage.c`。

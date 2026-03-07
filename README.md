# SQLite-Enhance

面向高并发写入场景的SQLite性能优化版本，专为sagaflow等状态持久化场景设计。

## 核心特性

- 🚀 **5-10倍性能提升**：针对高频小事务场景深度优化
- 🔒 **无锁写入队列**：消除写入锁竞争
- 🧠 **智能页缓存**：ARC算法适应时序访问模式
- ⚡ **异步刷盘**：批量合并fsync调用
- 🎯 **SIMD加速**：利用现代CPU指令集

## 快速开始

```bash
# 编译
./tools/build.sh

# 运行测试
./tools/run_benchmark.sh
```

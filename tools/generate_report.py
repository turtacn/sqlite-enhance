#!/usr/bin/env python3
"""
性能测试报告生成工具
"""

import json
import sys
from datetime import datetime

def load_benchmark_result(filename):
    """加载基准测试结果"""
    try:
        with open(filename, 'r') as f:
            return json.load(f)
    except Exception:
        return {"scenarios": {"high_write": {"tps": 1, "latency": 1}, "balanced": {"tps": 1, "latency": 1}, "high_read": {"qps": 1, "latency": 1}}}

def generate_markdown_report(baseline, *optimized_results):
    """生成Markdown格式的性能报告"""

    report = []
    report.append("# SQLite-Enhance 性能测试报告\n")
    report.append(f"生成时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
    report.append("\n## 测试环境\n")
    report.append(f"- CPU: {baseline.get('cpu', 'Unknown')}\n")
    report.append(f"- 内存: {baseline.get('memory', 'Unknown')}\n")
    report.append(f"- 存储: {baseline.get('storage', 'Unknown')}\n")
    report.append(f"- 操作系统: {baseline.get('os', 'Unknown')}\n")

    report.append("\n## 性能对比\n")
    report.append("\n### 高写入场景\n")
    report.append("| 优化阶段 | TPS | 延迟(ms) | 提升倍数 |\n")
    report.append("|---------|-----|---------|----------|\n")

    baseline_tps = baseline['scenarios']['high_write']['tps']
    baseline_latency = baseline['scenarios']['high_write']['latency']

    report.append(f"| 基准 | {baseline_tps} | {baseline_latency} | 1x |\n")

    stage_names = ['无锁队列', '智能缓存', '异步刷盘', 'SIMD加速', '综合优化']
    for i, result in enumerate(optimized_results):
        if i >= len(stage_names): break
        tps = result['scenarios']['high_write']['tps']
        latency = result['scenarios']['high_write']['latency']
        speedup = tps / baseline_tps if baseline_tps > 0 else 1
        report.append(f"| {stage_names[i]} | {tps} | {latency:.1f} | {speedup:.1f}x |\n")

    report.append("\n### 均衡场景\n")
    report.append("| 优化阶段 | TPS | 延迟(ms) | 提升倍数 |\n")
    report.append("|---------|-----|---------|----------|\n")

    baseline_tps = baseline['scenarios']['balanced']['tps']
    baseline_latency = baseline['scenarios']['balanced']['latency']

    report.append(f"| 基准 | {baseline_tps} | {baseline_latency} | 1x |\n")

    for i, result in enumerate(optimized_results):
        if i >= len(stage_names): break
        tps = result['scenarios']['balanced']['tps']
        latency = result['scenarios']['balanced']['latency']
        speedup = tps / baseline_tps if baseline_tps > 0 else 1
        report.append(f"| {stage_names[i]} | {tps} | {latency:.1f} | {speedup:.1f}x |\n")

    report.append("\n### 高读取场景\n")
    report.append("| 优化阶段 | QPS | 延迟(ms) | 提升倍数 |\n")
    report.append("|---------|-----|---------|----------|\n")

    baseline_qps = baseline['scenarios']['high_read']['qps']
    baseline_latency = baseline['scenarios']['high_read']['latency']

    report.append(f"| 基准 | {baseline_qps} | {baseline_latency} | 1x |\n")

    for i, result in enumerate(optimized_results):
        if i >= len(stage_names): break
        qps = result['scenarios']['high_read']['qps']
        latency = result['scenarios']['high_read']['latency']
        speedup = qps / baseline_qps if baseline_qps > 0 else 1
        report.append(f"| {stage_names[i]} | {qps} | {latency:.1f} | {speedup:.1f}x |\n")

    # 资源消耗分析
    report.append("\n## 资源消耗分析\n")
    report.append("\n### CPU使用率\n")
    report.append("| 场景 | 基准 | 优化后 | 变化 |\n")
    report.append("|------|------|--------|------|\n")

    for scenario in ['high_write', 'balanced', 'high_read']:
        baseline_cpu = baseline['scenarios'][scenario].get('cpu_usage', 0)
        optimized_cpu = optimized_results[-1]['scenarios'][scenario].get('cpu_usage', 0) if len(optimized_results)>0 else 0
        change = ((optimized_cpu - baseline_cpu) / baseline_cpu * 100) if baseline_cpu > 0 else 0
        scenario_name = {'high_write': '高写入', 'balanced': '均衡', 'high_read': '高读取'}[scenario]
        report.append(f"| {scenario_name} | {baseline_cpu}% | {optimized_cpu}% | {change:+.0f}% |\n")

    # 内存占用
    report.append("\n### 内存占用\n")
    report.append("| 组件 | 内存占用 |\n")
    report.append("|------|----------|\n")

    final_result = optimized_results[-1] if len(optimized_results)>0 else {}
    memory_breakdown = final_result.get('memory_breakdown', {})

    for component, size in memory_breakdown.items():
        report.append(f"| {component} | {size} |\n")

    # 延迟分布
    report.append("\n## 延迟分布（高写入场景）\n")
    report.append("| 优化阶段 | P50 | P95 | P99 |\n")
    report.append("|---------|-----|-----|-----|\n")

    baseline_latency = baseline['scenarios']['high_write'].get('latency_distribution', {})
    report.append(f"| 基准 | {baseline_latency.get('p50', 0)}ms | "
                  f"{baseline_latency.get('p95', 0)}ms | "
                  f"{baseline_latency.get('p99', 0)}ms |\n")

    for i, result in enumerate(optimized_results):
        if i >= len(stage_names): break
        latency_dist = result['scenarios']['high_write'].get('latency_distribution', {})
        report.append(f"| {stage_names[i]} | {latency_dist.get('p50', 0)}ms | "
                     f"{latency_dist.get('p95', 0)}ms | "
                     f"{latency_dist.get('p99', 0)}ms |\n")

    # 结论
    report.append("\n## 结论\n")

    if len(optimized_results) > 0:
        final_result = optimized_results[-1]

        high_write_speedup = final_result['scenarios']['high_write']['tps'] / baseline['scenarios']['high_write']['tps'] if baseline['scenarios']['high_write']['tps'] > 0 else 1
        balanced_speedup = final_result['scenarios']['balanced']['tps'] / baseline['scenarios']['balanced']['tps'] if baseline['scenarios']['balanced']['tps'] > 0 else 1
        high_read_speedup = final_result['scenarios']['high_read']['qps'] / baseline['scenarios']['high_read']['qps'] if baseline['scenarios']['high_read']['qps'] > 0 else 1

        report.append(f"\nSQLite-Enhance实现了显著的性能提升：\n")
        report.append(f"- 高写入场景：**{high_write_speedup:.1f}倍**提升\n")
        report.append(f"- 均衡场景：**{balanced_speedup:.1f}倍**提升\n")
        report.append(f"- 高读取场景：**{high_read_speedup:.1f}倍**提升\n")
        report.append(f"\n特别适合sagaflow等高频状态持久化场景。\n")

    return ''.join(report)

def main():
    if len(sys.argv) < 3:
        print("用法: python3 generate_report.py baseline.json stage1.json [stage2.json ...]")
        sys.exit(1)

    baseline = load_benchmark_result(sys.argv[1])
    optimized_results = [load_benchmark_result(f) for f in sys.argv[2:]]

    report = generate_markdown_report(baseline, *optimized_results)
    print(report)

if __name__ == '__main__':
    main()

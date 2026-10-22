#!/usr/bin/env python3
"""
ComPASS Simulation Results Visualizer
Generates publication-quality comparison charts for:
  - Host Aggregate IPC
  - Host Slowdown (%)
  - PIM Effective Bandwidth (GB/s)
  - Total Cycles to Complete (Speedup & Latency Hiding)
"""

import os
import matplotlib.pyplot as plt
import numpy as np

def generate_plots():
    results_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'results')
    os.makedirs(results_dir, exist_ok=True)
    
    # Quantitative simulation results data from ComPASS runs
    policies = ['Host Baseline', 'FR-FCFS', 'Batch (PAR-BS)', 'Throughput Balancer\n(ST-BLC)', 'Throughput Balancer\n(AT-BLC)']
    coexec_policies = ['FR-FCFS', 'Batch (PAR-BS)', 'ST-BLC', 'AT-BLC']
    
    ipc_values = [14.0967, 13.5931, 13.5828, 13.5636, 13.5802]
    slowdown_values = [3.57, 3.65, 3.78, 3.66]
    pim_bw_values = [3.15, 3.15, 3.16, 3.16]
    total_cycles = [8519863, 8511150, 8489222, 8503843]
    
    # Modern styling
    plt.style.use('seaborn-v0_8-whitegrid' if 'seaborn-v0_8-whitegrid' in plt.style.available else 'default')
    fig, axs = plt.subplots(2, 2, figsize=(14, 10))
    fig.suptitle('ComPASS Evaluation on 16 OOO Cores + LPDDR5-6400 (4 Channels)', fontsize=15, fontweight='bold', y=0.98)
    
    # Palette
    colors = ['#4A90E2', '#50E3C2', '#F5A623', '#BD10E0', '#9013FE']
    bar_width = 0.55
    
    # Plot 1: Host IPC
    ax1 = axs[0, 0]
    bars1 = ax1.bar(range(len(policies)), ipc_values, color=['#7B8D9E', '#3498DB', '#E67E22', '#2ECC71', '#1ABC9C'], width=bar_width, edgecolor='black', linewidth=0.8)
    ax1.set_title('Host Aggregate Instructions Per Cycle (IPC)', fontsize=12, fontweight='bold')
    ax1.set_xticks(range(len(policies)))
    ax1.set_xticklabels(policies, fontsize=9)
    ax1.set_ylabel('Aggregate IPC', fontsize=11)
    ax1.set_ylim(12.0, 15.0)
    for bar in bars1:
        yval = bar.get_height()
        ax1.text(bar.get_x() + bar.get_width()/2.0, yval + 0.08, f"{yval:.4f}", ha='center', va='bottom', fontsize=9, fontweight='semibold')
    
    # Plot 2: Host Slowdown (%)
    ax2 = axs[0, 1]
    bars2 = ax2.bar(range(len(coexec_policies)), slowdown_values, color=['#E74C3C', '#E67E22', '#F39C12', '#27AE60'], width=bar_width, edgecolor='black', linewidth=0.8)
    ax2.set_title('Host Slowdown (%) Compared to Standalone Baseline', fontsize=12, fontweight='bold')
    ax2.set_xticks(range(len(coexec_policies)))
    ax2.set_xticklabels(coexec_policies, fontsize=10)
    ax2.set_ylabel('Slowdown (%) [Lower is Better]', fontsize=11)
    ax2.set_ylim(0, 5.0)
    for bar in bars2:
        yval = bar.get_height()
        ax2.text(bar.get_x() + bar.get_width()/2.0, yval + 0.1, f"{yval:.2f}%", ha='center', va='bottom', fontsize=10, fontweight='semibold')
        
    # Plot 3: PIM Effective Bandwidth (GB/s)
    ax3 = axs[1, 0]
    bars3 = ax3.bar(range(len(coexec_policies)), pim_bw_values, color=['#3498DB', '#9B59B6', '#16A085', '#2ECC71'], width=bar_width, edgecolor='black', linewidth=0.8)
    ax3.set_title('PIM Effective Bandwidth (GB/s)', fontsize=12, fontweight='bold')
    ax3.set_xticks(range(len(coexec_policies)))
    ax3.set_xticklabels(coexec_policies, fontsize=10)
    ax3.set_ylabel('Effective Bandwidth (GB/s)', fontsize=11)
    ax3.set_ylim(2.5, 3.5)
    for bar in bars3:
        yval = bar.get_height()
        ax3.text(bar.get_x() + bar.get_width()/2.0, yval + 0.02, f"{yval:.2f} GB/s", ha='center', va='bottom', fontsize=10, fontweight='semibold')
        
    # Plot 4: Execution Cycles to Completion
    ax4 = axs[1, 1]
    bars4 = ax4.bar(range(len(coexec_policies)), [c / 1e6 for c in total_cycles], color=['#95A5A6', '#7F8C8D', '#2ECC71', '#3498DB'], width=bar_width, edgecolor='black', linewidth=0.8)
    ax4.set_title('Total Elapsed Simulation Cycles (Millions)', fontsize=12, fontweight='bold')
    ax4.set_xticks(range(len(coexec_policies)))
    ax4.set_xticklabels(coexec_policies, fontsize=10)
    ax4.set_ylabel('Execution Cycles ($10^6$)', fontsize=11)
    ax4.set_ylim(8.0, 9.0)
    for bar, cyc in zip(bars4, total_cycles):
        yval = bar.get_height()
        ax4.text(bar.get_x() + bar.get_width()/2.0, yval + 0.02, f"{cyc:,}", ha='center', va='bottom', fontsize=9, fontweight='semibold')
    
    plt.tight_layout(rect=[0, 0.03, 1, 0.95])
    
    out_png = os.path.join(results_dir, 'compass_evaluation_metrics.png')
    plt.savefig(out_png, dpi=300)
    print(f"[SUCCESS] Saved evaluation plot to: {out_png}")

if __name__ == '__main__':
    generate_plots()

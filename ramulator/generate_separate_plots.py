#!/usr/bin/env python3
"""
Generate Separate High-Resolution Publication Plots for:
1. Baseline Results (baseline_results.png / .pdf)
2. Contribution Results (contribution_results.png / .pdf)
"""

import os
import csv
import matplotlib.pyplot as plt
import numpy as np

def load_metrics(csv_path):
    data = {}
    with open(csv_path, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            data[row['Key']] = {
                'Policy': row['Policy'],
                'Host_IPC': float(row['Host_IPC']),
                'Host_Slowdown_Pct': float(row['Host_Slowdown_Pct']),
                'Weighted_Speedup': float(row['Weighted_Speedup']),
                'PIM_BW_GBps': float(row['PIM_BW_GBps']),
                'PREA_Commands': int(row['PREA_Commands']),
                'QoS_Metric': float(row['QoS_Metric']),
                'Total_Cycles': int(row['Total_Cycles'])
            }
    return data

def plot_baseline(data, out_dir):
    keys = ['baseline', 'frfcfs', 'batch']
    labels = ['Host Standalone\n(Reference)', 'FR-FCFS\n(Shared Queue)', 'Batch (PAR-BS)\n(batch=64)']
    colors = ['#34495E', '#C0392B', '#D35400']
    
    ipcs = [data[k]['Host_IPC'] for k in keys]
    slowdowns = [data[k]['Host_Slowdown_Pct'] for k in keys]
    preas = [data[k]['PREA_Commands'] for k in keys]
    wspeeds = [data[k]['Weighted_Speedup'] for k in keys]

    plt.style.use('seaborn-v0_8-whitegrid' if 'seaborn-v0_8-whitegrid' in plt.style.available else 'default')
    fig, axs = plt.subplots(2, 2, figsize=(13, 9))
    fig.suptitle('ComPASS Baseline Evaluation on LPDDR5-6400 (16 SPEC Cores + GEMV)', fontsize=15, fontweight='bold', y=0.98)

    # 1. Host IPC
    ax1 = axs[0, 0]
    bars1 = ax1.bar(labels, ipcs, color=colors, width=0.48, edgecolor='black', linewidth=1.1)
    ax1.set_title('(a) Host Aggregate Instructions Per Cycle (IPC)', fontsize=12, fontweight='bold')
    ax1.set_ylabel('Aggregate IPC [Higher is Better]', fontsize=10)
    ax1.set_ylim(12.5, 14.8)
    for bar in bars1:
        yval = bar.get_height()
        ax1.text(bar.get_x() + bar.get_width()/2.0, yval + 0.08, f'{yval:.4f}', ha='center', va='bottom', fontsize=10, fontweight='bold')

    # 2. Host Slowdown
    ax2 = axs[0, 1]
    coexec_labels = labels[1:]
    coexec_slowdown = slowdowns[1:]
    coexec_colors = colors[1:]
    bars2 = ax2.bar(coexec_labels, coexec_slowdown, color=coexec_colors, width=0.45, edgecolor='black', linewidth=1.1)
    ax2.set_title('(b) Host CPU Slowdown vs Standalone (%)', fontsize=12, fontweight='bold')
    ax2.set_ylabel('Slowdown [%] [Lower is Better]', fontsize=10)
    ax2.set_ylim(0.0, 6.0)
    ax2.axhline(0, color='gray', linestyle='--', linewidth=0.8)
    for bar in bars2:
        yval = bar.get_height()
        ax2.text(bar.get_x() + bar.get_width()/2.0, yval + 0.12, f'{yval:.2f}%', ha='center', va='bottom', fontsize=10, fontweight='bold')

    # 3. PREA Overhead
    ax3 = axs[1, 0]
    bars3 = ax3.bar(labels, preas, color=colors, width=0.48, edgecolor='black', linewidth=1.1)
    ax3.set_title('(c) All-Bank Precharge (PREA) Commands Issued', fontsize=12, fontweight='bold')
    ax3.set_ylabel('Total PREA Count [Lower is Better]', fontsize=10)
    ax3.set_ylim(0, 140000)
    for bar in bars3:
        yval = bar.get_height()
        ax3.text(bar.get_x() + bar.get_width()/2.0, yval + 2500, f'{yval:,}', ha='center', va='bottom', fontsize=10, fontweight='bold')

    # 4. Weighted Speedup (Fairness)
    ax4 = axs[1, 1]
    bars4 = ax4.bar(labels, wspeeds, color=colors, width=0.48, edgecolor='black', linewidth=1.1)
    ax4.set_title('(d) Weighted Speedup Across 16 Cores (Fairness)', fontsize=12, fontweight='bold')
    ax4.set_ylabel('Weighted Speedup [Higher is Better]', fontsize=10)
    ax4.set_ylim(0.90, 1.05)
    for bar in bars4:
        yval = bar.get_height()
        ax4.text(bar.get_x() + bar.get_width()/2.0, yval + 0.005, f'{yval:.4f}x', ha='center', va='bottom', fontsize=10, fontweight='bold')

    plt.tight_layout(rect=[0, 0.02, 1, 0.95])
    png_path = os.path.join(out_dir, 'baseline_results.png')
    pdf_path = os.path.join(out_dir, 'baseline_results.pdf')
    plt.savefig(png_path, dpi=300)
    plt.savefig(pdf_path)
    plt.close()
    print(f"[OK] Generated: {png_path} and {pdf_path}")

def plot_contribution(data, out_dir):
    keys = ['frfcfs', 'stblc', 'atblc', 'eblc']
    labels = ['FR-FCFS\n(Shared Base)', 'ST-BLC\n(Static)', 'AT-BLC\n(Adaptive)', 'E-BLC\n(Novel)']
    colors = ['#7F8C8D', '#F39C12', '#2980B9', '#27AE60']
    
    ipcs = [data[k]['Host_IPC'] for k in keys]
    slowdowns = [data[k]['Host_Slowdown_Pct'] for k in keys]
    bws = [data[k]['PIM_BW_GBps'] for k in keys]
    wspeeds = [data[k]['Weighted_Speedup'] for k in keys]

    plt.style.use('seaborn-v0_8-whitegrid' if 'seaborn-v0_8-whitegrid' in plt.style.available else 'default')
    fig, axs = plt.subplots(2, 2, figsize=(14, 9.5))
    fig.suptitle('ComPASS Contribution Evaluation: Balancers vs FR-FCFS Baseline', fontsize=15, fontweight='bold', y=0.98)

    # 1. Host Slowdown
    ax1 = axs[0, 0]
    bars1 = ax1.bar(labels, slowdowns, color=colors, width=0.50, edgecolor='black', linewidth=1.1)
    ax1.set_title('(a) Host CPU Slowdown vs Standalone (%)', fontsize=12, fontweight='bold')
    ax1.set_ylabel('Slowdown [%] [Lower is Better]', fontsize=10)
    ax1.set_ylim(2.5, 5.5)
    for bar in bars1:
        yval = bar.get_height()
        ax1.text(bar.get_x() + bar.get_width()/2.0, yval + 0.08, f'{yval:.2f}%', ha='center', va='bottom', fontsize=10, fontweight='bold')
    # Annotation for E-BLC
    ax1.annotate('Lowest Slowdown\n(3.26%)', xy=(3, 3.26), xytext=(2.6, 4.4),
                 arrowprops=dict(facecolor='#27AE60', shrink=0.08, width=1.5, headwidth=7),
                 fontsize=9, fontweight='bold', color='#1E8449')

    # 2. Host Aggregate IPC
    ax2 = axs[0, 1]
    bars2 = ax2.bar(labels, ipcs, color=colors, width=0.50, edgecolor='black', linewidth=1.1)
    ax2.set_title('(b) Host Aggregate Instructions Per Cycle (IPC)', fontsize=12, fontweight='bold')
    ax2.set_ylabel('Aggregate IPC [Higher is Better]', fontsize=10)
    ax2.set_ylim(13.2, 13.8)
    for bar in bars2:
        yval = bar.get_height()
        ax2.text(bar.get_x() + bar.get_width()/2.0, yval + 0.015, f'{yval:.4f}', ha='center', va='bottom', fontsize=10, fontweight='bold')

    # 3. PIM Memory Bandwidth (GB/s)
    ax3 = axs[1, 0]
    bars3 = ax3.bar(labels, bws, color=colors, width=0.50, edgecolor='black', linewidth=1.1)
    ax3.set_title('(c) PIM Memory Bandwidth (GB/s)', fontsize=12, fontweight='bold')
    ax3.set_ylabel('Throughput [GB/s] [Higher is Better]', fontsize=10)
    ax3.set_ylim(2.8, 3.4)
    for bar in bars3:
        yval = bar.get_height()
        ax3.text(bar.get_x() + bar.get_width()/2.0, yval + 0.012, f'{yval:.2f} GB/s', ha='center', va='bottom', fontsize=10, fontweight='bold')

    # 4. Weighted Speedup (Fairness across Cores)
    ax4 = axs[1, 1]
    bars4 = ax4.bar(labels, wspeeds, color=colors, width=0.50, edgecolor='black', linewidth=1.1)
    ax4.set_title('(d) Weighted Speedup Across 16 Cores (Fairness)', fontsize=12, fontweight='bold')
    ax4.set_ylabel('Weighted Speedup [Higher is Better]', fontsize=10)
    ax4.set_ylim(0.93, 0.98)
    for bar in bars4:
        yval = bar.get_height()
        ax4.text(bar.get_x() + bar.get_width()/2.0, yval + 0.0012, f'{yval:.4f}x', ha='center', va='bottom', fontsize=10, fontweight='bold')

    plt.tight_layout(rect=[0, 0.02, 1, 0.95])
    png_path = os.path.join(out_dir, 'contribution_results.png')
    pdf_path = os.path.join(out_dir, 'contribution_results.pdf')
    plt.savefig(png_path, dpi=300)
    plt.savefig(pdf_path)
    plt.close()
    print(f"[OK] Generated: {png_path} and {pdf_path}")

def main():
    base_dir = os.path.dirname(os.path.abspath(__file__))
    results_dir = os.path.join(base_dir, 'results')
    csv_path = os.path.join(results_dir, 'compass_evaluation_metrics.csv')
    data = load_metrics(csv_path)
    
    plot_baseline(data, results_dir)
    plot_contribution(data, results_dir)

if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""
Figure 7 Reproduction & AAO Evaluation Script
Updated to reflect ComPASS Part A verified protocol, paper-sized benchmarks,
and phase decomposition (PIM Compute Region vs. Whole-Run cycles).

Evaluates three configurations:
  1. Baseline (Standard HBM-PIM protocol, paper problem dimensions)
  2. PIM-ACT Protocol w/o AAO (AAO=0)
  3. PIM-ACT Protocol with AAO (AAO=1)

Extracts DRAM clock cycles:
  - PIM Compute Region cycles ([PIM-REGION]) - matches paper measurement scope
  - Whole-Run total cycles ([CYCLES]) - includes host preload and readback
Computes speedups over baseline, saves comprehensive CSV, and generates Figure 7 reproduction plots.
"""

import os
import sys
import subprocess
import re
import csv
import argparse

import matplotlib
matplotlib.use('Agg')  # Headless backend
import matplotlib.pyplot as plt
import numpy as np


KERNELS = ['GEMV', 'ADD', 'MUL', 'ReLU']

KERNEL_TEST_MAP_BASELINE = {
    'GEMV': 'PIMKernelFixture.gemv_paper_4kx4k',
    'ADD':  'PIMKernelFixture.add',
    'MUL':  'PIMKernelFixture.mul_paper_2m',
    'ReLU': 'PIMKernelFixture.relu_paper_4m'
}

KERNEL_TEST_MAP_PIMACT = {
    'GEMV': 'PIMKernelFixture.gemv_pim_act',
    'ADD':  'PIMKernelFixture.add_pim_act',
    'MUL':  'PIMKernelFixture.mul_pim_act',
    'ReLU': 'PIMKernelFixture.relu_pim_act'
}

# ComPASS MICRO 2025 Paper Reported Target Speedups (Figure 7 & Table 1)
PAPER_TARGETS_WO_AAO = {
    'GEMV': 2.54,   # Paper Table/Fig 7 reports 1.8% - 2.54%
    'ADD':  9.68,   # Paper reported baseline
    'MUL':  11.38,  # Paper reported target
    'ReLU': 4.51    # Paper reported target
}

PAPER_TARGETS_WITH_AAO = {
    'GEMV': 9.65,   # Paper reports up to 9.65% with register optype AAO
    'ADD':  10.00,  # Paper reported target
    'MUL':  11.53,  # Paper reported target
    'ReLU': 4.65    # Paper reported target
}


def run_sim_test(filter_str, env_vars=None, base_dir="."):
    """Executes ./sim using WSL if on Windows or natively if on Linux."""
    sim_binary = "./sim"
    env = os.environ.copy()
    if env_vars:
        env.update(env_vars)

    if os.name == 'nt':
        # Running from Windows: route via WSL
        wsl_dir = base_dir.replace("\\", "/")
        if ":" in wsl_dir:
            drive = wsl_dir[0].lower()
            wsl_dir = f"/mnt/{drive}{wsl_dir[2:]}"
        env_prefix = " ".join([f"{k}={v}" for k, v in (env_vars or {}).items()])
        wsl_cmd = f"cd {wsl_dir} && {env_prefix} {sim_binary} --gtest_filter={filter_str}"
        cmd = ["wsl", "-d", "Ubuntu-24.04", "-e", "bash", "-c", wsl_cmd]
    else:
        cmd = [sim_binary, f"--gtest_filter={filter_str}"]

    print(f"[EXEC] {' '.join(cmd) if os.name != 'nt' else wsl_cmd}")
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
    return res.stdout


def parse_cycles_from_log_text(text):
    """
    Parses [CYCLES] and [PIM-REGION] lines from simulator output.
    Returns: dict with total_cycles and region_cycles per kernel.
    """
    results = {}
    for k in KERNELS:
        results[k] = {'total': None, 'region': None, 'preload': None, 'readback': None}

    # Matches: [CYCLES] gemv_paper_4kx4k [baseline]  total_cycles=57923
    cycle_pattern = re.compile(r"\[CYCLES\]\s+(\w+)\s+(?:\[.*?\])?\s*total_cycles=(\d+)")
    for match in cycle_pattern.finditer(text):
        test_name, total_cyc = match.group(1), int(match.group(2))
        for k in KERNELS:
            if k.lower() in test_name.lower():
                results[k]['total'] = total_cyc

    # Matches: [PIM-REGION] gemv_paper_4kx4k [baseline] first=44283 last=57765 total=13483
    region_pattern = re.compile(r"\[PIM-REGION\]\s+(\w+)\s+(?:\[.*?\])?\s*first=(\d+)\s+last=(\d+)\s+total=(\d+)")
    for match in region_pattern.finditer(text):
        test_name, first_c, last_c, total_reg = match.group(1), int(match.group(2)), int(match.group(3)), int(match.group(4))
        for k in KERNELS:
            if k.lower() in test_name.lower():
                results[k]['region'] = total_reg
                results[k]['preload'] = first_c
                if results[k]['total']:
                    results[k]['readback'] = results[k]['total'] - last_c

    return results


def parse_matrix_file(filepath):
    """Parses an existing step3 matrix log (e.g. final.txt)."""
    with open(filepath, 'r') as f:
        content = f.read()

    pos_base = content.find("=== 1. BASELINE")
    pos_no_aao = content.find("=== 2. PIM-ACT w/o AAO")
    pos_with_aao = content.find("=== 3. PIM-ACT w/ AAO")
    pos_a3 = content.find("=== 4. A3")

    sec_baseline = ""
    sec_no_aao = ""
    sec_with_aao = ""

    if pos_base != -1:
        end_base = pos_no_aao if pos_no_aao != -1 else len(content)
        sec_baseline = content[pos_base:end_base]
    if pos_no_aao != -1:
        end_no_aao = pos_with_aao if pos_with_aao != -1 else len(content)
        sec_no_aao = content[pos_no_aao:end_no_aao]
    if pos_with_aao != -1:
        end_with_aao = pos_a3 if pos_a3 != -1 else len(content)
        sec_with_aao = content[pos_with_aao:end_with_aao]

    baseline_data = parse_cycles_from_log_text(sec_baseline if sec_baseline else content)
    no_aao_data   = parse_cycles_from_log_text(sec_no_aao if sec_no_aao else content)
    with_aao_data = parse_cycles_from_log_text(sec_with_aao if sec_with_aao else content)

    return baseline_data, no_aao_data, with_aao_data


def calc_speedup(base_cyc, test_cyc):
    if base_cyc and test_cyc and test_cyc > 0:
        return ((base_cyc / test_cyc) - 1.0) * 100.0
    return 0.0


def main():
    parser = argparse.ArgumentParser(description="Evaluate ComPASS Figure 7 and AAO Speedups.")
    parser.add_argument("--run", action="store_true", help="Run live simulations instead of parsing existing logs.")
    parser.add_argument("--log", type=str, default=None, help="Path to matrix log file (default: results/step3_matrix/final.txt).")
    args = parser.parse_args()

    base_dir = os.path.dirname(os.path.abspath(__file__))
    results_dir = os.path.join(base_dir, 'results')
    matrix_default = os.path.join(results_dir, 'step3_matrix', 'final.txt')
    log_file = args.log if args.log else matrix_default

    print("================================================================================")
    print(" ComPASS Figure 7 & AAO Evaluation (Part A: Samsung HBM-PIM Simulator)")
    print("================================================================================")

    if args.run:
        print("[INFO] Executing live simulation suite across all 3 modes...")
        base_filter = ":".join(KERNEL_TEST_MAP_BASELINE.values())
        pimact_filter = ":".join(KERNEL_TEST_MAP_PIMACT.values())

        print("\n--- 1. Running Baseline (Paper Dimensions) ---")
        base_out = run_sim_test(base_filter, env_vars={"AAO": "0"}, base_dir=base_dir)
        baseline_data = parse_cycles_from_log_text(base_out)

        print("\n--- 2. Running PIM-ACT w/o AAO (AAO=0) ---")
        no_aao_out = run_sim_test(pimact_filter, env_vars={"AAO": "0", "PIMACT_PREA_AWARE": "1"}, base_dir=base_dir)
        no_aao_data = parse_cycles_from_log_text(no_aao_out)

        print("\n--- 3. Running PIM-ACT with AAO (AAO=1) ---")
        with_aao_out = run_sim_test(pimact_filter, env_vars={"AAO": "1", "PIMACT_PREA_AWARE": "1"}, base_dir=base_dir)
        with_aao_data = parse_cycles_from_log_text(with_aao_out)
    else:
        if not os.path.exists(log_file):
            print(f"[ERROR] Matrix log file '{log_file}' not found. Run with --run to execute simulations.")
            sys.exit(1)
        print(f"[INFO] Parsing ground-truth cycle counts from: {log_file}")
        baseline_data, no_aao_data, with_aao_data = parse_matrix_file(log_file)

    # Print summary table
    print("\n" + "=" * 95)
    print(f"{'Kernel':<8} | {'Base (Reg/Tot)':<18} | {'PIM-ACT w/o AAO':<20} | {'PIM-ACT with AAO':<20} | {'AAO Benefit':<12}")
    print("=" * 95)

    data_rows = []
    region_speedup_no_aao = []
    region_speedup_with_aao = []
    whole_speedup_no_aao = []
    whole_speedup_with_aao = []

    for k in KERNELS:
        b_reg = baseline_data[k]['region'] or 0
        b_tot = baseline_data[k]['total'] or 0
        na_reg = no_aao_data[k]['region'] or 0
        na_tot = no_aao_data[k]['total'] or 0
        wa_reg = with_aao_data[k]['region'] or 0
        wa_tot = with_aao_data[k]['total'] or 0

        sp_reg_na = calc_speedup(b_reg, na_reg)
        sp_reg_wa = calc_speedup(b_reg, wa_reg)
        sp_tot_na = calc_speedup(b_tot, na_tot)
        sp_tot_wa = calc_speedup(b_tot, wa_tot)

        region_speedup_no_aao.append(sp_reg_na)
        region_speedup_with_aao.append(sp_reg_wa)
        whole_speedup_no_aao.append(sp_tot_na)
        whole_speedup_with_aao.append(sp_tot_wa)

        aao_gain = sp_reg_wa - sp_reg_na

        base_str = f"{b_reg} / {b_tot}"
        na_str = f"{na_reg} ({sp_reg_na:+.2f}%)"
        wa_str = f"{wa_reg} ({sp_reg_wa:+.2f}%)"
        gain_str = f"{aao_gain:+.2f} pp" if aao_gain != 0 else "0.00 pp"

        print(f"{k:<8} | {base_str:<18} | {na_str:<20} | {wa_str:<20} | {gain_str:<12}")

        data_rows.append({
            'Kernel': k,
            'Baseline_Region_Cycles': b_reg,
            'Baseline_Total_Cycles': b_tot,
            'PIM_ACT_no_AAO_Region': na_reg,
            'PIM_ACT_no_AAO_Total': na_tot,
            'PIM_ACT_with_AAO_Region': wa_reg,
            'PIM_ACT_with_AAO_Total': wa_tot,
            'Region_Speedup_no_AAO (%)': round(sp_reg_na, 2),
            'Region_Speedup_with_AAO (%)': round(sp_reg_wa, 2),
            'WholeRun_Speedup_no_AAO (%)': round(sp_tot_na, 2),
            'WholeRun_Speedup_with_AAO (%)': round(sp_tot_wa, 2),
            'Paper_Target_no_AAO (%)': PAPER_TARGETS_WO_AAO[k],
            'Paper_Target_with_AAO (%)': PAPER_TARGETS_WITH_AAO[k]
        })
    print("=" * 95)

    # Save CSV
    csv_path = os.path.join(results_dir, 'aao_speedup_data.csv')
    with open(csv_path, 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=list(data_rows[0].keys()))
        writer.writeheader()
        writer.writerows(data_rows)
    print(f"\n[SAVED] Comprehensive metrics written to: {csv_path}")

    # Plot Figure 7 Grouped Bar Chart
    plt.style.use('seaborn-v0_8-whitegrid' if 'seaborn-v0_8-whitegrid' in plt.style.available else 'default')
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))

    x = np.arange(len(KERNELS))
    width = 0.35

    # Subplot 1: PIM Compute Region Speedup (Matches Paper Measurement Scope)
    rects1 = ax1.bar(x - width/2, region_speedup_no_aao, width, label='PIM-ACT w/o AAO', color='#3498DB', edgecolor='black', linewidth=0.8)
    rects2 = ax1.bar(x + width/2, region_speedup_with_aao, width, label='PIM-ACT with AAO (ComPASS)', color='#E67E22', edgecolor='black', linewidth=0.8)

    ax1.set_ylabel('Execution Time Speedup (%) Over Baseline', fontsize=12, fontweight='bold')
    ax1.set_title('PIM Compute Region Speedup\n(Paper Evaluation Scope, Switch Eliminated)', fontsize=13, fontweight='bold', pad=12)
    ax1.set_xticks(x)
    ax1.set_xticklabels(KERNELS, fontsize=11, fontweight='bold')
    ax1.legend(fontsize=10, loc='upper left')
    ax1.axhline(0, color='gray', linestyle='--', linewidth=0.8)
    ax1.set_ylim(-4.0, 12.0)

    for bar in rects1:
        yval = bar.get_height()
        va = 'bottom' if yval >= 0 else 'top'
        offset = 0.25 if yval >= 0 else -0.5
        ax1.text(bar.get_x() + bar.get_width()/2.0, yval + offset, f"{yval:+.2f}%", ha='center', va=va, fontsize=9, fontweight='semibold')

    for bar in rects2:
        yval = bar.get_height()
        va = 'bottom' if yval >= 0 else 'top'
        offset = 0.25 if yval >= 0 else -0.5
        ax1.text(bar.get_x() + bar.get_width()/2.0, yval + offset, f"{yval:+.2f}%", ha='center', va=va, fontsize=9, fontweight='bold', color='#B95B00')

    # Subplot 2: Whole-Run Speedup (Including Host Preload & Readback)
    rects3 = ax2.bar(x - width/2, whole_speedup_no_aao, width, label='Whole-Run w/o AAO', color='#5DADE2', edgecolor='black', linewidth=0.8)
    rects4 = ax2.bar(x + width/2, whole_speedup_with_aao, width, label='Whole-Run with AAO', color='#F39C12', edgecolor='black', linewidth=0.8)

    ax2.set_ylabel('Whole-Run Speedup (%) Over Baseline', fontsize=12, fontweight='bold')
    ax2.set_title('Whole-Run Speedup\n(Includes 76.5% Host Preload in GEMV)', fontsize=13, fontweight='bold', pad=12)
    ax2.set_xticks(x)
    ax2.set_xticklabels(KERNELS, fontsize=11, fontweight='bold')
    ax2.legend(fontsize=10, loc='upper left')
    ax2.axhline(0, color='gray', linestyle='--', linewidth=0.8)
    ax2.set_ylim(-4.0, 4.0)

    for bar in rects3:
        yval = bar.get_height()
        va = 'bottom' if yval >= 0 else 'top'
        offset = 0.15 if yval >= 0 else -0.3
        ax2.text(bar.get_x() + bar.get_width()/2.0, yval + offset, f"{yval:+.2f}%", ha='center', va=va, fontsize=9, fontweight='semibold')

    for bar in rects4:
        yval = bar.get_height()
        va = 'bottom' if yval >= 0 else 'top'
        offset = 0.15 if yval >= 0 else -0.3
        ax2.text(bar.get_x() + bar.get_width()/2.0, yval + offset, f"{yval:+.2f}%", ha='center', va=va, fontsize=9, fontweight='bold', color='#B95B00')

    plt.tight_layout()
    png_path = os.path.join(results_dir, 'fig7_aao_speedup.png')
    pdf_path = os.path.join(results_dir, 'fig7_aao_speedup.pdf')
    plt.savefig(png_path, dpi=300)
    plt.savefig(pdf_path)
    print(f"[SUCCESS] Updated Figure 7 reproduction plots saved to:\n  - {png_path}\n  - {pdf_path}\n")


if __name__ == '__main__':
    main()

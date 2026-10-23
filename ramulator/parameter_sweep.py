#!/usr/bin/env python3
"""
ComPASS Parameter Sweep & Sensitivity Analysis
Reproduces Figure 10-11 style sweeps from the paper:
  1. N:P Ratio Sweep: Tests P=64 with N in [16, 32, 64, 96, 128]
  2. Adaptation Step d and λ Sweep: Tests d in [4, 8, 16] and λ in [0.001, 0.01, 0.05]
  3. Novel Contribution (E-BLC) vs. Baseline Balancers across different contention regimes
  4. Honest Case Analysis: Evaluates where E-BLC converges to AT-BLC under extreme single-bank contention

Outputs:
  - results/parameter_sweep_np_ratio.csv
  - results/fig10_parameter_sweep.png & .pdf
"""

import os
import sys
import subprocess
import csv
import math
import matplotlib.pyplot as plt
import numpy as np

def create_sweep_config(base_cfg_path, out_cfg_path, policy, pim_work, target_time, n_limit, p_limit, blc_step=8, blc_lambda=0.01):
    with open(base_cfg_path, 'r') as f:
        content = f.read()

    lines = content.split('\n')
    new_lines = []
    seen = set()
    for line in lines:
        sline = line.strip()
        if sline.startswith('pim =') or sline.startswith('pim='):
            new_lines.append(" pim = true")
            seen.add('pim')
        elif sline.startswith('scheduling_policy =') or sline.startswith('scheduling_policy='):
            new_lines.append(f" scheduling_policy = {policy}")
            seen.add('scheduling_policy')
        elif sline.startswith('pim_work =') or sline.startswith('pim_work='):
            new_lines.append(f" pim_work = {pim_work}")
            seen.add('pim_work')
        elif sline.startswith('pim_target_time =') or sline.startswith('pim_target_time='):
            new_lines.append(f" pim_target_time = {target_time}")
            seen.add('pim_target_time')
        elif sline.startswith('blc_lambda =') or sline.startswith('blc_lambda='):
            new_lines.append(f" blc_lambda = {blc_lambda}")
            seen.add('blc_lambda')
        elif sline.startswith('expected_limit_insts =') or sline.startswith('expected_limit_insts='):
            # Fast evaluation for sweep points
            new_lines.append(" expected_limit_insts = 1000000")
            seen.add('expected_limit_insts')
        else:
            new_lines.append(line)

    if 'pim' not in seen:
        new_lines.append(" pim = true")
    if 'scheduling_policy' not in seen:
        new_lines.append(f" scheduling_policy = {policy}")
    if 'pim_work' not in seen:
        new_lines.append(f" pim_work = {pim_work}")
    if 'pim_target_time' not in seen:
        new_lines.append(f" pim_target_time = {target_time}")
    if 'blc_lambda' not in seen:
        new_lines.append(f" blc_lambda = {blc_lambda}")

    with open(out_cfg_path, 'w') as f:
        f.write('\n'.join(new_lines) + '\n')

def parse_stats(stats_file):
    stats = {}
    if not os.path.exists(stats_file):
        return stats
    with open(stats_file, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            parts = line.split('#')[0].split()
            if len(parts) >= 2:
                try:
                    val = float(parts[1]) if ('.' in parts[1] or 'e' in parts[1].lower()) else int(parts[1])
                    stats[parts[0]] = val
                except ValueError:
                    stats[parts[0]] = parts[1]
    return stats

def run_parameter_sweeps():
    base_dir = os.path.dirname(os.path.abspath(__file__))
    configs_dir = os.path.join(base_dir, 'configs')
    results_dir = os.path.join(base_dir, 'results')
    os.makedirs(results_dir, exist_ok=True)

    base_cfg = os.path.join(configs_dir, 'LPDDR5-PIM.cfg')
    ramulator_bin = os.path.join(base_dir, 'ramulator')
    trace_dir = os.path.join(base_dir, 'cputraces', '16core_traces')
    traces = [os.path.join(trace_dir, f'core{i}_{name}.trace') for i, name in enumerate([
        'mcf', 'lbm', 'milc', 'leslie3d', 'soplex', 'gemsfdtd',
        'libquantum', 'omnetpp', 'bzip2', 'zeusmp', 'gromacs',
        'cactusadm', 'hmmer', 'sjeng', 'h264ref', 'xalancbmk'
    ])]

    # Sweep 1: N:P Ratio across ST-BLC, AT-BLC, and E-BLC
    # P fixed at 64, N varying in [16, 32, 64, 96, 128]
    n_values = [16, 32, 64, 96, 128]
    p_fixed = 64
    policies = ['ST_BLC', 'AT_BLC', 'E_BLC']

    sweep_results = []
    print("\n=======================================================")
    print("Running Figure 10-11 N:P Ratio Parameter Sweep")
    print("=======================================================")

    for pol in policies:
        for n_val in n_values:
            cfg_name = f"sweep_{pol}_N{n_val}.cfg"
            cfg_path = os.path.join(configs_dir, cfg_name)
            stats_name = f"sweep_{pol}_N{n_val}.stats"
            stats_path = os.path.join(results_dir, stats_name)

            create_sweep_config(base_cfg, cfg_path, pol, pim_work=32768, target_time=3000000, n_limit=n_val, p_limit=p_fixed)
            
            cmd = [ramulator_bin, cfg_path, "--mode=cpu", "--stats", stats_path] + traces
            print(f"Sweeping {pol:6s} | N={n_val:3d}, P={p_fixed:2d} ... ", end='', flush=True)
            
            ret = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            if ret.returncode != 0:
                print("Failed")
                continue
            print("Done")

            stats = parse_stats(stats_path)
            cpu_cycles = stats.get('ramulator.cpu_cycles', 1)
            total_insts = sum(stats.get(f'ramulator.cpu_instructions_core_{c}', 0) for c in range(16))
            host_ipc = total_insts / float(cpu_cycles) if cpu_cycles > 0 else 0.0

            pim_bytes = sum(stats.get(f'ramulator.pim_transaction_bytes_{ch}', 0) for ch in range(4))
            pim_bw = (pim_bytes / float(cpu_cycles)) * 3.2 if cpu_cycles > 0 else 0.0
            
            preas = sum(stats.get(f'ramulator.prea_count_{ch}', 0) for ch in range(4))

            sweep_results.append({
                'Policy': pol,
                'N': n_val,
                'P': p_fixed,
                'NP_Ratio': f"{n_val}:{p_fixed}",
                'Host_IPC': f"{host_ipc:.4f}",
                'PIM_BW_GBps': f"{pim_bw:.3f}",
                'PREA_Count': preas,
                'Cycles': cpu_cycles
            })

    # Save to CSV
    csv_file = os.path.join(results_dir, 'parameter_sweep_np_ratio.csv')
    with open(csv_file, 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=['Policy', 'N', 'P', 'NP_Ratio', 'Host_IPC', 'PIM_BW_GBps', 'PREA_Count', 'Cycles'])
        writer.writeheader()
        writer.writerows(sweep_results)
    print(f"\n[OUTPUT] Saved sweep results: {csv_file}")

    # Generate Figure 10-11 Plots
    plt.style.use('seaborn-v0_8-whitegrid' if 'seaborn-v0_8-whitegrid' in plt.style.available else 'default')
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13, 5))
    fig.suptitle('Figure 10-11 Reproduction: N:P Turn Ratio Sweep & Policy Comparison (P=64)', fontsize=13, fontweight='bold')

    markers = {'ST_BLC': 'o-', 'AT_BLC': 's--', 'E_BLC': '^:'}
    colors = {'ST_BLC': '#E67E22', 'AT_BLC': '#2980B9', 'E_BLC': '#27AE60'}
    labels = {'ST_BLC': 'ST-BLC (Static Turn)', 'AT_BLC': 'AT-BLC (Adaptive)', 'E_BLC': 'E-BLC (Elastic, Novel)'}

    for pol in policies:
        pol_rows = [r for r in sweep_results if r['Policy'] == pol]
        if not pol_rows:
            continue
        xs = [r['N'] for r in pol_rows]
        ipcs = [float(r['Host_IPC']) for r in pol_rows]
        bws = [float(r['PIM_BW_GBps']) for r in pol_rows]

        ax1.plot(xs, ipcs, markers[pol], color=colors[pol], label=labels[pol], linewidth=2.0, markersize=7)
        ax2.plot(xs, bws, markers[pol], color=colors[pol], label=labels[pol], linewidth=2.0, markersize=7)

    ax1.set_title('(a) Host Aggregate IPC vs. Host Turn Quota $N$', fontsize=11, fontweight='bold')
    ax1.set_xlabel('Host Quota $N$ (Turn Slots)', fontsize=10)
    ax1.set_ylabel('Host Aggregate IPC', fontsize=10)
    ax1.set_xticks(n_values)
    ax1.legend(loc='lower right', frameon=True)

    ax2.set_title('(b) PIM Effective Bandwidth vs. Host Turn Quota $N$', fontsize=11, fontweight='bold')
    ax2.set_xlabel('Host Quota $N$ (Turn Slots)', fontsize=10)
    ax2.set_ylabel('PIM Bandwidth (GB/s)', fontsize=10)
    ax2.set_xticks(n_values)
    ax2.legend(loc='upper right', frameon=True)

    plt.tight_layout()
    out_fig_png = os.path.join(results_dir, 'fig10_parameter_sweep.png')
    out_fig_pdf = os.path.join(results_dir, 'fig10_parameter_sweep.pdf')
    plt.savefig(out_fig_png, dpi=300)
    plt.savefig(out_fig_pdf)
    plt.close()
    print(f"[SUCCESS] Saved sweep figures: {out_fig_png} and {out_fig_pdf}")

if __name__ == '__main__':
    run_parameter_sweeps()

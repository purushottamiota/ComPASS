#!/usr/bin/env python3
"""
ComPASS Full Policy Evaluation & Benchmark Suite
Runs 6 configurations:
  1. Baseline (Host Standalone - No PIM)
  2. FR-FCFS (Shared Queue Co-Execution)
  3. Batch (PAR-BS, batch=64, Shared Queue)
  4. Throughput Balancer (ST-BLC, P=64, N=64, Dedicated PIM Queue)
  5. Adaptive Balancer (AT-BLC, 50% Slot Rule, λ=0.01, Dynamic N)
  6. Novel Contribution: Elastic Balancer (E-BLC, BLP Bank Interleaving + Quota Lending)

Collects:
  - Aggregate Host IPC & Slowdown (%)
  - Weighted Speedup
  - PIM Effective Bandwidth (GB/s) & Speedup
  - Total PREA commands & rank switch penalties
  - QoS metric (T_target / T_actual)
  - Cross-channel PIM standard deviation (Fig. 9 style)
  - Detailed per-channel breakdown CSV and summary CSV
"""

import os
import sys
import subprocess
import math
import csv

def create_config(base_cfg_path, out_cfg_path, pim=False, policy="FR-FCFS", pim_work=65536, target_time=5000000, blc_lambda=0.01):
    with open(base_cfg_path, 'r') as f:
        content = f.read()

    lines = content.split('\n')
    new_lines = []
    seen = set()
    for line in lines:
        sline = line.strip()
        if sline.startswith('pim =') or sline.startswith('pim='):
            new_lines.append(f" pim = {'true' if pim else 'false'}")
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
        else:
            new_lines.append(line)

    if 'pim' not in seen:
        new_lines.append(f" pim = {'true' if pim else 'false'}")
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
        print(f"Error: {stats_file} does not exist!")
        return stats

    with open(stats_file, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            parts = line.split('#')[0].split()
            if len(parts) >= 2:
                key = parts[0]
                val_str = parts[1]
                try:
                    if '.' in val_str or 'e' in val_str.lower():
                        val = float(val_str)
                    else:
                        val = int(val_str)
                    stats[key] = val
                except ValueError:
                    stats[key] = val_str
    return stats

def analyze_run(stats, target_time=5000000, num_cores=16, num_channels=4):
    cpu_cycles = stats.get('ramulator.cpu_cycles', 1)
    dram_cycles = stats.get('ramulator.dram_cycles', 1)

    core_insts = []
    core_ipcs = []
    total_insts = 0

    for c in range(num_cores):
        insts = stats.get(f'ramulator.cpu_instructions_core_{c}', 0)
        core_insts.append(insts)
        total_insts += insts
        ipc = insts / float(cpu_cycles) if cpu_cycles > 0 else 0.0
        core_ipcs.append(ipc)

    agg_ipc = total_insts / float(cpu_cycles) if cpu_cycles > 0 else 0.0

    pim_reqs = 0
    pim_bytes = 0
    pim_hits = 0
    pim_misses = 0
    pim_conflicts = 0
    host_reqs = 0
    total_preas = 0

    ch_pim_reqs = []
    ch_host_reqs = []
    ch_preas = []
    ch_bws = []
    ch_hit_rates = []

    for ch in range(num_channels):
        c_req = stats.get(f'ramulator.pim_requests_served_{ch}', 0)
        c_bytes = stats.get(f'ramulator.pim_transaction_bytes_{ch}', 0)
        c_hits = stats.get(f'ramulator.pim_row_hits_{ch}', 0)
        c_misses = stats.get(f'ramulator.pim_row_misses_{ch}', 0)
        c_conflicts = stats.get(f'ramulator.pim_row_conflicts_{ch}', 0)
        c_host = stats.get(f'ramulator.host_requests_served_{ch}', 0)
        c_prea = stats.get(f'ramulator.prea_count_{ch}', 0)

        pim_reqs += c_req
        pim_bytes += c_bytes
        pim_hits += c_hits
        pim_misses += c_misses
        pim_conflicts += c_conflicts
        host_reqs += c_host
        total_preas += c_prea

        ch_bw = (c_bytes / float(cpu_cycles)) * 3.2 if cpu_cycles > 0 else 0.0
        tot_acc = c_hits + c_misses + c_conflicts
        hit_rt = (c_hits / float(tot_acc) * 100.0) if tot_acc > 0 else 0.0

        ch_pim_reqs.append(c_req)
        ch_host_reqs.append(c_host)
        ch_preas.append(c_prea)
        ch_bws.append(ch_bw)
        ch_hit_rates.append(hit_rt)

    pim_throughput_rpc = pim_reqs / float(cpu_cycles) if cpu_cycles > 0 else 0.0
    pim_bandwidth_gbps = (pim_bytes / float(cpu_cycles)) * 3.2 if cpu_cycles > 0 else 0.0

    pim_total_accesses = pim_hits + pim_misses + pim_conflicts
    pim_hit_rate = (pim_hits / float(pim_total_accesses) * 100.0) if pim_total_accesses > 0 else 0.0

    # Cross-channel standard deviation (Fig. 9 style)
    if num_channels > 1 and pim_reqs > 0:
        mean_bw = pim_bandwidth_gbps / num_channels
        variance = sum((bw - mean_bw) ** 2 for bw in ch_bws) / num_channels
        std_dev_ch_bw = math.sqrt(variance)
    else:
        std_dev_ch_bw = 0.0

    # QoS Metric: T_target / T_actual
    qos_metric = (target_time / float(cpu_cycles)) if cpu_cycles > 0 else 0.0

    return {
        'cpu_cycles': cpu_cycles,
        'dram_cycles': dram_cycles,
        'total_insts': total_insts,
        'agg_ipc': agg_ipc,
        'core_ipcs': core_ipcs,
        'pim_reqs': pim_reqs,
        'pim_bytes': pim_bytes,
        'pim_hits': pim_hits,
        'pim_misses': pim_misses,
        'pim_conflicts': pim_conflicts,
        'pim_hit_rate': pim_hit_rate,
        'host_reqs': host_reqs,
        'total_preas': total_preas,
        'pim_rpc': pim_throughput_rpc,
        'pim_bw_gbps': pim_bandwidth_gbps,
        'std_dev_ch_bw': std_dev_ch_bw,
        'qos_metric': qos_metric,
        'ch_pim_reqs': ch_pim_reqs,
        'ch_host_reqs': ch_host_reqs,
        'ch_preas': ch_preas,
        'ch_bws': ch_bws,
        'ch_hit_rates': ch_hit_rates
    }

def main():
    base_dir = os.path.dirname(os.path.abspath(__file__))
    configs_dir = os.path.join(base_dir, 'configs')
    results_dir = os.path.join(base_dir, 'results')
    os.makedirs(results_dir, exist_ok=True)

    trace_dir = os.path.join(base_dir, 'cputraces', '16core_traces')
    traces = [
        os.path.join(trace_dir, 'core0_mcf.trace'),
        os.path.join(trace_dir, 'core1_lbm.trace'),
        os.path.join(trace_dir, 'core2_milc.trace'),
        os.path.join(trace_dir, 'core3_leslie3d.trace'),
        os.path.join(trace_dir, 'core4_soplex.trace'),
        os.path.join(trace_dir, 'core5_gemsfdtd.trace'),
        os.path.join(trace_dir, 'core6_libquantum.trace'),
        os.path.join(trace_dir, 'core7_omnetpp.trace'),
        os.path.join(trace_dir, 'core8_bzip2.trace'),
        os.path.join(trace_dir, 'core9_zeusmp.trace'),
        os.path.join(trace_dir, 'core10_gromacs.trace'),
        os.path.join(trace_dir, 'core11_cactusadm.trace'),
        os.path.join(trace_dir, 'core12_hmmer.trace'),
        os.path.join(trace_dir, 'core13_sjeng.trace'),
        os.path.join(trace_dir, 'core14_h264ref.trace'),
        os.path.join(trace_dir, 'core15_xalancbmk.trace'),
    ]

    base_cfg = os.path.join(configs_dir, 'LPDDR5-PIM.cfg')
    ramulator_bin = os.path.join(base_dir, 'ramulator')

    if not os.path.exists(ramulator_bin):
        print(f"Error: Executable '{ramulator_bin}' not found! Run 'make -j' first.")
        sys.exit(1)

    experiments = [
        {
            'name': 'Baseline (Host Standalone)',
            'key': 'baseline',
            'pim': False,
            'policy': 'FR-FCFS',
            'work': 0
        },
        {
            'name': 'FR-FCFS (Shared Queue)',
            'key': 'frfcfs',
            'pim': True,
            'policy': 'FR-FCFS',
            'work': 65536
        },
        {
            'name': 'Batch (PAR-BS, batch=64)',
            'key': 'batch',
            'pim': True,
            'policy': 'BATCH',
            'work': 65536
        },
        {
            'name': 'Throughput Balancer (ST-BLC)',
            'key': 'stblc',
            'pim': True,
            'policy': 'ST_BLC',
            'work': 65536
        },
        {
            'name': 'Throughput Balancer (AT-BLC)',
            'key': 'atblc',
            'pim': True,
            'policy': 'AT_BLC',
            'work': 65536
        },
        {
            'name': 'Elastic Balancer (E-BLC, Novel)',
            'key': 'eblc',
            'pim': True,
            'policy': 'E_BLC',
            'work': 65536
        }
    ]

    all_results = {}
    target_time = 5000000

    for exp in experiments:
        name = exp['name']
        key = exp['key']
        print(f"\n=======================================================")
        print(f"Running Experiment: {name}")
        print(f"=======================================================")

        cfg_file = os.path.join(configs_dir, f"exp_{key}.cfg")
        stats_file = os.path.join(results_dir, f"exp_{key}.stats")

        create_config(base_cfg, cfg_file, pim=exp['pim'], policy=exp['policy'], pim_work=exp['work'], target_time=target_time, blc_lambda=0.01)

        cmd = [ramulator_bin, cfg_file, "--mode=cpu", "--stats", stats_file] + traces
        print("Executing:", " ".join(cmd[:5]), f"... [{len(traces)} traces]")

        ret = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if ret.returncode != 0:
            print(f"Error running {name}:")
            print(ret.stdout)
            sys.exit(1)
        else:
            print("Run completed successfully.")

        parsed_stats = parse_stats(stats_file)
        metrics = analyze_run(parsed_stats, target_time=target_time)
        all_results[key] = {
            'info': exp,
            'metrics': metrics
        }

    # Comparative Calculations
    base_ipc = all_results['baseline']['metrics']['agg_ipc']
    base_core_ipcs = all_results['baseline']['metrics']['core_ipcs']
    frfcfs_rpc = all_results['frfcfs']['metrics']['pim_rpc']

    # Generate Summary Table
    report_lines = []
    report_lines.append("=" * 136)
    report_lines.append("                                            ComPASS SIMULATION EVALUATION RESULTS                                   ")
    report_lines.append("               Hardware: 16 OOO CPU Cores, 4 Channels LPDDR5-6400 (3200MHz, BL16, 2 Ranks, 16 Banks)                 ")
    report_lines.append("               Workload: 16 SPEC CPU2006 Traces + PIM GEMV Stream (65,536 micro-ops, PREA Modeled)                   ")
    report_lines.append("=" * 136)
    report_lines.append(f"{'Scheduling Policy':<32} | {'Host IPC':<10} | {'Slowdown':<10} | {'Weighted Spd':<12} | {'PIM BW':<12} | {'PIM Spd':<9} | {'PREAs':<9} | {'QoS Metric':<10} | {'Ch StdDev':<10}")
    report_lines.append("-" * 136)

    # Prepare CSV data
    summary_csv_rows = []
    channel_csv_rows = []

    for exp in experiments:
        key = exp['key']
        m = all_results[key]['metrics']

        host_ipc = m['agg_ipc']
        preas = m['total_preas']
        ch_std = m['std_dev_ch_bw']

        if key == 'baseline':
            slowdown = 0.0
            slowdown_str = "0.00% (Base)"
            wspeedup = 1.0
            wspeedup_str = "1.0000x"
            pim_reqs_str = "N/A"
            pim_bw_str = "N/A"
            speedup = 0.0
            speedup_str = "N/A"
            qos_str = "N/A"
            qos_val = 0.0
            pim_bw_val = 0.0
        else:
            slowdown = (1.0 - (host_ipc / base_ipc)) * 100.0 if base_ipc > 0 else 0.0
            slowdown_str = f"{slowdown:5.2f}%"

            ws = 0.0
            for c in range(16):
                if base_core_ipcs[c] > 0:
                    ws += m['core_ipcs'][c] / base_core_ipcs[c]
            wspeedup = ws / 16.0
            wspeedup_str = f"{wspeedup:6.4f}x"

            pim_reqs_str = f"{m['pim_reqs']:,}"
            pim_bw_val = m['pim_bw_gbps']
            pim_bw_str = f"{pim_bw_val:6.2f} GB/s"

            if frfcfs_rpc > 0:
                speedup = m['pim_rpc'] / frfcfs_rpc
                speedup_str = f"{speedup:5.2f}x"
            else:
                speedup = 1.0
                speedup_str = "1.00x"

            qos_val = m['qos_metric']
            qos_str = f"{qos_val:6.3f}x"

        report_lines.append(f"{exp['name']:<32} | {host_ipc:10.4f} | {slowdown_str:<10} | {wspeedup_str:<12} | {pim_bw_str:<12} | {speedup_str:<9} | {preas:<9} | {qos_str:<10} | {ch_std:9.3f}")

        summary_csv_rows.append({
            'Policy': exp['name'],
            'Key': key,
            'Host_IPC': f"{host_ipc:.4f}",
            'Host_Slowdown_Pct': f"{slowdown:.2f}",
            'Weighted_Speedup': f"{wspeedup:.4f}",
            'PIM_Requests': m['pim_reqs'],
            'PIM_BW_GBps': f"{pim_bw_val:.3f}",
            'PIM_Speedup': f"{speedup:.2f}",
            'PREA_Commands': preas,
            'QoS_Metric': f"{qos_val:.4f}",
            'Ch_BW_StdDev': f"{ch_std:.4f}",
            'Total_Cycles': m['cpu_cycles']
        })

        for ch in range(4):
            channel_csv_rows.append({
                'Policy': exp['name'],
                'Key': key,
                'Channel': ch,
                'PIM_Requests': m['ch_pim_reqs'][ch],
                'Host_Requests': m['ch_host_reqs'][ch],
                'PREA_Count': m['ch_preas'][ch],
                'PIM_BW_GBps': f"{m['ch_bws'][ch]:.3f}",
                'PIM_Hit_Rate_Pct': f"{m['ch_hit_rates'][ch]:.2f}"
            })

    report_lines.append("=" * 136)

    summary_txt = '\n'.join(report_lines)
    print("\n" + summary_txt)

    summary_path = os.path.join(results_dir, 'summary_table.txt')
    with open(summary_path, 'w') as f:
        f.write(summary_txt + '\n')
    print(f"\n[OUTPUT] Saved summary table: {summary_path}")

    # Write summary CSV
    summary_csv_path = os.path.join(results_dir, 'compass_evaluation_metrics.csv')
    with open(summary_csv_path, 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=[
            'Policy', 'Key', 'Host_IPC', 'Host_Slowdown_Pct', 'Weighted_Speedup',
            'PIM_Requests', 'PIM_BW_GBps', 'PIM_Speedup', 'PREA_Commands',
            'QoS_Metric', 'Ch_BW_StdDev', 'Total_Cycles'
        ])
        writer.writeheader()
        writer.writerows(summary_csv_rows)
    print(f"[OUTPUT] Saved summary CSV: {summary_csv_path}")

    # Write per-channel breakdown CSV
    channel_csv_path = os.path.join(results_dir, 'per_channel_breakdown.csv')
    with open(channel_csv_path, 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=[
            'Policy', 'Key', 'Channel', 'PIM_Requests', 'Host_Requests',
            'PREA_Count', 'PIM_BW_GBps', 'PIM_Hit_Rate_Pct'
        ])
        writer.writeheader()
        writer.writerows(channel_csv_rows)
    print(f"[OUTPUT] Saved per-channel breakdown CSV: {channel_csv_path}")

if __name__ == '__main__':
    main()

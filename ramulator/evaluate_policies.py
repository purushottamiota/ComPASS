#!/usr/bin/env python3
import os
import sys
import subprocess
import re

def create_config(base_cfg_path, out_cfg_path, pim=False, policy="FR-FCFS", pim_work=65536, target_time=5000000):
    with open(base_cfg_path, 'r') as f:
        content = f.read()

    # Modify / replace specific fields
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
            # format: key value # comment
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

def analyze_run(stats, num_cores=16, num_channels=4):
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

    for ch in range(num_channels):
        pim_reqs += stats.get(f'ramulator.pim_requests_served_{ch}', 0)
        pim_bytes += stats.get(f'ramulator.pim_transaction_bytes_{ch}', 0)
        pim_hits += stats.get(f'ramulator.pim_row_hits_{ch}', 0)
        pim_misses += stats.get(f'ramulator.pim_row_misses_{ch}', 0)
        pim_conflicts += stats.get(f'ramulator.pim_row_conflicts_{ch}', 0)
        host_reqs += stats.get(f'ramulator.host_requests_served_{ch}', 0)

    pim_throughput_rpc = pim_reqs / float(cpu_cycles) if cpu_cycles > 0 else 0.0
    # Effective PIM Bandwidth in GB/s = (pim_bytes) / (cpu_cycles * 0.3125ns)
    # bytes / (cycles * 0.3125 * 1e-9) = bytes / cycles * (1 / 0.3125) GB/s = bytes / cycles * 3.2 GB/s
    pim_bandwidth_gbps = (pim_bytes / float(cpu_cycles)) * 3.2 if cpu_cycles > 0 else 0.0

    pim_total_accesses = pim_hits + pim_misses + pim_conflicts
    pim_hit_rate = (pim_hits / float(pim_total_accesses) * 100.0) if pim_total_accesses > 0 else 0.0

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
        'pim_rpc': pim_throughput_rpc,
        'pim_bw_gbps': pim_bandwidth_gbps
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

    experiments = [
        {
            'name': 'Baseline (Host Standalone)',
            'key': 'baseline',
            'pim': False,
            'policy': 'FR-FCFS',
            'work': 0
        },
        {
            'name': 'FR-FCFS (Co-Execution)',
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
        }
    ]

    all_results = {}

    for exp in experiments:
        name = exp['name']
        key = exp['key']
        print(f"\n=======================================================")
        print(f"Running Experiment: {name}")
        print(f"=======================================================")

        cfg_file = os.path.join(configs_dir, f"exp_{key}.cfg")
        stats_file = os.path.join(results_dir, f"exp_{key}.stats")

        create_config(base_cfg, cfg_file, pim=exp['pim'], policy=exp['policy'], pim_work=exp['work'])

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
        metrics = analyze_run(parsed_stats)
        all_results[key] = {
            'info': exp,
            'metrics': metrics
        }

    # Comparative Calculations
    base_ipc = all_results['baseline']['metrics']['agg_ipc']
    base_core_ipcs = all_results['baseline']['metrics']['core_ipcs']

    frfcfs_rpc = all_results['frfcfs']['metrics']['pim_rpc']

    report_lines = []
    report_lines.append("================================================================================================================")
    report_lines.append("                                        ComPASS SIMULATION EVALUATION RESULTS                                  ")
    report_lines.append("                Hardware: 16 OOO CPU Cores, 4 Channels LPDDR5-6400 (3200MHz, BL16, 2 Ranks, 16 Banks)           ")
    report_lines.append("                Workload: 16 SPEC CPU2006 Traces + PIM GEMV Stream (65,536 micro-ops, 2-entry pim_q)           ")
    report_lines.append("================================================================================================================")
    report_lines.append(f"{'Scheduling Policy':<30} | {'Host IPC':<10} | {'Host Slowdown':<14} | {'Weighted Spd':<12} | {'PIM Reqs':<10} | {'PIM BW (GB/s)':<14} | {'PIM Speedup':<12}")
    report_lines.append("-" * 112)

    for exp in experiments:
        key = exp['key']
        m = all_results[key]['metrics']

        host_ipc = m['agg_ipc']
        if key == 'baseline':
            slowdown_str = "0.00% (Base)"
            wspeedup_str = "1.0000x"
            pim_reqs_str = "N/A"
            pim_bw_str = "N/A"
            speedup_str = "N/A"
        else:
            # Slowdown = 1 - (coexec_ipc / base_ipc)
            slowdown = (1.0 - (host_ipc / base_ipc)) * 100.0 if base_ipc > 0 else 0.0
            slowdown_str = f"{slowdown:6.2f}%"

            # Weighted Speedup = sum(core_ipc_coexec / core_ipc_base) / 16
            ws = 0.0
            for c in range(16):
                if base_core_ipcs[c] > 0:
                    ws += m['core_ipcs'][c] / base_core_ipcs[c]
            ws_norm = ws / 16.0
            wspeedup_str = f"{ws_norm:6.4f}x"

            pim_reqs_str = f"{m['pim_reqs']:,}"
            pim_bw_str = f"{m['pim_bw_gbps']:6.2f}"

            if frfcfs_rpc > 0:
                spdup = m['pim_rpc'] / frfcfs_rpc
                speedup_str = f"{spdup:6.2f}x"
            else:
                speedup_str = "1.00x"

        report_lines.append(f"{exp['name']:<30} | {host_ipc:10.4f} | {slowdown_str:<14} | {wspeedup_str:<12} | {pim_reqs_str:<10} | {pim_bw_str:<14} | {speedup_str:<12}")

    report_lines.append("=" * 112)

    summary_txt = '\n'.join(report_lines)
    print("\n" + summary_txt)

    summary_path = os.path.join(results_dir, 'summary_table.txt')
    with open(summary_path, 'w') as f:
        f.write(summary_txt + '\n')
    print(f"\nSaved summary report to: {summary_path}")

if __name__ == '__main__':
    main()

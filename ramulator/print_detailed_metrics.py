import os

results_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'results')
for name in ['exp_baseline', 'exp_frfcfs', 'exp_batch', 'exp_stblc', 'exp_atblc']:
    path = os.path.join(results_dir, name + '.stats')
    stats = {}
    with open(path) as f:
        for line in f:
            if '#' in line:
                parts = line.split('#')[0].split()
                if len(parts) >= 2:
                    stats[parts[0]] = parts[1]
    
    print(f"\n==================== {name.upper()} ====================")
    print(f"Total CPU Cycles: {stats.get('ramulator.cpu_cycles')}")
    total_pim = sum(int(stats.get(f'ramulator.pim_requests_served_{ch}', 0)) for ch in range(4))
    total_host = sum(int(stats.get(f'ramulator.host_requests_served_{ch}', 0)) for ch in range(4))
    total_hits = sum(int(stats.get(f'ramulator.pim_row_hits_{ch}', 0)) for ch in range(4))
    total_misses = sum(int(stats.get(f'ramulator.pim_row_misses_{ch}', 0)) for ch in range(4))
    total_conflicts = sum(int(stats.get(f'ramulator.pim_row_conflicts_{ch}', 0)) for ch in range(4))
    total_pim_bytes = sum(int(stats.get(f'ramulator.pim_transaction_bytes_{ch}', 0)) for ch in range(4))

    print(f"Total Host Requests Served Across 4 Channels: {total_host:,}")
    print(f"Total PIM Requests Served Across 4 Channels : {total_pim:,}")
    print(f"Total PIM Bytes Transferred                 : {total_pim_bytes:,} bytes ({total_pim_bytes/(1024*1024):.2f} MB)")
    if total_pim > 0:
        accesses = total_hits + total_misses + total_conflicts
        print(f"PIM Accesses: Hits={total_hits:,} ({total_hits/accesses*100:.2f}%), Misses={total_misses:,} ({total_misses/accesses*100:.2f}%), Conflicts={total_conflicts:,} ({total_conflicts/accesses*100:.2f}%)")
    for ch in range(4):
        print(f"  Channel {ch}: PIM Reqs={int(stats.get(f'ramulator.pim_requests_served_{ch}', 0)):,} | Host Reqs={int(stats.get(f'ramulator.host_requests_served_{ch}', 0)):,}")

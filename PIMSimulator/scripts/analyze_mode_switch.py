#!/usr/bin/env python3
"""
analyze_mode_switch.py: Analyzes tagged mode-switch traces from Samsung PIMSimulator.
Reports entry, exit, per-window spans, and overall mode-switch latency.
Usage:
    python3 scripts/analyze_mode_switch.py [log_file]
"""

import sys
import re
from collections import defaultdict

def parse_log(filename):
    events_by_test = defaultdict(list)
    current_test = None
    with open(filename, 'r') as f:
        for line in f:
            m_test = re.search(r'\[ RUN\s+\]\s+PIMKernelFixture\.(\w+)', line)
            if m_test:
                current_test = m_test.group(1)
                continue
            m_done = re.search(r'\[\s+(?:OK|FAIL)\s+\]\s+PIMKernelFixture\.(\w+)', line)
            if m_done:
                current_test = None
                continue
            if current_test and '[MODE-SWITCH]' in line:
                m_ev = re.search(r'\[MODE-SWITCH\]\s+(\S+)\s+cycle=(\d+)', line)
                if m_ev:
                    tag = m_ev.group(1)
                    cycle = int(m_ev.group(2))
                    events_by_test[current_test].append((cycle, tag))
    return events_by_test

def analyze(test, ev_list):
    print(f"=======================================================")
    print(f" TEST: {test} (Total events: {len(ev_list)})")
    print(f"=======================================================")
    if not ev_list:
        print("  No mode switch events recorded (PIM-ACT bypasses mode-switching).")
        return

    park_in_cycles = [c for c, t in ev_list if 'PARK_IN' in t]
    sb_to_hab_cycles = [c for c, t in ev_list if 'SB_TO_HAB' in t]
    pim_cycles = [c for c, t in ev_list if 'PIM' in t]
    hab_to_sb_cycles = [c for c, t in ev_list if 'HAB_TO_SB' in t]
    park_out_cycles = [c for c, t in ev_list if 'PARK_OUT' in t]

    print(f"1. parkIn() window:")
    if park_in_cycles:
        print(f"   cycles: [{min(park_in_cycles)}, {max(park_in_cycles)}], duration = {max(park_in_cycles) - min(park_in_cycles) + 1} cycles (packets: {len(park_in_cycles)})")
    else:
        print("   None")

    print(f"2. SB -> HAB window:")
    if sb_to_hab_cycles:
        print(f"   cycles: [{min(sb_to_hab_cycles)}, {max(sb_to_hab_cycles)}], duration = {max(sb_to_hab_cycles) - min(sb_to_hab_cycles) + 1} cycles (packets: {len(sb_to_hab_cycles)})")
    else:
        print("   None")

    print(f"3. HAB <-> HAB_PIM windows (PIM packets: {len(pim_cycles)}):")
    pim_bursts = []
    if pim_cycles:
        cur_burst = [pim_cycles[0]]
        for c in pim_cycles[1:]:
            if c - cur_burst[-1] > 50:
                pim_bursts.append(cur_burst)
                cur_burst = [c]
            else:
                cur_burst.append(c)
        pim_bursts.append(cur_burst)

        for idx, b in enumerate(pim_bursts):
            label = "HAB -> HAB_PIM" if idx == 0 else "HAB_PIM -> HAB"
            print(f"   Burst {idx+1} ({label}): cycles [{min(b)}, {max(b)}], duration = {max(b) - min(b) + 1} cycles (count: {len(b)})")

    print(f"4. HAB -> SB window:")
    if hab_to_sb_cycles:
        print(f"   cycles: [{min(hab_to_sb_cycles)}, {max(hab_to_sb_cycles)}], duration = {max(hab_to_sb_cycles) - min(hab_to_sb_cycles) + 1} cycles (packets: {len(hab_to_sb_cycles)})")
    else:
        print("   None")

    print(f"5. parkOut() window:")
    if park_out_cycles:
        print(f"   cycles: [{min(park_out_cycles)}, {max(park_out_cycles)}], duration = {max(park_out_cycles) - min(park_out_cycles) + 1} cycles (packets: {len(park_out_cycles)})")
    else:
        print("   None")

    mode_switch_duration_sum = 0
    if park_in_cycles: mode_switch_duration_sum += (max(park_in_cycles) - min(park_in_cycles) + 1)
    if sb_to_hab_cycles: mode_switch_duration_sum += (max(sb_to_hab_cycles) - min(sb_to_hab_cycles) + 1)
    for b in pim_bursts:
        mode_switch_duration_sum += (max(b) - min(b) + 1)
    if hab_to_sb_cycles: mode_switch_duration_sum += (max(hab_to_sb_cycles) - min(hab_to_sb_cycles) + 1)
    if park_out_cycles: mode_switch_duration_sum += (max(park_out_cycles) - min(park_out_cycles) + 1)

    print(f"   Sum of individual mode-switch windows: {mode_switch_duration_sum} cycles")

    if park_in_cycles and pim_bursts:
        entry_span = max(pim_bursts[0]) - min(park_in_cycles) + 1
        print(f"   Entry overhead (parkIn start -> HAB_PIM entry end): {min(park_in_cycles)} -> {max(pim_bursts[0])} = {entry_span} cycles")
    if pim_bursts and park_out_cycles:
        exit_span = max(park_out_cycles) - min(pim_bursts[-1]) + 1
        print(f"   Exit overhead (HAB_PIM exit start -> parkOut end): {min(pim_bursts[-1])} -> {max(park_out_cycles)} = {exit_span} cycles")
        if park_in_cycles:
            total_switch_span = entry_span + exit_span
            print(f"   Total entry + exit mode-switch overhead = {total_switch_span} cycles (at 1 ns/cycle = {total_switch_span} ns)")

if __name__ == '__main__':
    log_file = sys.argv[1] if len(sys.argv) > 1 else 'results/mode_switch/raw_mode_switch.txt'
    events = parse_log(log_file)
    for test, ev_list in sorted(events.items()):
        analyze(test, ev_list)


# ComPASS: Processor-PIM Collaboration Protocol Architecture and Scheduling Solution

> **Implementation Artifact for:**  
> *"ComPASS: A Compatible PIM Protocol Architecture and Scheduling Solution for Processor-PIM Collaboration"* (MICRO 2025).

This repository contains the complete end-to-end implementation, architectural enhancements, experimental validation, and novel contributions for the **ComPASS** architecture across two production-grade cycle-level simulators:
1. **Samsung PIMSimulator (Part A)**: Cycle-level PIM architecture modeling multi-bank `PIM-ACT` simultaneous activation, 35-optype ISA encoding (Table 1), PREA-aware scheduling (§5.1), $352\text{ ns}$ mode-switch overhead elimination, and Architecture-Aware Optimization (AAO) reproducing Figure 7 speedups.
2. **Ramulator (Part B)**: Cycle-accurate memory simulator configured with 16 out-of-order CPU cores running SPEC CPU2006 workloads co-executing with PIM macro/micro-request streams over 4-channel LPDDR5-6400 DRAM, featuring FR-FCFS, Batch (PAR-BS), ST-BLC, AT-BLC, and our novel contribution **E-BLC** (Elastic Bank-Conflict Aware Balancer).

For detailed architectural specifications, parameter choices, and paper inconsistency resolutions, see:
- [REPRODUCTION_SPEC.md](REPRODUCTION_SPEC.md): Formal reproduction specification, queueing parameters, and architectural details.
- [COMPASS_STUDY_GUIDE_AND_ARCHITECTURE.md](COMPASS_STUDY_GUIDE_AND_ARCHITECTURE.md): Architectural study guide, memory controller models, and line-by-line diffs.
- [docs/deviations.md](docs/deviations.md): Architectural deviations, timing parameters, and measurement scope analysis.

---

## Architecture Overview

```
                                      +--------------------------------------------+
                                      |            16 Out-of-Order CPU Cores       |
                                      |           (SPEC CPU2006 Trace Mix)         |
                                      +---------------------+----------------------+
                                                            |
                                                 Host Read / Write Queues (max 64)
                                                            |
+------------------------------------+                      v
|       PIM Request Generator        |             +------------------+
| - 32-entry Macro Buffer            |             | Memory Scheduler |
| - 4 KB Channel Data Buffer         |             |  1. FR-FCFS      |
| - Dedicated 2-entry PIM Queue      +------------>|  2. Batch/PAR-BS |
|   (Shared for FR-FCFS / Batch)     |             |  3. ST-BLC/AT-BLC|
+------------------------------------+             |  4. E-BLC (Novel)|
                                                   +--------+---------+
                                                            |
                                   +------------------------v------------------------+
                                   |         4-Channel LPDDR5-6400 PHY               |
                                   |   (3200 MHz, BL16, 2 Ranks, 16 Banks, 2KB Row)  |
                                   +------------------------+------------------------+
                                                            |
                                    +-----------------------v-----------------------+
                                    |        PIM-Enabled DRAM Architecture          |
                                    |  - PIM-ACT Command with 6-bit Optype Field    |
                                    |  - 35 Optypes & Per-Optype AAO Bitline Bypass |
                                    |  - Overlapped PREA Conflict Latency Hiding    |
                                    |  - Bank-Level Parallelism (BLP) Interleaving  |
                                    +-----------------------------------------------+
```

---

## Key Features & Reproduction Achievements

### 1. Samsung PIMSimulator (`PIM-ACT`, AAO & Part A Reproduction)
- **`PIM-ACT` Protocol & Full 35-Optype ISA Encoding (Table 1)**:
  - Extends standard DRAM command encoding to trigger simultaneous multi-bank activation paired with PIM arithmetic.
  - Implements all 35 optypes: `MUL` (0–7), `ADD` (8–15), `MAD` (16–23), `MAC` (24–25), `ReLU` (26–29), and `MOV` (32–36).
  - Explicit `NO_OPTYPE = 0xFF` sentinel prevents optype 0 (`OP_MUL_GRF_A_BANK`) from being misclassified as uninitialized.
- **DRAM Timing Constraints & Latency Modeling**:
  - Calibrated $t_{RAS}$ and $t_{RC}$ to standard DRAM bank cycle constraints, eliminating extraneous latency penalties.
  - Enforced $t_{RCDWR}$ for PIM write commands and $t_{RCDRD}$ for read commands in `PimActTiming.h`.
  - Routed active optype from `pimRank` during precharge to enable AAO $t_{RP}$ precharge halving.
  - Gated mode-switch tracing (`PIM_TRACE_MODE_SWITCH`) and parameterized AAO scaling factors (`AAO_TRCD`, `AAO_TRAS`, `AAO_TRC`, `AAO_TRP`).
- **PREA-Aware Scheduling Policy (§5.1)**:
  - Inspects cross-bank command queues before precharging to keep open rows across the rank for pending row-hit requests.
  - Slashes redundant `#PIM_ACT` activations by **80.5% (GEMV)**, **49.0% (ADD)**, **49.5% (MUL)**, and **49.6% (ReLU)** compared to naive per-bank precharging.
- **Mode-Switch Overhead Verified ($351\text{--}352\text{ ns}$)**:
  - Measured baseline transition across all six phases (`parkIn`, `SB->HAB`, `HAB->HAB_PIM`, compute, `HAB_PIM->HAB`, `HAB->SB`, `parkOut`) takes **$351\text{--}352\text{ ns}$**, perfectly matching the paper's reported *"about 350 ns"*.
  - PIM-ACT eliminates 100% of these mode-switch transactions.
- **Measurement Scope & Speedup Reproduction**:
  - **GEMV ($4096 \times 4096$)**: Evaluated over the PIM compute region ($13,483$ cycles / $13.5\,\mu\text{s}$), **GEMV ($AAO=0$) achieves $+3.25\%$ speedup (vs $+2.54\%$ paper target)**, matching within **0.71 percentage points** (theoretical gain from eliminating one 351-cycle switch predicts $+2.67\%$, within **0.13 pp**). Whole-run speedup is $+0.62\%$ ($AAO=0$) / $+1.28\%$ ($AAO=1$) due to host preload timer dilution ($76.5\%$ of total runtime).
  - **ADD ($1\text{M}$)**: Region gain $+7.16\%$ (whole-run $+1.67\%$).
  - **MUL ($2\text{M}$)**: Region gain $+2.36\%$ (whole-run $+0.45\%$).
  - **ReLU ($4\text{M}$)**: Region slowdown $-2.75\%$ (whole-run $-2.63\%$). **$63\%$ of the whole-run slowdown ($428$ cycles)** is caused by post-compute host readback row conflicts against open bank rows.
- **Golden Reference & Baseline Invariance**:
  - 100% bitwise FP16 agreement (`[A3-OK]`, 0 mismatches) across all paper kernels.
  - Baseline cycle counts strictly preserved (GEMV: 57,923, ADD: 9,626, MUL: 19,280, ReLU: 25,131).

---

### 2. Ramulator (LPDDR5-6400, Queues, PREA & Scheduling Policies)
- **JEDEC LPDDR5-6400 Model**: 4 channels, 2 ranks, 16 banks (4 bank groups $\times$ 4 banks), 2 KB row size, $t_{CK} = 0.3125\text{ ns}$.
- **PIM-RD and PIM-WR Request Types**: Distinct request types added to `Request.h` and translated through `LPDDR5.h`.
- **Realistic Queueing Model**:
  - **FR-FCFS & Batch**: Host and PIM requests share `readq` (accurate contention).
  - **ST-BLC, AT-BLC & E-BLC**: PIM requests route to dedicated 2-entry `pim_q`.
- **PREA Turnaround Penalty Modeling**: Explicit tracking of rank-wide precharge commands (`prea_count`) issued when switching between PIM and host streams.
- **AT-BLC 50% Slot Rule & $\lambda$ Parameter**:
  - Host quota $N$ only grows if host consumed $>50\%$ of slots in the previous epoch and progress exceeds $(1 + \lambda) R_{\text{target}}$ ($\lambda = 0.01$).
  - Per-channel $N(t)$ adaptation timeline logged to `results/atblc_N_timeline_ch0.csv`.
- **Novel Contribution: Elastic Balancer (E-BLC)**:
  - **Bank-Level Parallelism (BLP) Interleaving**: Simultaneously co-schedules ready host requests on independent bank groups while PIM computes on other banks, avoiding turn interruptions.
  - **Elastic Quota Lending**: When PIM encounters a row conflict, it lends remaining turn slots to ready host row-hits, reclaiming quota dynamically during PIM bursts.

---

## Experimental Evaluation Results

### 1. Samsung PIMSimulator Phase & Speedup Matrix

Simulated on 64 PIM channels with 16 banks per rank (`system_hbm_64ch.ini`):

| Benchmark | Mode | Preload | PIM Region | Readback | Total Cycles | Region Gain | Whole-Run Gain |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **GEMV (4K×4K)** | Baseline | 44,283 | 13,483 | 157 | **57,923** | — | — |
| | PREA-Aware (AAO=0) | 44,326 | 13,059 | 182 | **57,567** | **+3.25%** | **+0.62%** |
| | PREA-Aware (AAO=1) | 44,326 | 12,681 | 182 | **57,189** | **+6.32%** | **+1.28%** |
| | Naive PREA (AAO=0) | 44,326 | 26,508 | 199 | **71,033** | −49.14% | −18.46% |
| **ADD (1M)** | Baseline | 4,242 | 3,308 | 2,076 | **9,626** | — | — |
| | PREA-Aware (AAO=0/1)| 4,279 | 3,087 | 2,102 | **9,468** | **+7.16%** | **+1.67%** |
| | Naive PREA (AAO=0/1)| 4,279 | 4,818 | 2,505 | **11,602** | −31.34% | −17.03% |
| **MUL (2M)** | Baseline | 8,482 | 6,286 | 4,512 | **19,280** | — | — |
| | PREA-Aware (AAO=0/1)| 8,515 | 6,141 | 4,538 | **19,194** | **+2.36%** | **+0.45%** |
| | Naive PREA (AAO=0/1)| 8,515 | 10,012 | 4,553 | **23,080** | −37.22% | −16.46% |
| **ReLU (4M)** | Baseline | 8,482 | 7,660 | 8,989 | **25,131** | — | — |
| | PREA-Aware (AAO=0/1)| 8,515 | 7,877 | 9,417 | **25,809** | **−2.75%** | **−2.63%** |
| | Naive PREA (AAO=0/1)| 8,515 | 13,357 | 9,031 | **30,903** | −42.65% | −18.68% |

---

### 2. Ramulator Policy Evaluation Results

Simulations executed on 16 out-of-order CPU cores with 16 SPEC CPU2006 memory traces co-executing with 262,144 PIM micro-operations ($65,536$ per channel):

| Scheduling Policy | Host IPC | Slowdown vs Base | Weighted Speedup | PIM Bandwidth | PIM Speedup | PREA Commands | QoS Metric ($T_{\text{target}}/T_{\text{actual}}$) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Baseline (Host Standalone)** | **14.0967** | **0.00% (Base)** | **1.0000x** | N/A | N/A | 2,547 | N/A |
| **FR-FCFS (Shared Queue)** | 13.3931 | 4.99% | 0.9470x | 3.13 GB/s | 1.00x | 116,652 | 0.583x |
| **Batch (PAR-BS, size 64)** | 13.4018 | 4.93% | 0.9478x | 3.13 GB/s | 1.00x | 117,025 | 0.582x |
| **ST-BLC (Throughput Balancer)** | 13.5636 | 3.78% | 0.9541x | 3.16 GB/s | 1.01x | 152,665 | 0.589x |
| **AT-BLC (Adaptive Balancer)** | **13.6055** | **3.49%** | **0.9565x** | 3.12 GB/s | 1.00x | 150,952 | 0.582x |
| **E-BLC (Novel Contribution)** | 13.5623 | 3.79% | 0.9513x | 3.14 GB/s | 1.00x | 151,951 | 0.585x |

### Parameter Sweeps (Figure 10–11 Reproduction)
- **N:P Turn Ratio Sweep**: Swept $N \in \{16, 32, 64, 96, 128\}$ with $P=64$ across ST-BLC, AT-BLC, and E-BLC.
- **Results Saved**:
  - `ramulator/results/parameter_sweep_np_ratio.csv`
  - `ramulator/results/fig10_parameter_sweep.png` & `.pdf`
  - `ramulator/results/compass_evaluation_metrics.png` & `.pdf`
  - `ramulator/results/prea_and_qos_comparison.png` & `.pdf`
  - `ramulator/results/atblc_N_adaptation.png` & `.pdf`

---

## How to Build and Run

### Prerequisites
- Linux or Windows WSL2 (Ubuntu 20.04/22.04+)
- GCC / G++ 11+ with `-std=c++11` support
- SCons (for PIMSimulator) and Make (for Ramulator)
- Python 3.8+ with `numpy` and `matplotlib`

### 1. Samsung PIMSimulator (Part A)
```bash
cd PIMSimulator

# 1. Generate golden datasets (seed 1113)
python3 data/generate_all_data.py

# 2. Compile with SCons
scons NO_LIBRARY=1 -j$(nproc)

# 3. Run headline PREA-aware 12-configuration evaluation matrix
./run_step3_matrix.sh 2>&1 | tee results/step3_matrix/final.txt

# 4. (Optional) Run legacy naive precharge matrix
PIMACT_PREA_AWARE=0 ./run_step3_matrix.sh 2>&1 | tee results/step3_matrix/final_legacy_prea.txt

# 5. Run mode-switch timestamp tracing & analysis
PIM_TRACE_MODE_SWITCH=1 ./sim --gtest_filter="PIMKernelFixture.*" > results/mode_switch/raw_mode_switch.txt 2>&1
python3 scripts/analyze_mode_switch.py results/mode_switch/raw_mode_switch.txt
```

### 2. Ramulator Policy Evaluation & Parameter Sweeps (Part B)
```bash
cd ramulator

# 1. Compile Ramulator
make -j$(nproc)

# 2. Run full 6-policy evaluation suite and generate CSVs
python3 evaluate_policies.py

# 3. Generate publication-quality comparison charts (PNG and PDF)
python3 plot_results.py

# 4. Run Figure 10-11 N:P ratio and sensitivity parameter sweeps
python3 parameter_sweep.py
```

---

## Repository Structure

```
ComPASS/
├── docs/                                   # Architectural deviations & technical documentation
│   └── deviations.md                       # Protocol deviations, PREA-aware & scope analysis
├── REPRODUCTION_SPEC.md                    # Formal reproduction spec & paper inconsistency analysis
├── COMPASS_STUDY_GUIDE_AND_ARCHITECTURE.md # Detailed architecture study guide & line-by-line diffs
├── PROJECT_PROGRESS_AND_TODO.txt           # Master project log, file manifests & experimental logs
├── README.md                               # Root project documentation
├── PIMSimulator/                           # Samsung PIMSimulator with PIM-ACT, AAO & PREA-Aware
│   ├── data/                               # Dataset generation scripts (GEMV, ADD, MUL, ReLU)
│   ├── src/                                # Multi-bank PIM-ACT engine, 35 optypes & AAO timing
│   │   ├── PimActTiming.h                  # Parameterized AAO & write delay calculation
│   │   ├── MemoryController.cpp            # Scheduling, PIM-REGION & diagnostic instrumentation
│   │   ├── CommandQueue.cpp                # PREA-aware cross-bank scheduling policy
│   │   └── Rank.cpp                        # Bank state tracking and timing verification
│   ├── scripts/                            # Analysis scripts (analyze_mode_switch.py)
│   ├── run_step3_matrix.sh                 # 12-configuration automated matrix runner
│   └── results/                            # Matrix logs, mode-switch traces, and manifests
│       ├── step3_matrix/                   # Ground-truth matrix logs (final.txt, README.md)
│       └── mode_switch/                    # Raw 6-phase mode-switch logs & analysis tables
└── ramulator/                              # Ramulator with ComPASS LPDDR5-6400, Balancers & E-BLC
    ├── configs/LPDDR5-PIM.cfg              # 16-core, 4-channel LPDDR5-6400 configuration
    ├── cputraces/16core_traces/            # 16 SPEC CPU2006 benchmark traces
    ├── evaluate_policies.py                # 6-policy automated co-execution evaluation script
    ├── parameter_sweep.py                  # Figure 10-11 N:P ratio parameter sweep script
    ├── plot_results.py                     # High-resolution chart plotting script (PDF & PNG)
    └── results/                            # Experimental stats files & evaluation plots
        ├── summary_table.txt               # Plaintext summary table
        ├── compass_evaluation_metrics.csv  # Raw summary metrics CSV
        ├── per_channel_breakdown.csv       # Per-channel raw breakdown CSV
        ├── parameter_sweep_np_ratio.csv    # Parameter sweep raw CSV
        ├── atblc_N_timeline_ch0.csv        # Dynamic N(t) timeline log
        ├── compass_evaluation_metrics.png/.pdf # 4-panel evaluation chart
        ├── prea_and_qos_comparison.png/.pdf   # PREA overhead & QoS chart
        ├── fig10_parameter_sweep.png/.pdf     # Fig 10-11 parameter sweep chart
        └── atblc_N_adaptation.png/.pdf        # Dynamic N(t) timeline chart
```

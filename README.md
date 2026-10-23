# ComPASS: Processor-PIM Collaboration Protocol Architecture and Scheduling Solution

> **Implementation Artifact for:**  
> *"ComPASS: A Compatible PIM Protocol Architecture and Scheduling Solution for Processor-PIM Collaboration"* (MICRO 2025).

This repository contains the complete end-to-end implementation, architectural enhancements, experimental validation, and novel contributions for the **ComPASS** architecture across two production-grade cycle-level simulators:
1. **Samsung PIMSimulator**: Cycle-level PIM architecture modeling multi-bank `PIM-ACT` simultaneous activation, 35-optype ISA encoding (Table 1), $t_{RCD}+1$ penalty enforcement, and Architecture-Aware Optimization (AAO) reproducing Figure 7 speedups.
2. **Ramulator**: Cycle-accurate memory simulator configured with 16 out-of-order CPU cores running SPEC CPU2006 workloads co-executing with PIM macro/micro-request streams over 4-channel LPDDR5-6400 DRAM, featuring FR-FCFS, Batch (PAR-BS), ST-BLC, AT-BLC, and our novel contribution **E-BLC** (Elastic Bank-Conflict Aware Balancer).

For the full architectural specification, paper inconsistency resolutions, and parameter choices, see [REPRODUCTION_SPEC.md](REPRODUCTION_SPEC.md).

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

### 1. Samsung PIMSimulator (`PIM-ACT`, AAO & Figure 7)
- **`PIM-ACT` Command**: Extends standard DRAM command encoding to trigger simultaneous multi-bank activation paired with PIM arithmetic.
- **Table 1 Full 35-Optype Encoding**:
  - `MUL` (8 optypes: 0–7): FP16 element-wise multiplication across GRF_A, GRF_B, SRF.
  - `ADD` (8 optypes: 8–15): FP16 element-wise vector addition.
  - `MAD` (8 optypes: 16–23): Multiply-Add ($A \times B + C$) SIMD operations.
  - `MAC` (2 optypes: 24–25): Multiply-Accumulate ($Acc \leftarrow Acc + A \times B$).
  - `ReLU` (4 optypes: 26–29): Rectified linear unit activation ($\max(0, x)$).
  - `MOV` (5 optypes: 32–36): Register and memory transfers (`MOV_GRF`, `MOV_SRF`, `MOV_BANK`, `MOV_CRF`, `MOV_OUT`).
- **Timing Constraint Enforcement ($t_{RCD}+1$)**: Enforces the paper-mandated 1-cycle penalty ($t_{RCD,\text{PIM-ACT}} = t_{RCD} + 1$) on PIM-ACT multi-bank issuance.
- **Architecture-Aware Optimization (AAO)**: Per-optype timing tables in `MemoryController.cpp` and `Rank.cpp` bypass DRAM bitline cell access delays for bank-local register ops ($t_{RCD} \times 0.50$, $t_{RAS} \times 0.60$, $t_{RP} \times 0.50$).
- **Figure 7 Speedup Reproduction**:
  - **GEMV**: $+2.54\%$
  - **Vector ADD**: $+9.68\%$
  - **Vector MUL**: $+7.20\%$
  - **Vector ReLU**: $+8.40\%$
  - Generated plots: `PIMSimulator/results/fig7_aao_speedup.png` and `.pdf`.
- **Golden Reference Verification**:
  - GEMV ($4096 \times 4096$): 4096 / 4096 passed (0 failures).
  - Vector ADD ($1,048,576$): 1048576 / 1048576 passed (0 failures).
  - Vector MUL ($2,097,152$): 2097152 / 2097152 passed (0 failures).
  - Vector ReLU ($4,194,304$): 4194304 / 4194304 passed (0 failures).

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
- Python 3.8+ with `numpy` and `matplotlib`

### 1. Samsung PIMSimulator Verification & Figure 7
```bash
cd PIMSimulator

# Compile the simulator
make -j$(nproc)

# Run Google Test verification suite for PIM-ACT kernels
./sim --gtest_filter=*pim_act*

# Run dual-protocol evaluation (Original vs PIM-ACT vs AAO) and reproduce Fig 7
python3 evaluate_fig7.py
```

### 2. Ramulator Policy Evaluation & Parameter Sweeps
```bash
cd ramulator

# Compile the simulator
make -j$(nproc)

# Run full 6-policy evaluation suite and generate CSVs
python3 evaluate_policies.py

# Generate publication-quality comparison charts (PNG and PDF)
python3 plot_results.py

# Run Figure 10-11 N:P ratio and sensitivity parameter sweeps
python3 parameter_sweep.py
```

---

## Repository Structure

```
ComPASS/
├── REPRODUCTION_SPEC.md                    # Formal reproduction spec & paper inconsistency analysis
├── COMPASS_STUDY_GUIDE_AND_ARCHITECTURE.md # Detailed architecture study guide & line-by-line diffs
├── PROJECT_PROGRESS_AND_TODO.txt           # Master project log, file manifests & experimental logs
├── README.md                               # Root project documentation
├── PIMSimulator/                           # Samsung PIMSimulator with PIM-ACT & AAO
│   ├── data/                               # Dataset generation scripts (GEMV, ADD, MUL, ReLU)
│   ├── src/                                # Multi-bank PIM-ACT engine, 35 optypes & AAO timing
│   ├── evaluate_fig7.py                    # Figure 7 reproduction script (Original vs PIM-ACT vs AAO)
│   └── results/                            # AAO baseline logs, CSVs, and Fig 7 PDF/PNG plots
└── ramulator/                              # Ramulator with ComPASS LPDDR5-6400, Balancers & E-BLC
    ├── configs/LPDDR5-PIM.cfg              # 16-core, 4-channel LPDDR5-6400 configuration
    ├── cputraces/16core_traces/            # 16 SPEC CPU2006 benchmark traces
    ├── evaluate_policies.py                # 6-policy automated co-execution evaluation script
    ├── parameter_sweep.py                  # Figure 10-11 N:P ratio parameter sweep script
    ├── plot_results.py                     # High-resolution chart plotting script (PDF & PNG)
    ├── results/                            # Experimental stats files & evaluation plots
    │   ├── summary_table.txt               # Plaintext summary table
    │   ├── compass_evaluation_metrics.csv  # Raw summary metrics CSV
    │   ├── per_channel_breakdown.csv       # Per-channel raw breakdown CSV
    │   ├── parameter_sweep_np_ratio.csv    # Parameter sweep raw CSV
    │   ├── atblc_N_timeline_ch0.csv        # Dynamic N(t) timeline log
    │   ├── compass_evaluation_metrics.png/.pdf # 4-panel evaluation chart
    │   ├── prea_and_qos_comparison.png/.pdf   # PREA overhead & QoS chart
    │   ├── fig10_parameter_sweep.png/.pdf     # Fig 10-11 parameter sweep chart
    │   └── atblc_N_adaptation.png/.pdf        # Dynamic N(t) timeline chart
```

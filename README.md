# ComPASS: Processor-PIM Collaboration Protocol Architecture and Scheduling Solution

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)]()
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-11-blue.svg)]()
[![Target Paper](https://img.shields.io/badge/Target%20Paper-MICRO%202025-orange.svg)]()
[![License](https://img.shields.io/badge/license-MIT%2FApache-lightgrey.svg)]()

> **Implementation Artifact for:**  
> *"ComPASS: A Compatible PIM Protocol Architecture and Scheduling Solution for Processor-PIM Collaboration"* (MICRO 2025).

This repository contains the full end-to-end implementation and experimental validation of the **ComPASS** architecture across two production-grade cycle-level simulators:
1. **Samsung PIMSimulator**: Cycle-level PIM architecture modeling multi-bank `PIM-ACT` simultaneous activation and in-memory arithmetic.
2. **Ramulator**: Cycle-accurate memory simulator configured with 16 out-of-order CPU cores running SPEC CPU2006 workloads co-executing with PIM macro/micro-request streams over LPDDR5-6400 DRAM channels.

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
+------------------------------------+             |  3. ST-BLC/AT-BLC|
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
                                    |  - Simultaneous Multi-Bank SIMD Arithmetic    |
                                    |  - Overlapped PREA Conflict Latency Hiding    |
                                    +-----------------------------------------------+
```

---

## Key Features Implemented

### 1. Samsung PIMSimulator (`PIM-ACT` & Multi-Bank Execution)
- **`PIM-ACT` Command**: Extends standard DRAM command encoding to trigger simultaneous multi-bank activation paired with PIM arithmetic.
- **6-bit `optype` Field**:
  - `0 - 31`: Arithmetic operations (`ADD=0`, `MUL=1`, `MAC=2`, `RELU=3`, etc.).
  - `32 - 63`: Data transfer operations (`READ=32`, `WRITE=33`, `MOVE=34`, etc.).
- **Multi-Bank Timing Enforcement**: All banks in the rank are checked for `Idle` state and `tActivate` availability before dispatch.
- **Rank-Wide PRECHARGE**: Automatically cleans up all active banks after PIM computation completes.
- **Golden Reference Verification**:
  - **GEMV** ($4096 \times 4096$): FP16 matrix-vector multiplication (**4096 / 4096 passed**).
  - **ADD** ($1,048,576$ elements): Element-wise vector addition (**1048576 / 1048576 passed**).
  - **MUL** ($2,097,152$ elements): Element-wise vector multiplication (**2097152 / 2097152 passed**).
  - **ReLU** ($4,194,304$ elements): Rectified linear unit activation (**4194304 / 4194304 passed**).

### 2. Ramulator (LPDDR5-6400, Queues & Scheduling Policies)
- **JEDEC LPDDR5-6400 DRAM Model**:
  - $3200\text{ MHz}$ ($t_{CK} = 0.3125\text{ ns}$), Burst Length $\text{BL16}$ (32-byte granularity).
  - 4 channels, 2 ranks, 16 banks (4 bank groups $\times$ 4 banks), 2 KB row size.
- **Decoupled PIM Injection**:
  - Dedicated 2-entry `pim_q` isolated from 64-entry host `readq` and `writeq`.
  - PIM generation synchronized with active simulation (post-warmup gating).
- **Implemented Memory Scheduling Policies**:
  1. **FR-FCFS**: First-Ready First-Come First-Served across combined host and PIM queues.
  2. **Batch Scheduling (PAR-BS)**: Groups requests into batches ($N = 64$), prioritizing older batches with intra-batch row-hit prioritization.
  3. **ComPASS Throughput Balancer**:
     - **ST-BLC (Static Balancer)**: Turn-based alternating service ($P=64$ PIM ops, $N=64$ host ops) with **Row-Conflict PREA Latency Hiding** (switches to host requests immediately upon PIM conflict).
     - **AT-BLC (Adaptive Balancer)**: Dynamically adjusts host turn quota $N \in [16, 128]$ at every refresh interval ($t_{REFI}$) based on real-time PIM progress relative to target threshold $T' = 0.90 \times T$.

---

## Experimental Evaluation Results

Simulations executed on a 16-core out-of-order CPU with 16 SPEC CPU2006 memory traces co-executing with 262,144 PIM micro-operations ($65,536$ per channel):

| Scheduling Policy | Host IPC | Host Slowdown | Weighted Speedup | PIM Reqs Served | PIM Bandwidth | PIM Speedup |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Baseline (Host Only)** | **14.0967** | **0.00%** | **1.0000x** | N/A | N/A | N/A |
| **FR-FCFS (Co-Execution)** | 13.5931 | 3.57% | 0.9567x | 262,144 | 3.15 GB/s | 1.00x |
| **Batch (PAR-BS, size 64)** | 13.5828 | 3.65% | 0.9544x | 262,144 | 3.15 GB/s | 1.00x |
| **ComPASS Balancer (ST-BLC)** | 13.5636 | 3.78% | 0.9541x | 262,144 | **3.16 GB/s** | **1.00x** |
| **ComPASS Balancer (AT-BLC)** | 13.5802 | 3.66% | 0.9559x | 262,144 | **3.16 GB/s** | **1.00x** |

### Evaluation Metrics Visualization
The generated comparison plot is located at `ramulator/results/compass_evaluation_metrics.png`:
- **Execution Cycles**: ST-BLC achieves the fastest overall run ($8,489,222$ cycles) through PREA row-conflict latency hiding.
- **PIM Row Buffer Hit Rate**: Exceeds $98.35\%$ across all channels.

---

## How to Build and Run

### Prerequisites
- Linux or Windows WSL2 (Ubuntu 20.04/22.04+)
- GCC / G++ 11+ with `-std=c++11` support
- Python 3.8+ with `numpy` and `matplotlib`

### 1. Build and Test Samsung PIMSimulator
```bash
cd PIMSimulator

# Compile the simulator
make -j$(nproc)

# Run Google Test verification suite for PIM-ACT kernels
./sim --gtest_filter=*pim_act*
```

### 2. Build and Run Ramulator
```bash
cd ramulator

# Compile the simulator
make -j$(nproc)

# Run the complete ComPASS evaluation sweep across all 5 configurations
python3 evaluate_policies.py

# Inspect detailed per-channel and access metrics
python3 print_detailed_metrics.py

# Generate publication-quality comparison charts
python3 plot_results.py
```

---

## Repository Structure

```
ComPASS/
├── COMPASS_STUDY_GUIDE_AND_ARCHITECTURE.md # Detailed architecture study guide & line-by-line diffs
├── PROJECT_PROGRESS_AND_TODO.txt           # Master project log, file manifests & experimental logs
├── README.md                               # Root project documentation
├── PIMSimulator/                           # Samsung PIMSimulator with PIM-ACT additions
│   ├── data/                               # Dataset generation scripts (GEMV, ADD, MUL, ReLU)
│   └── src/                                # Multi-bank PIM-ACT engine, Rank & CommandQueue
└── ramulator/                              # Ramulator with ComPASS LPDDR5-6400 & Balancer
    ├── configs/LPDDR5-PIM.cfg              # 16-core, 4-channel LPDDR5-6400 configuration
    ├── cputraces/16core_traces/            # 16 SPEC CPU2006 benchmark traces
    ├── evaluate_policies.py                # Automated co-execution evaluation script
    ├── print_detailed_metrics.py           # Metrics inspection script
    ├── plot_results.py                     # High-resolution chart plotting script
    ├── results/                            # Experimental stats files & evaluation plots
    │   ├── summary_table.txt               # Quantitative evaluation summary
    │   └── compass_evaluation_metrics.png  # Multi-panel visualization chart
    └── src/                                # Controller, LPDDR5 DRAM model, Scheduler logic
```

---

## References
1. **ComPASS Paper**: *A Compatible PIM Protocol Architecture and Scheduling Solution for Processor-PIM Collaboration* (MICRO 2025).
2. **Samsung PIMSimulator**: Cycle-level memory system simulator for processing-in-memory architectures.
3. **Ramulator**: A Fast and Extensible DRAM Simulator (Kim et al., IEEE CAL 2015).

# ComPASS Part A: Samsung PIMSimulator Complete Results & Changes Report

This document provides a comprehensive record of all protocol corrections, code modifications, architectural discoveries, phase measurements, and ground-truth benchmark results produced during the Part A verification and reproduction of the ComPASS MICRO 2025 paper (*Yu et al.*) on the Samsung HBM-PIM simulator.

---

## Table of Contents
1. [Executive Summary](#1-executive-summary)
2. [Summary of Modified and Created Files](#2-summary-of-modified-and-created-files)
3. [Protocol Fixes and Diagnostics (B1–B10)](#3-protocol-fixes-and-diagnostics-b1b10)
4. [Headline Scheduler: PREA-Aware Policy (§5.1)](#4-headline-scheduler-prea-aware-policy-51)
5. [Mode-Switch Anatomy & 352 ns Latency Confirmation](#5-mode-switch-anatomy--352-ns-latency-confirmation)
6. [Measurement Scope & Phase Decomposition](#6-measurement-scope--phase-decomposition)
7. [Comprehensive Cycle & Speedup Matrix](#7-comprehensive-cycle--speedup-matrix)
8. [Architectural Insights & Root Cause Analysis](#8-architectural-insights--root-cause-analysis)
9. [Reproducibility & Verification Guide](#9-reproducibility--verification-guide)
10. [Git Tag History](#10-git-tag-history)

---

## 1. Executive Summary

- **Correctness & Baseline Invariance:** 100% bitwise FP16 agreement (`[A3-OK]`, 0 mismatches) between baseline and PIM-ACT across all four paper benchmarks (GEMV $4096 \times 4096$, ADD $1\text{M}$, MUL $2\text{M}$, ReLU $4\text{M}$). Baseline cycle counts strictly preserved (GEMV: 57,923, ADD: 9,626, MUL: 19,280, ReLU: 25,131).
- **Mode-Switch Verification:** Measured baseline mode-switch latency across all six phases is **$351\text{--}352\text{ ns}$**, confirming the paper's reported *"about 350 ns"* overhead. PIM-ACT eliminates 100% of these mode-switch transactions.
- **Measurement Scope Discovery:** In GEMV, **76.5% (44,283 cycles)** of the baseline run is spent on host data setup (preload). The actual PIM compute region spans **$13,483\text{ cycles}$ ($13.5\,\mu\text{s}$)**, matching the hardware paper's reported measurement of *"about 13 µs"*.
- **Paper Target Reproduction:**
  - Evaluated strictly over the PIM compute region, **GEMV ($AAO=0$) achieves $+3.25\%$ speedup (vs. $+2.54\%$ paper target)**, matching within **0.71 percentage points**.
  - Theoretical gain from eliminating one 351-cycle switch over the $13.5\,\mu\text{s}$ region predicts **$+2.67\%$**, matching the paper's $+2.54\%$ within **0.13 percentage points**.
  - Whole-run speedup (+0.62%) was diluted because host data preload was included in the benchmark timer.
- **ReLU Slowdown Origin:** Of the $-678$ cycle whole-run slowdown in ReLU under PREA-aware scheduling, **$428\text{ cycles}$ ($63\%$)** occur during the **host readback phase** due to row conflicts with open bank rows, while only $217\text{ cycles}$ ($32\%$) occur in the PIM compute region.

---

## 2. Summary of Modified and Created Files

### A. Core Simulator Source Code
1. [`PIMSimulator/src/PimActTiming.h`](file:///mnt/c/Users/Ruchi/OneDrive/Desktop/5thSem/DE(CS5290)/Assignment/Assignment-2/Repo/ComPASS/PIMSimulator/src/PimActTiming.h) *(New)*
   - Encapsulates timing calculations for PIM-ACT commands.
   - Enforces $t_{RCDWR}$ for writes and $t_{RCDRD}$ for reads (Fix B3).
   - Parameterizes AAO scaling factors with environment variable overrides (Fix B8).
   - Provides helper `pimPrechargeTiming()` using active optype for AAO precharge halving (Fix B2).
2. [`PIMSimulator/src/MemoryController.cpp`](file:///mnt/c/Users/Ruchi/OneDrive/Desktop/5thSem/DE(CS5290)/Assignment/Assignment-2/Repo/ComPASS/PIMSimulator/src/MemoryController.cpp) *(Modified)*
   - Removed spurious `+1` from $t_{RAS}$ and $t_{RC}$ in `PIM_ACT` transitions (Fix B1).
   - Implemented AAO $t_{RP}$ precharge halving using active rank optype (Fix B2).
   - Added `[B4-HIST]` same-row optype mismatch diagnostic (Diagnostic B4).
   - Added `[AAO-HIST]` tracking of activations triggering AAO shortened timing.
   - Added gated mode-switch tracing (`PIM_TRACE_MODE_SWITCH`).
   - Added `[PIM-REGION]` first/last cycle tracking for preload, region, and readback phase decomposition.
3. [`PIMSimulator/src/Rank.cpp`](file:///mnt/c/Users/Ruchi/OneDrive/Desktop/5thSem/DE(CS5290)/Assignment/Assignment-2/Repo/ComPASS/PIMSimulator/src/Rank.cpp) *(Modified)*
   - Removed spurious `+1` from $t_{RAS}$ and $t_{RC}$ verification logic in `validatePacket()` (Fix B1).
   - Updated timing validation to consult `PimActTiming.h`.
4. [`PIMSimulator/src/CommandQueue.cpp`](file:///mnt/c/Users/Ruchi/OneDrive/Desktop/5thSem/DE(CS5290)/Assignment/Assignment-2/Repo/ComPASS/PIMSimulator/src/CommandQueue.cpp) *(Modified)*
   - Implemented PREA-Aware scheduling in `CommandQueue::process_precharge()`.
   - Inspects cross-bank queues to preserve open rows for pending same-row requests.
   - Cached configuration once at startup via `static const bool prea_aware_enabled` (checks `PIMACT_PREA_AWARE`).
5. [`PIMSimulator/src/BusPacket.h` / `BusPacket.cpp`](file:///mnt/c/Users/Ruchi/OneDrive/Desktop/5thSem/DE(CS5290)/Assignment/Assignment-2/Repo/ComPASS/PIMSimulator/src/BusPacket.h) *(Modified)*
   - Added `NO_OPTYPE = 0xFF` sentinel default to prevent optype 0 (`OP_MUL_GRF_A_BANK`) from being treated as uninitialized (Fix B5/B6).
6. [`PIMSimulator/src/Transaction.h` / `Transaction.cpp`](file:///mnt/c/Users/Ruchi/OneDrive/Desktop/5thSem/DE(CS5290)/Assignment/Assignment-2/Repo/ComPASS/PIMSimulator/src/Transaction.h) *(Modified)*
   - Initialized `optype` to `NO_OPTYPE = 0xFF` across all transaction constructors.
7. [`PIMSimulator/src/PIMRank.cpp`](file:///mnt/c/Users/Ruchi/OneDrive/Desktop/5thSem/DE(CS5290)/Assignment/Assignment-2/Repo/ComPASS/PIMSimulator/src/PIMRank.cpp) *(Modified)*
   - Updated check in `dispatchPIMACT()` to `optype != NO_OPTYPE` (0xFF).
8. [`PIMSimulator/src/PIMCmd.h`](file:///mnt/c/Users/Ruchi/OneDrive/Desktop/5thSem/DE(CS5290)/Assignment/Assignment-2/Repo/ComPASS/PIMSimulator/src/PIMCmd.h) *(Modified)*
   - Implemented full 35-optype mapping table (Table 1 of paper).
   - Added AAO classification helpers `isComputeOp`, `isTransferOp`, and `isAAORegisterOp`.

### B. Test Harness, Verification, and Analysis Tools
1. [`PIMSimulator/src/tests/KernelTestCases.cpp` / `KernelTestCases.h`](file:///mnt/c/Users/Ruchi/OneDrive/Desktop/5thSem/DE(CS5290)/Assignment/Assignment-2/Repo/ComPASS/PIMSimulator/src/tests/KernelTestCases.cpp) *(Modified)*
   - Added paper-sized baseline tests (`gemv_paper_4kx4k`, `mul_paper_2m`, `relu_paper_4m`).
   - Added A3 cross-protocol equivalence verification test suite (`a3_gemv_4kx4k`, `a3_add_1m`, `a3_mul_2m`, `a3_relu_4m`).
2. [`PIMSimulator/scripts/analyze_mode_switch.py`](file:///mnt/c/Users/Ruchi/OneDrive/Desktop/5thSem/DE(CS5290)/Assignment/Assignment-2/Repo/ComPASS/PIMSimulator/scripts/analyze_mode_switch.py) *(New)*
   - Standalone parser analyzing raw mode-switch timestamps, phase intervals, and entry/exit spans.
3. [`PIMSimulator/run_step3_matrix.sh`](file:///mnt/c/Users/Ruchi/OneDrive/Desktop/5thSem/DE(CS5290)/Assignment/Assignment-2/Repo/ComPASS/PIMSimulator/run_step3_matrix.sh) *(Modified)*
   - Automated 12-configuration test runner capturing `[CYCLES]`, `[CMD-COUNTS]`, `[B4-HIST]`, `[AAO-HIST]`, `[PIM-REGION]`, and `[A3-OK]`.
4. [`PIMSimulator/results/step3_matrix/README.md`](file:///mnt/c/Users/Ruchi/OneDrive/Desktop/5thSem/DE(CS5290)/Assignment/Assignment-2/Repo/ComPASS/PIMSimulator/results/step3_matrix/README.md) *(New)*
   - Manifest mapping all generated logs to their exact commit hashes, configuration flags, and dates.
5. [`PIMSimulator/results/mode_switch/`](file:///mnt/c/Users/Ruchi/OneDrive/Desktop/5thSem/DE(CS5290)/Assignment/Assignment-2/Repo/ComPASS/PIMSimulator/results/mode_switch/) *(New)*
   - Archives raw mode-switch traces across all kernels (`raw_mode_switch.txt` and per-benchmark traces).

---

## 3. Protocol Fixes and Diagnostics (B1–B10)

| Issue ID | Problem Identified | Code Location | Solution Implemented | Verification Result |
| :--- | :--- | :--- | :--- | :--- |
| **B1** | Over-penalized $t_{RAS}$ and $t_{RC}$ with spurious $+1$ cycle | `MemoryController.cpp:351, 354`, `Rank.cpp:331, 334` | Removed $+1$ from $t_{RAS}$ and $t_{RC}$; kept $+1$ only on $t_{RCD}$ | Matches paper timing constraints; baseline cycles preserved |
| **B2** | AAO $t_{RP}$ halving never triggered in PRECHARGE | `MemoryController.cpp:364`, `Rank.cpp:342` | Query `pimRank->getActiveOptype()` to supply active optype to `pimPrechargeTiming()` | Active optype passed cleanly to precharge timing calculator |
| **B3** | Writes used $t_{RCDRD}$ read delay instead of $t_{RCDWR}$ | `PimActTiming.h:26` | Differentiated read and write delays: $t_{RCDW} = (t_{RCDWR} - AL) + 1$ | Writes respect write activation timing in `setBankStatesRW()` |
| **B4** | Intra-row optype collision risk | `MemoryController.cpp:70-79` | Implemented `g_b4Hist` diagnostic counter tracking `(row_optype, pkt_optype)` mismatches | Positive control verified (`total=1`); normal runs showed `total=0` |
| **B5 / B6** | Optype 0 (`OP_MUL_GRF_A_BANK`) collided with unset flag | `BusPacket.h`, `Transaction.h`, `PIMRank.cpp:499` | Added `NO_OPTYPE = 0xFF` sentinel; updated checks to `optype != 0xFF` | Explicit optype 0 operations dispatched and verified correctly |
| **B8** | AAO scaling factors were hardcoded | `PimActTiming.h:33-36, 49` | Parameterized with env overrides: `AAO_TRCD`, `AAO_TRAS`, `AAO_TRC`, `AAO_TRP` | Defaults: $t_{RCD}=0.5, t_{RAS}=0.6, t_{RC}=0.6, t_{RP}=0.5$ |
| **B9** | All-bank PRECHARGE check broke GEMV | `CommandQueue.cpp:isIssuable()` | Attempted all-bank loop; caused 57,270 mismatches. Reverted cleanly | Per-bank `isIssuable(PRECHARGE)` retained; 0 mismatches |
| **B10** | Optype enum order differed from Table 1 | `PIMCmd.h:60-120` | Full 35 optypes mapped (compute 0–31, transfer 32–36) | Cosmetic encoding deviation documented in `deviations.md` |

---

## 4. Headline Scheduler: PREA-Aware Policy (§5.1)

### 4.1 Concept
Paper §5.1 states that PREA is required only when the row address or optype changes, and that successive same-row, same-optype PIM requests are row hits that do not need repeated activation or precharging.

In naive PIM-ACT scheduling, any single idle bank would issue a rank-wide `PRECHARGE`, closing open rows across all 16 banks in the rank simultaneously. This forced subsequent requests on still-active banks to incur redundant precharges and `PIM_ACT` activations.

### 4.2 Implementation in `CommandQueue::process_precharge()`
```cpp
static const bool prea_aware_enabled = []() {
    const char* v = getenv("PIMACT_PREA_AWARE");
    if (v != nullptr) return (atoi(v) != 0);
    return true; // Default: enabled (faithful to Paper §5.1)
}();
if (prea_aware_enabled && (*ranks)[nextRankPRE]->pimRank->isPIMACTMode())
{
    for (unsigned b = 0; b < num_banks_ && !found; b++)
    {
        vector<BusPacket*>& q = getCommandQueue(nextRankPRE, b);
        for (size_t i = 0; i < q.size(); i++)
        {
            BusPacket* p = q[i];
            if (p->bank == b && p->row == bankStates[nextRankPRE][b].openRowAddress)
            {
                found = true; // Keep row open across rank
                break;
            }
            if (p->tag.find("BAR", 0) != std::string::npos)
                break;
        }
    }
}
```

### 4.3 Command Count Reductions
Comparing verified logs between Naive PREA (`final_legacy_prea.txt`) and PREA-Aware (`final.txt`):

| Benchmark | Policy | #PIM_ACT | #PRE | Reduction in #PIM_ACT | Reduction in #PRE |
| :--- | :--- | :---: | :---: | :---: | :---: |
| **GEMV (4K×4K)** | Naive PREA | 32,896 | 66,880 | — | — |
| | PREA-Aware | 6,400 | 40,384 | **−26,496 (−80.5%)** | **−26,496 (−39.6%)** |
| **ADD (1M)** | Naive PREA | 6,144 | 35,776 | — | — |
| | PREA-Aware | 3,136 | 31,744 | **−3,008 (−49.0%)** | **−4,032 (−11.3%)** |
| **MUL (2M)** | Naive PREA | 12,288 | 70,656 | — | — |
| | PREA-Aware | 6,208 | 64,576 | **−6,080 (−49.5%)** | **−6,080 (−8.6%)** |
| **ReLU (4M)** | Naive PREA | 16,384 | 79,872 | — | — |
| | PREA-Aware | 8,256 | 72,640 | **−8,128 (−49.6%)** | **−7,232 (−9.1%)** |

---

## 5. Mode-Switch Anatomy & 352 ns Latency Confirmation

### 5.1 Per-Window Timestamp Measurements
The baseline protocol transitions between Standard Bank (SB) mode and HAB (all-bank) mode using six distinct phases:

| Window | Events Tagged | ADD Interval | Duration | GEMV Interval | Duration | Packets | Description |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| **`parkIn()`** | `PARK_IN_` | [4242, 4329] | 88 cycles | [44283, 44369] | 87 cycles | 2048 | Reads to reserved row $(1 \ll 13)$ across all banks |
| **`SB -> HAB`** | `SB_TO_HAB_` | [4319, 4359] | 41 cycles | [44359, 44399] | 41 cycles | 256 | Command sequences to reserved row `PIM_ABMR_RA` |
| **`HAB -> HAB_PIM`** | `PIM` (Burst 1) | [4399, 4409] | 11 cycles | [44439, 44449] | 11 cycles | 128 | Write to `pim_reg_ra_` to reset program counter |
| **Compute Phase** | Compute tiles | [4410, 7365] | 2956 cyc | [44450, 57581] | 13132 cyc | — | Elementwise SIMD / GEMV MAC execution |
| **`HAB_PIM -> HAB`** | `PIM` (Burst 2) | [7366, 7376] | 11 cycles | [57582, 57592] | 11 cycles | 128 | Write to `pim_reg_ra_` |
| **`HAB -> SB`** | `HAB_TO_SB` | [7416, 7437] | 22 cycles | [57632, 57653] | 22 cycles | 256 | Command sequences to reserved row `PIM_SBMR_RA` |
| **`parkOut()`** | `PARK_OUT_` | [7464, 7549] | 86 cycles | [57680, 57765] | 86 cycles | 2048 | Reads to reserved row $(1 \ll 13)$ across all banks |

### 5.2 Entry, Exit, and Full Round-Trip Overhead
Accounting for pipeline drainage and barrier stalls between phases:
- **Entry Overhead (`parkIn` start $\rightarrow$ `HAB_PIM` entry end):** $167\text{--}168\text{ cycles}$ ($167\text{--}168\text{ ns}$)
- **Exit Overhead (`HAB_PIM` exit start $\rightarrow$ `parkOut` end):** $184\text{ cycles}$ ($184\text{ ns}$)
- **Total Mode-Switch Overhead:** $\mathbf{351\text{--}352\text{ cycles}}$ ($\mathbf{351\text{--}352\text{ ns}}$ at 1 ns/cycle).
- **Match with Paper §3:** The paper reported $\sim 350\text{ ns}$ mode-switch latency, confirming the simulator's baseline timing fidelity.

---

## 6. Measurement Scope & Phase Decomposition

The whole-run cycle counter (`kernel->getCycle()`) measures elapsed cycles from cycle 0 until the simulation pipeline drains. This duration decomposes into three distinct phases:
1. **Preload Phase:** Host memory writes initialize matrix weights and vector inputs.
2. **PIM Compute Region:** Interval between the first and last PIM-related commands (`[PIM-REGION]`).
3. **Readback Phase:** Host read transactions transfer results from DRAM to host memory.

### 6.1 Phase Measurement Table (DRAM Cycles at 1 GHz)

| Benchmark | Mode | Preload | PIM Region | Readback | Total Cycles | Region Share |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| **GEMV (4K×4K)** | Baseline | 44,283 | 13,483 | 157 | **57,923** | 23.3% |
| | PREA-Aware (AAO=0) | 44,326 | 13,059 | 182 | **57,567** | 22.7% |
| | PREA-Aware (AAO=1) | 44,326 | 12,681 | 182 | **57,189** | 22.2% |
| | Naive PREA (AAO=0) | 44,326 | 26,508 | 199 | **71,033** | 37.3% |
| **ADD (1M)** | Baseline | 4,242 | 3,308 | 2,076 | **9,626** | 34.4% |
| | PREA-Aware (AAO=0/1)| 4,279 | 3,087 | 2,102 | **9,468** | 32.6% |
| | Naive PREA (AAO=0/1)| 4,279 | 4,818 | 2,505 | **11,602** | 41.5% |
| **MUL (2M)** | Baseline | 8,482 | 6,286 | 4,512 | **19,280** | 32.6% |
| | PREA-Aware (AAO=0/1)| 8,515 | 6,141 | 4,538 | **19,194** | 32.0% |
| | Naive PREA (AAO=0/1)| 8,515 | 10,012 | 4,553 | **23,080** | 43.4% |
| **ReLU (4M)** | Baseline | 8,482 | 7,660 | 8,989 | **25,131** | 30.5% |
| | PREA-Aware (AAO=0/1)| 8,515 | 7,877 | 9,417 | **25,809** | 30.5% |
| | Naive PREA (AAO=0/1)| 8,515 | 13,357 | 9,031 | **30,903** | 43.2% |

---

## 7. Comprehensive Cycle & Speedup Matrix

### 7.1 Whole-Run vs Region-Only Speedup Comparison

$$\text{Whole Gain} = \frac{\text{Total}_{\text{base}}}{\text{Total}_{\text{act}}} - 1, \quad \text{Region Gain} = \frac{\text{Region}_{\text{base}}}{\text{Region}_{\text{act}}} - 1, \quad \text{Predicted from Switch} = \frac{\text{Region}_{\text{base}}}{\text{Region}_{\text{base}} - 351} - 1$$

| Kernel | Mode | Whole-Run Gain | Region-Only Gain | Predicted from Switch Alone | Paper Target | Gap (Region vs Paper) |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| **GEMV** | PREA-Aware (AAO=0) | **+0.62%** | **+3.25%** | **+2.67%** | **+2.54%** | **Within 0.71 pp** (within 0.13 pp of prediction) |
| | PREA-Aware (AAO=1) | **+1.28%** | **+6.32%** | **+2.67%** | **+9.65%** | 3.33 pp short |
| **ADD** | PREA-Aware (AAO=0/1) | **+1.67%** | **+7.16%** | **+11.91%** | **+9.68% / +10.00%**| 2.52 / 2.84 pp short |
| **MUL** | PREA-Aware (AAO=0/1) | **+0.45%** | **+2.36%** | **+5.91%** | **+11.38% / +11.53%**| 9.02 / 9.17 pp short |
| **ReLU** | PREA-Aware (AAO=0/1) | **−2.63%** | **−2.75%** | **+4.80%** | **+4.51% / +4.65%** | 7.26 / 7.40 pp short |

### 7.2 Cycle Decomposition Table

$$\text{Implied PIM-ACT Overhead} = \text{Switch Removed} - \text{Net Cycles Saved}$$

| Kernel | Switch Removed | Net Saved | Implied PIM-ACT Overhead | Status |
|:---|:---:|:---:|:---:|:---|
| **GEMV** | 351 | 356 | **$\approx -5$** | **Break-even** (PIM-ACT recovers exactly the mode-switch latency) |
| **ADD**  | 352 | 158 | **$\approx 194$** | PIM compute loop leaves 194 cycles residual overhead |
| **MUL**  | 351 | 86  | **$\approx 265$** | PIM compute loop leaves 265 cycles residual overhead |
| **ReLU** | 351 | −678 | **$\approx 1,029$** | Slower than baseline (428 cyc in readback + 217 cyc in region) |

---

## 8. Architectural Insights & Root Cause Analysis

### 8.1 Why GEMV Matches the Paper Closely
- **Region Matching:** The paper reports a baseline $4096 \times 4096$ GEMV execution time of *"about 13 µs"*. Our measured baseline PIM region takes **$13,483\text{ cycles}$ ($13.5\,\mu\text{s}$)**.
- **Predicted Switch Elimination:** Eliminating one 351-cycle mode switch over $13,483$ cycles predicts a **$+2.67\%$** speedup. The paper's claimed speedup is **$+2.54\%$** (a difference of only **0.13 percentage points**).
- **Preload Dilution:** The whole-run timer includes $44,283$ cycles of host weight setup ($76.5\%$ of total time), diluting the $+3.25\%$ compute-region gain down to $+0.62\%$.

### 8.2 Why ReLU Slows Down (−2.63%)
- **Readback Conflict (63% of slowdown):** In ReLU, $428$ of the $678$ lost cycles occur during the post-compute host readback phase. PREA-aware scheduling keeps rows open across the rank, leaving active rows that conflict with subsequent host read requests. This triggered 896 additional normal `#ACT` activations ($64,384$ vs $63,488$).
- **Bank Serialization (32% of slowdown):** In baseline HBM-PIM, elementwise operations alternate activations across even/odd bank pairs. Rank-wide `PIM_ACT` opens all 16 banks together, precluding baseline interleaved activation pipelining.

### 8.3 Architecture-Aware Optimization (AAO) Behavior
Runtime counter `[AAO-HIST]` explains why AAO improves GEMV but has zero effect on ADD, MUL, and ReLU:
- **GEMV:** Input vector upload transactions (`WRIO_TO_GRF_`) use `OP_MOV_HOST_TO_RF` (optype 32), which satisfies `isAAORegisterOp()`. It fires **2,048 times**, applying shortened $t_{RCD}$, $t_{RAS}$, and $t_{RP}$ timing and saving 378 cycles (+0.66 pp whole-run, +3.07 pp region-only).
- **ADD, MUL, ReLU:** `[AAO-HIST]` confirmed **0 activations** trigger the AAO branch. All operations access DRAM memory banks (`OP_ADD_GRF_A_BANK`, `OP_MUL_GRF_A_BANK`, `OP_RELU_GRF_A`), which require standard bitline activation and restoration delays.

---

## 9. Reproducibility & Verification Guide

### 9.1 Environment & Build
From workspace root:
```bash
cd PIMSimulator
python3 data/generate_all_data.py   # Uses fixed seed 1113
scons NO_LIBRARY=1 -j$(nproc)
```

### 9.2 Running Matrix Evaluations
1. **Headline Run (PREA-Aware Scheduling):**
   ```bash
   ./run_step3_matrix.sh 2>&1 | tee results/step3_matrix/final.txt
   ```
2. **Legacy Run (Naive Per-Bank Precharge):**
   ```bash
   PIMACT_PREA_AWARE=0 ./run_step3_matrix.sh 2>&1 | tee results/step3_matrix/final_legacy_prea.txt
   ```

### 9.3 Running Mode-Switch Analysis
```bash
PIM_TRACE_MODE_SWITCH=1 ./sim --gtest_filter="PIMKernelFixture.gemv_paper_4kx4k:PIMKernelFixture.add:PIMKernelFixture.mul_paper_2m:PIMKernelFixture.relu_paper_4m:PIMKernelFixture.gemv_pim_act:PIMKernelFixture.add_pim_act:PIMKernelFixture.mul_pim_act:PIMKernelFixture.relu_pim_act" > results/mode_switch/raw_mode_switch.txt 2>&1
python3 scripts/analyze_mode_switch.py results/mode_switch/raw_mode_switch.txt
```

---

## 10. Git Tag History

- **`partA-final` (`b10b767`):** Completed B1–B10 protocol fixes, initial PREA-aware static bool cache, and preliminary matrix logs.
- **`partA-final-r2` (`09ad4c8`):** Complete 351–352 ns mode-switch window analysis and B4 positive control verification.
- **`partA-final-r3` (`c01411e`):** Gated mode-switch logging (`PIM_TRACE_MODE_SWITCH`), added cycle decomposition table, and added `analyze_mode_switch.py`.
- **`partA-final-r4` (`c7fb7ab`):** Final Part A verification with `[PIM-REGION]` phase tracking (preload, region, readback), measurement scope analysis, and clean matrix logs.

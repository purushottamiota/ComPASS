# ComPASS Implementation Deviations & Architectural Analysis

This document catalogs protocol corrections, architectural deviations, parameter assumptions, and empirical findings between the MICRO 2025 ComPASS paper (*Yu et al.*) and the current simulation artifacts.

---

## 1. Optype Numbering (Deviation B10)
- **Paper Specification (Table 1, Page 7):**
  - Compute operations: `0–31`
  - Register transfers (`MOV Host -> RF`): `32`
  - Bank/Register transfers (`MOV Bank -> RF`, `MOV RF -> Bank`): `36–39`
- **Implementation in `PIMCmd.h`:**
  - Transfers are numbered contiguously `32–36`:
    - `OP_MOV_HOST_TO_RF = 32`
    - `OP_MOV_BANK_TO_GRF_A = 33`
    - `OP_MOV_BANK_TO_GRF_B = 34`
    - `OP_MOV_GRF_A_TO_BANK = 35`
    - `OP_MOV_GRF_B_TO_BANK = 36`
- **Impact:** Strictly cosmetic / internal enum encoding. Does not affect command encoding bit-widths on the command bus or simulation timings.

---

## 2. Architecture-Aware Optimization (AAO) Timing Parameters (Deviation B8)
- **Paper Specification (Section 4.5 & 4.7):**
  - Shortened timing constraints for register-only accesses (`tRCD`, `tRAS`, `tRC`, `tRP`) bypass bitline sensing, charge sharing, and full row restoration.
  - The paper omits specific cycle counts, noting calibration via manufacturer SPD.
- **Implementation in `PimActTiming.h`:**
  - Parameterized in Step 11 via `src/PimActTiming.h` with runtime environment variable overrides (`AAO_TRCD`, `AAO_TRAS`, `AAO_TRC`, `AAO_TRP`) parsed in `src/tests/MainTest.cpp`.
  - Defaults:
    - $t_{RCD} = \max(2, \lfloor t_{RCDRD} \times \text{AAO\_TRCD\_FACTOR} \rfloor)$ (default factor: 0.5)
    - $t_{RAS} = \max(4, \lfloor t_{RAS} \times \text{AAO\_TRAS\_FACTOR} \rfloor)$ (default factor: 0.6)
    - $t_{RC}  = \max(6, \lfloor t_{RC} \times \text{AAO\_TRC\_FACTOR} \rfloor)$ (default factor: 0.6)
    - $t_{RP}  = \max(2, \lfloor t_{RP} \times \text{AAO\_TRP\_FACTOR} \rfloor)$ (default factor: 0.5)

---

## 3. Benchmark Problem Dimensions (Deviation A1)
- **Paper Specification (Section 6, Lines 998–999):**
  - GEMV: $4096 \times 4096$ (4K × 4K)
  - ADD: $1024 \times 1024$ (1M vector elements)
  - MUL: $2048 \times 1024$ (2M vector elements)
  - ReLU: $4096 \times 1024$ (4M vector elements)
- **Correction:**
  - Added matched-size baseline tests (`gemv_paper_4kx4k`, `mul_paper_2m`, `relu_paper_4m`) to eliminate legacy sizing mismatches (e.g. 1M vs 16M GEMV).

---

## 4. Superseded Legacy Evaluation Artifacts
- **Superseded Files:**
  - `PIMSimulator/evaluate_fig7.py`
  - `PIMSimulator/results/aao_speedup_data.csv`
  - `PIMSimulator/results/fig7_aao_speedup.png`
- **Why Superseded:**
  - `evaluate_fig7.py` parsed host wall-clock execution time (in milliseconds) rather than simulated DRAM cycles and compared mismatched benchmark dimensions.
  - Valid cycle measurements are sourced exclusively from `results/step3_matrix/final.txt` and `results/step3_matrix/final_legacy_prea.txt`.

---

## 5. Performance Results & Protocol Verification Matrix

### 5.1 Cycle Matrix and Gain Comparison

Data extracted directly from `results/step3_matrix/final.txt` (Headline: PREA-Aware §5.1, cached static check) and `results/step3_matrix/final_legacy_prea.txt` (Comparison: Naive per-bank PRECHARGE):

| Kernel | Baseline Protocol (cycles) | Naive PIM-ACT AAO=0 (cycles) | Headline PIM-ACT AAO=0 (cycles) | Headline PIM-ACT AAO=1 (cycles) | Paper PIM-ACT AAO=0 | Paper PIM-ACT AAO=1 | FP16 Correctness |
|:---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **GEMV (4K×4K)** | 57,923 | 71,033 | 57,567 | 57,189 | +2.54% | +9.65% | `[A3-OK]` (0 mismatches) |
| **ADD (1M)** | 9,626 | 11,602 | 9,468 | 9,468 | +9.68% | +10.00% | `[A3-OK]` (0 mismatches) |
| **MUL (2M)** | 19,280 | 23,080 | 19,194 | 19,194 | +11.38% | +11.53% | `[A3-OK]` (0 mismatches) |
| **ReLU (4M)** | 25,131 | 30,903 | 25,809 | 25,809 | +4.51% | +4.65% | `[A3-OK]` (0 mismatches) |

### 5.2 Percentage Gain vs. Baseline ($\text{Baseline} / \text{PIM-ACT} - 1$)

| Kernel | Naive PIM-ACT (AAO=0) | Headline PREA-Aware (AAO=0) | Headline PREA-Aware (AAO=1) | Paper Target (AAO=0 / AAO=1) | Status vs. Paper |
|:---|:---:|:---:|:---:|:---:|:---|
| **GEMV** | −18.45% | **+0.62%** | **+1.28%** | +2.54% / +9.65% | 1.9% short (AAO=0); 8.37% short (AAO=1) |
| **ADD**  | −17.03% | **+1.67%** | **+1.67%** | +9.68% / +10.00% | ~8.3% short of target |
| **MUL**  | −16.49% | **+0.45%** | **+0.45%** | +11.38% / +11.53% | ~11.0% short of target |
| **ReLU** | −18.68% | **−2.63%** | **−2.63%** | +4.51% / +4.65% | **Slower than baseline** (7.2% short) |

**Conclusion on Speedup Claims:**
- **Fig. 7 numbers are NOT reproduced.**
- ReLU remains 2.63% slower than baseline.
- While PREA-aware scheduling rescues GEMV, ADD, and MUL from a ~17–19% deficit into slight gains (+0.45% to +1.67%), the gains fall 7–11 percentage points short of the paper's claimed speedups.
- AAO reduces GEMV execution by only 378 cycles (+0.66 percentage points) and has zero effect on ADD, MUL, and ReLU (where register transfers represent a negligible fraction of row activations).

---

## 6. Architectural Analysis: Why Paper Gains Cannot Be Reached in This Simulator

The paper posits that PIM-ACT derives substantial speedup by eliminating HBM-PIM mode-switch overheads (SB $\rightarrow$ HAB $\rightarrow$ HAB-PIM round trips).

### 6.1 Measured Mode-Switch Events in Baseline Kernels
Logging tagged mode-switch packets in `MemoryController::updateCommandQueue` revealed that all four baseline kernels perform **exactly one round trip** of mode switching:
1. `SB -> HAB` (initial switch to Host-Application-Bank mode): spans cycles 4319–4359 in ADD, 44359–44399 in GEMV, 8558–8598 in MUL/ReLU (41 cycles).
2. `HAB -> SB` (final exit to Standard-Bank mode): spans 22 cycles.
3. **Total mode-switch cost per kernel = 63 cycles.**

### 6.2 Theoretical Upper Bound on Speedup from Mode-Switch Elimination
If PIM-ACT completely eliminates all mode switching, the maximum achievable gain strictly from mode-switch reduction is:

$$\text{Upper Bound} = \frac{\text{Saved Switch Cycles}}{\text{Baseline Cycles}} = \frac{63}{\text{Baseline Cycles}}$$

| Kernel | Baseline Cycles | Mode Switch Cycles | Max Theoretical Speedup | Paper Claimed Speedup (AAO=0 / AAO=1) |
|:---|:---:|:---:|:---:|:---:|
| **GEMV** | 57,923 | 63 | **0.11%** | +2.54% / +9.65% |
| **ADD**  | 9,626  | 63 | **0.65%** | +9.68% / +10.00% |
| **MUL**  | 19,280 | 63 | **0.33%** | +11.38% / +11.53% |
| **ReLU** | 25,131 | 63 | **0.25%** | +4.51% / +4.65% |

**Architectural Insight:**
In the simulation harness, the workload is executed as a single contiguous compute block with a single mode switch at entry and exit. The paper's ~10% gains assume an environment where mode switches occur frequently (e.g. dozens of round trips interspersed between fine-grained host memory accesses and PIM bursts). Because this simulator incurs only 63 mode-switch cycles, eliminating mode switches can never yield more than 0.11%–0.65% speedup on these benchmarks.

---

## 7. Status of Protocol Issues (B1–B10)

1. **Issue B1 (tRAS/tRC +1 over-penalization):** Fixed. Removed spurious `+1` from `MemoryController.cpp` and `Rank.cpp`.
2. **Issue B2 (AAO tRP never triggered):** Fixed. Precharge uses `activeOptype` from `pimRank->getActiveOptype()`.
3. **Issue B3 (tRCDWR write delay):** Fixed. Writes use `tRCDWR` via `PimActTiming.h`.
4. **Issue B4 (Same-Row Optype Mismatch):** Diagnostic instrumented. Counter confirmed operational via positive control (injected mismatch fired `total=1`). When tested against the failing loop build, `[B4-HIST]` printed `total=0`. No same-row optype collisions occur in paper kernels.
5. **Issue B5 & B6 (Optype-0 Sentinel & AAO Scope):** Fixed. `NO_OPTYPE = 0xFF` sentinel implemented across `BusPacket` and `Transaction`.
6. **Issue B8 (AAO Parameterization):** Fixed. Parameterized via `PimActTiming.h` with env var overrides.
7. **Issue B9 (All-Bank PRECHARGE isIssuable Check):** **Attempted and reverted.**
   - Adding an all-bank loop to `CommandQueue::isIssuable(PRECHARGE)` dropped 2,112 `PIM_ACT` commands and caused 57,270 FP16 element mismatches in GEMV.
   - Restoring the target-bank guard did not fix the regression.
   - The B4 counter stayed 0 during the failure, so the exact micro-architectural mechanism for why 2,112 activations were dropped remains unestablished.
   - B9 is reverted; the verified per-bank `isIssuable(PRECHARGE)` is retained.
8. **Issue B10 (Optype Enum Numbering):** Documented in Section 1. Cosmetic difference.

---

## 8. Remaining Architectural Gap: ReLU Slowdown & Parity-Scoped PIM-ACT
- ReLU remains **2.63% slower** under PIM-ACT (25,809 cycles vs. 25,131 baseline cycles).
- **Hypothesis:** In HBM-PIM, elementwise operations alternate between even and odd bank pairs. The simulator's rank-wide `PIM_ACT` precharges and opens all 16 banks together, whereas the baseline protocol can interleave activations across even/odd bank pairs. Investigating parity-scoped (even/odd) `PIM_ACT` is marked as future work.

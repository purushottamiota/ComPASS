# ComPASS Implementation Deviations & Architectural Analysis

This document catalogs known deviations, implementation differences, and parameter assumptions between the MICRO 2025 ComPASS paper (*Yu et al.*) and the current simulation artifacts.

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
  - Section 4.5 describes shortened timing constraints for register-only accesses (`tRCD`, `tRAS`, `tRC`, `tRP`) because register operations bypass bitline sensing, charge sharing, and full row restoration.
  - The paper notes that exact values are calibrated via manufacturer SPD / timing registers and omits specific cycle counts.
- **Implementation in `PimActTiming.h`:**
  - Fully parameterized in Step 11 via `src/PimActTiming.h` with runtime environment variable overrides (`AAO_TRCD`, `AAO_TRAS`, `AAO_TRC`, `AAO_TRP`) parsed in `src/tests/MainTest.cpp`.
  - Defaults:
    - $t_{RCD} = \max(2, \lfloor t_{RCDRD} \times \text{AAO\_TRCD\_FACTOR} \rfloor)$ (default factor: 0.5)
    - $t_{RAS} = \max(4, \lfloor t_{RAS} \times \text{AAO\_TRAS\_FACTOR} \rfloor)$ (default factor: 0.6)
    - $t_{RC}  = \max(6, \lfloor t_{RC} \times \text{AAO\_TRC\_FACTOR} \rfloor)$ (default factor: 0.6)
    - $t_{RP}  = \max(2, \lfloor t_{RP} \times \text{AAO\_TRP\_FACTOR} \rfloor)$ (default factor: 0.5)
- **Status:** Parameterized in Step 11 for sensitivity analysis and sweeps without recompilation.

---

## 3. Benchmark Problem Dimensions (Deviation A1)
- **Paper Specification (Section 6, Lines 998–999):**
  - GEMV: $4096 \times 4096$ (4K × 4K)
  - ADD: $1024 \times 1024$ (1M vector elements)
  - MUL: $2048 \times 1024$ (2M vector elements)
  - ReLU: $4096 \times 1024$ (4M vector elements)
- **Original Codebase Inconsistency:**
  - Legacy `gemv` baseline test was set to $4096 \times 1024$ (1M elements) while `gemv_pim_act` was $4096 \times 4096$ (16M elements).
  - Legacy `mul` was 1M while `mul_pim_act` was 2M.
  - Legacy `relu` was 1M while `relu_pim_act` was 4M.
- **Correction:**
  - Added matched-size baseline tests (`gemv_paper_4kx4k`, `mul_paper_2m`, `relu_paper_4m`) to ensure strictly apples-to-apples baseline vs. PIM-ACT comparisons.

---

## 4. Superseded Legacy Evaluation Artifacts
- **Superseded Files:**
  - `PIMSimulator/evaluate_fig7.py`
  - `PIMSimulator/results/aao_speedup_data.csv`
  - `PIMSimulator/results/fig7_aao_speedup.png`
- **Why Superseded:**
  - `evaluate_fig7.py` parsed wall-clock execution time (in milliseconds) of the host test runner instead of simulated DRAM cycles.
  - Furthermore, it compared mismatched test sizes (e.g., 4096×1024 baseline vs. 4096×4096 PIM-ACT).
  - All legitimate cycle measurements must be sourced exclusively from `PIMSimulator/results/step3_matrix/` (`step3_prefix.txt`, `after_B1.txt`, `final.txt`, `final_prea_aware.txt`).

---

## 5. Performance Results & Protocol Verification Matrix

### Ground Truth Cycle Comparison

Data extracted directly from `results/step3_matrix/final.txt` (default scheduler) and `results/step3_matrix/final_prea_aware.txt` (`PIMACT_PREA_AWARE=1`):

| Kernel | Baseline Protocol | PIM-ACT w/o AAO (AAO=0) | PIM-ACT w/ AAO (AAO=1) | PIM-ACT w/ PREA_AWARE (AAO=0) | PIM-ACT w/ PREA_AWARE (AAO=1) | FP16 Correctness |
|:---|:---:|:---:|:---:|:---:|:---:|:---:|
| **GEMV (4K×4K)** | 57,923 | 71,033 | 69,818 | 57,567 | 57,189 | `[A3-OK]` (0 mismatches) |
| **ADD (1M)** | 9,626 | 11,602 | 11,602 | 9,468 | 9,468 | `[A3-OK]` (0 mismatches) |
| **MUL (2M)** | 19,280 | 23,080 | 23,080 | 19,194 | 19,194 | `[A3-OK]` (0 mismatches) |
| **ReLU (4M)** | 25,131 | 30,903 | 30,903 | 25,809 | 25,809 | `[A3-OK]` (0 mismatches) |

### Protocol Corrections Breakdown (Steps 6–11)
1. **Issue B1 (tRAS/tRC +1 over-penalization):** Artificially inflated PIM-ACT bank lock times. Corrected in Step 6 (`MemoryController.cpp` and `Rank.cpp`).
2. **Issue B3 (tRCDWR write delay):** PIM-ACT write path improperly constrained by read tRCD. Corrected in Step 7 via unified helper `PimActTiming.h`.
3. **Issue B2 (AAO tRP never triggered):** Precharge packets lacked active optype context, disabling AAO tRP reduction. Corrected in Step 9 via `pimRank->getActiveOptype()`.
4. **Issue B5 & B6 (Optype-0 Sentinel & AAO Scope):** Corrected in Step 10 using `NO_OPTYPE = 0xFF` to prevent collision with valid `OP_MUL_GRF_A_BANK = 0`.
5. **Issue B8 (AAO Parameterization):** Parameterized in Step 11 (`PimActTiming.h`, `PrintMacros.cpp`, `SystemConfiguration.h`, `MainTest.cpp`).

### B9 Investigation & GEMV Regression Root Cause
- **Hypothesis B9:** In PIM-ACT mode, `MemoryController` broadcasts `PRECHARGE` across all banks. It was hypothesized that `CommandQueue::isIssuable(PRECHARGE)` should therefore verify that all active banks have reached `nextPrecharge`.
- **Observed Behavior:** Enforcing an all-bank `nextPrecharge` loop in `isIssuable(PRECHARGE)` delayed rank-wide precharge past the completion of current bank bursts. When subsequent bursts accessing row 0 under a different optype entered the command queue, the scheduler saw row 0 still active in the bank states and treated the requests as **row hits**, completely bypassing `PIM_ACT` activation and omitting the required `pimRank->setActiveOptype()` transition.
- **Consequence:** 2,112 `PIM_ACT` commands were skipped in GEMV, causing 57,270 FP16 element mismatches (`[A3-FAIL]`).
- **Resolution:** `CommandQueue::isIssuable(PRECHARGE)` is retained in its verified, working per-target-bank form. This guarantees that all required `PIM_ACT` activations take place, restoring 100% bitwise FP16 accuracy (`[A3-OK]`, 0 mismatches).

### Cross-Bank Row-Hit Aware Scheduling (`PIMACT_PREA_AWARE`)
- **Root Cause of Excessive PIM_ACTs:** In `CommandQueue::process_precharge()`, an idle bank with no remaining commands in its local queue would immediately trigger rank-wide precharge, even if sibling banks had pending row-hit requests on the open row. This forced peer banks to undergo costly rank re-activations (`PIM_ACT`).
- **Mechanism:** Implemented in `process_precharge()` under the `PIMACT_PREA_AWARE` environment flag. When active, `process_precharge` checks whether any bank in the rank has pending row-hit PIM requests before allowing precharge generation.
- **Results:**
  - In GEMV, `#PIM_ACT` drops by **80.5%** (from 32,896 down to 6,400), and cycles reduce from 71,033 to 57,567 (w/o AAO) and 57,189 (w/ AAO), successfully beating the 57,923 baseline cycles.
  - In ADD 1M, cycles drop to 9,468 (vs 9,626 baseline).
  - In MUL 2M, cycles drop to 19,194 (vs 19,280 baseline).
  - 100% numerical correctness is preserved across all microbenchmarks.

---

## 6. Ramulator Architecture Simulator (Part B)
- **Paper Specification:** Uses Ramulator v1 (Kim et al., CAL 2015).
- **Active Codebase:** Ramulator 2.0 (`ramulator/`). Any Part B scheduling extensions will be implemented within the Ramulator 2.0 modular framework.

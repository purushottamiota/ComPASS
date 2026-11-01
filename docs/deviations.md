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
- **Implementation in `MemoryController.cpp`:**
  - Hardcoded scaling fractions are used:
    - $t_{RCD} = \max(2, \lfloor t_{RCDRD} / 2 \rfloor)$ (50% reduction)
    - $t_{RAS} = \max(4, \lfloor (t_{RAS} \times 6) / 10 \rfloor)$ (40% reduction / 60% of original)
    - $t_{RC}  = \max(6, \lfloor (t_{RC} \times 6) / 10 \rfloor)$ (40% reduction / 60% of original)
    - $t_{RP}  = \max(2, \lfloor t_{RP} / 2 \rfloor)$ (50% reduction)
- **Status:** Documented engineering assumptions. These will be parameterized in Step 11 for sensitivity analysis.

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
  - All legitimate cycle measurements must be sourced exclusively from `PIMSimulator/results/step3_matrix/` (`step3_prefix.txt`, `after_B1.txt`, etc.).

---

## 5. Current Performance Results & Investigation Status
- **Baseline Cycles (Ground Truth):**
  - ADD (1M): 9,626 cycles
  - GEMV (4K×4K): 57,923 cycles
  - MUL (2M): 19,280 cycles
  - ReLU (4M): 25,131 cycles
- **Numerical Correctness:**
  - Verified bitwise-identical FP16 results between baseline protocol and PIM-ACT across all benchmarks (`[A3-OK]`).
- **Current PIM-ACT Gap:**
  - In initial uncorrected code, PIM-ACT exhibits an execution slowdown (~21%–24% slower than baseline).
  - Root-cause breakdown:
    1. **Issue B1 (tRAS/tRC +1 over-penalization):** Artificially inflated PIM-ACT bank lock times. Corrected in Step 4.
    2. **Issue B3 (tRCDWR write delay):** PIM-ACT write path improperly constrained by read tRCD. Corrected in Step 5.
    3. **Issue B2 (AAO tRP never triggered):** Precharge packets lacked active optype context, disabling AAO tRP reduction. Corrected in Step 5.
    4. **Command serialization / row activation overhead:** In PIM-ACT mode, the scheduler activates all banks with `PIM_ACT` and generates more row activation/precharge cycles than the baseline mode-switching sequence. Command-count instrumentation (`#ACT`, `#PIM_ACT`, `#PRE`) is now active to isolate this behavior.

---

## 6. Ramulator Architecture Simulator (Part B)
- **Paper Specification:** Uses Ramulator v1 (Kim et al., CAL 2015).
- **Active Codebase:** Ramulator 2.0 (`ramulator/`). Any Part B scheduling extensions will be implemented within the Ramulator 2.0 modular framework.


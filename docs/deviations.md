# ComPASS Implementation Deviations & Architectural Analysis

This document catalogs protocol corrections, architectural deviations, parameter assumptions, empirical findings, and reproduction details between the MICRO 2025 ComPASS paper (*Yu et al.*) and the current simulation artifacts.

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
  - Parameterized via `src/PimActTiming.h` with runtime environment variable overrides (`AAO_TRCD`, `AAO_TRAS`, `AAO_TRC`, `AAO_TRP`) parsed in `src/tests/MainTest.cpp`.
  - Defaults:
    - $t_{RCD} = \max(2, \lfloor t_{RCDRD} \times \text{AAO\_TRCD\_FACTOR} \rfloor)$ (default factor: 0.5)
    - $t_{RAS} = \max(4, \lfloor t_{RAS} \times \text{AAO\_TRAS\_FACTOR} \rfloor)$ (default factor: 0.6)
    - $t_{RC}  = \max(6, \lfloor t_{RC} \times \text{AAO\_TRC\_FACTOR} \rfloor)$ (default factor: 0.6)
    - $t_{RP}  = \max(2, \lfloor t_{RP} \times \text{AAO\_TRP\_FACTOR} \rfloor)$ (default factor: 0.5)

---

## 3. Benchmark Problem Dimensions (Deviation A1)
- **Paper Specification (Section 6, Lines 998–999):**
  - GEMV: $4096 \times 4096$ matrix-vector
  - ADD: $1\text{M}$ vectors
  - MUL: $2\text{M}$ vectors
  - ReLU: $4\text{M}$ vectors
- **Implementation in Harness:**
  - In the Samsung PIMSimulator memory address space, vector operands are laid out across channels, ranks, banks, and bank groups as 2D row/column arrays:
    - ADD: $1024 \times 1024$ FP16 elements ($1\text{M}$ elements)
    - MUL: $2048 \times 1024$ FP16 elements ($2\text{M}$ elements)
    - ReLU: $4096 \times 1024$ FP16 elements ($4\text{M}$ elements)
    - GEMV: $4096 \times 4096$ FP16 matrix-vector ($16\text{M}$ weights, $4\text{K}$ input)
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
| **GEMV** | −18.46% | **+0.62%** | **+1.28%** | +2.54% / +9.65% | 1.9 pp short (AAO=0); 8.4 pp short (AAO=1) |
| **ADD**  | −17.03% | **+1.67%** | **+1.67%** | +9.68% / +10.00% | 8.0 pp short (AAO=0); 8.3 pp short (AAO=1) |
| **MUL**  | −16.46% | **+0.45%** | **+0.45%** | +11.38% / +11.53% | 10.9 pp short (AAO=0); 11.1 pp short (AAO=1) |
| **ReLU** | −18.68% | **−2.63%** | **−2.63%** | +4.51% / +4.65% | **Slower than baseline** (7.1 pp / 7.3 pp short) |

**Conclusion on Speedup Claims:**
- **Fig. 7 numbers are NOT reproduced.**
- ReLU remains 2.63% slower than baseline under PIM-ACT.
- While PREA-aware scheduling rescues GEMV, ADD, and MUL from a ~16–18% deficit into slight gains (+0.45% to +1.67%), the gains fall 8–11 percentage points short of the paper's claimed speedups.
- **AAO Measurement:** Runtime instrumentation (`[AAO-HIST]`) reveals that:
  - In GEMV, `OP_MOV_HOST_TO_RF` (optype 32) fires 2,048 times out of 6,464 `PIM_ACT` commands, reducing execution by 378 cycles (+0.66 percentage points).
  - In ADD, MUL, and ReLU, `[AAO-HIST]` confirmed exactly 0 activations trigger the AAO branch. In these kernels, all operations access DRAM banks (`OP_ADD_GRF_A_BANK`, `OP_MUL_GRF_A_BANK`, `OP_RELU_GRF_A`), which require standard bitline activation and restoration rather than shortened register-only timing.

---

## 6. Headline Policy: PREA-Aware Scheduling (§5.1)

### 6.1 Mechanism & Implementation
In naive PIM-ACT scheduling, when any single bank becomes idle, it issues a rank-wide `PRECHARGE` that closes open rows across all 16 banks in the rank simultaneously. If other banks have pending requests on their currently open row, this premature precharge forces them to incur unnecessary subsequent `PIM_ACT` activations.

To address this, our implementation of PREA-Aware scheduling in `CommandQueue::process_precharge()`:
1. When a bank requests a rank-wide `PRECHARGE` in PIM-ACT mode, the scheduler scans all 16 bank queues in the rank.
2. If any other bank in the rank has a pending command matching its currently open row address, the rank-wide precharge is suppressed, keeping the row open.
3. The queue scan stops early if it encounters a barrier-tagged packet (`BAR`), preventing cross-barrier command reordering.
4. The scan is optype-agnostic.

### 6.2 Configuration & Selection
- **Flag Name:** `PIMACT_PREA_AWARE`.
- **Initialization:** Evaluated once at startup using a `static const bool prea_aware_enabled` in `CommandQueue.cpp:334` to prevent per-cycle `getenv()` overhead.
- **Default:** `true` (enabled), faithful to our reading of Paper §5.1.
- **Legacy Selection:** Setting `PIMACT_PREA_AWARE=0` selects the legacy naive per-bank precharge policy.

### 6.3 Effect on Command Counts (from Verified Logs)

| Kernel | Policy | #PIM_ACT | #PRE | Change in #PIM_ACT | Change in #PRE |
| :--- | :--- | :---: | :---: | :---: | :---: |
| **GEMV (AAO=0)** | Naive PREA | 32,896 | 66,880 | — | — |
| | PREA-Aware | 6,400 | 40,384 | **−26,496 (−80.5%)** | **−26,496 (−39.6%)** |
| **ADD** | Naive PREA | 6,144 | 35,776 | — | — |
| | PREA-Aware | 3,136 | 31,744 | **−3,008 (−49.0%)** | **−4,032 (−11.3%)** |
| **MUL** | Naive PREA | 12,288 | 70,656 | — | — |
| | PREA-Aware | 6,208 | 64,576 | **−6,080 (−49.5%)** | **−6,080 (−8.6%)** |
| **ReLU** | Naive PREA | 16,384 | 79,872 | — | — |
| | PREA-Aware | 8,256 | 72,640 | **−8,128 (−49.6%)** | **−7,232 (−9.1%)** |

### 6.4 Caveats & Clarification
- **Optype-Agnostic Caveat:** Because the scan checks row matching without checking optype matching, it could theoretically keep a row open for a subsequent packet requiring a different optype. However, runtime diagnostics confirmed `[B4-HIST] total=0` across all kernels, proving no intra-row optype collisions occur in paper workloads.
- **Reading of Paper §5.1:** This mechanism represents our reading of the paper's §5.1 concept of coordinating precharges across banks to hide latency, rather than an explicit algorithm detailed in the paper text.

---

## 7. Extended Mode-Switch Measurement & Architectural Analysis

The paper (§3) notes that conventional HBM-PIM incurs significant latency switching between Standard Bank (SB) mode and HAB (all-bank) mode, estimating a full round-trip mode switch at about 350 ns.

### 7.1 Anatomy of Baseline Mode Switching
In `PIMKernel.cpp`, the baseline protocol executes mode switches through distinct phases:
1. `parkIn()`: Issues a barrier, reads special row $(1 \ll 13)$ across every bank in the rank (`START_PARK_IN_` ... `END_PARK_IN_`), followed by a barrier.
2. `changePIMMode(SB -> HAB)`: Issues 4 MRS writes (`START_SB_TO_HAB_` ... `END_SB_TO_HAB_`) configuring HAB (all-bank) mode, followed by a barrier.
3. `changePIMMode(HAB -> HAB_PIM)`: Issues a write tagged `PIM` to register `pim_reg_ra_` to reset the program counter, followed by a barrier.
4. **Kernel Compute:** Execution of compute tiles.
5. `changePIMMode(HAB_PIM -> HAB)`: Issues a write tagged `PIM` to register `pim_reg_ra_`, followed by a barrier.
6. `changePIMMode(HAB -> SB)`: Issues 2 MRS writes (`START_HAB_TO_SB` ... `END_HAB_TO_SB`) returning to Standard Bank mode, followed by a barrier.
7. `parkOut()`: Reads special row across every bank (`START_PARK_OUT_` ... `END_PARK_OUT_`), followed by a barrier.

### 7.2 Measured Mode-Switch Windows (Baseline Kernels)

Instrumenting `MemoryController::updateCommandQueue` with tag filters across all mode-switch packets yielded the following exact timings (1 GHz clock $\implies$ 1 cycle = 1 ns):

| Window | Events Tagged | Cycle Interval (ADD) | Duration (ADD) | Cycle Interval (GEMV) | Duration (GEMV) | Cycle Interval (MUL/ReLU) | Duration (MUL/ReLU) | Packets |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **parkIn()** | `PARK_IN_` | [4242, 4329] | 88 cycles | [44283, 44369] | 87 cycles | [8482, 8568] | 87 cycles | 2048 |
| **SB $\rightarrow$ HAB** | `SB_TO_HAB_` | [4319, 4359] | 41 cycles | [44359, 44399] | 41 cycles | [8558, 8598] | 41 cycles | 256 |
| **HAB $\rightarrow$ HAB_PIM** | `PIM` (Burst 1) | [4399, 4409] | 11 cycles | [44439, 44449] | 11 cycles | [8638, 8648] | 11 cycles | 128 |
| **HAB_PIM $\rightarrow$ HAB** | `PIM` (Burst 2) | [7366, 7376] | 11 cycles | [57582, 57592] | 11 cycles | [14584, 14594]* | 11 cycles | 128 |
| **HAB $\rightarrow$ SB** | `HAB_TO_SB` | [7416, 7437] | 22 cycles | [57632, 57653] | 22 cycles | [14634, 14655]* | 22 cycles | 256 |
| **parkOut()** | `PARK_OUT_` | [7464, 7549] | 86 cycles | [57680, 57765] | 86 cycles | [14682, 14767]* | 86 cycles | 2048 |

*\*Note: In ReLU, exit phase starts at cycle 15,958 with identical per-window durations.*

### 7.3 Entry, Exit, and Full Round-Trip Overhead

Accounting for barrier waiting times and pipeline drainage between phases gives the comprehensive entry and exit spans:
- **Entry Span (`parkIn` start $\rightarrow$ `HAB_PIM` entry end):**
  - ADD: $4242 \rightarrow 4409 = \mathbf{168\text{ cycles}}$
  - GEMV: $44283 \rightarrow 44449 = \mathbf{167\text{ cycles}}$
  - MUL: $8482 \rightarrow 8648 = \mathbf{167\text{ cycles}}$
  - ReLU: $8482 \rightarrow 8648 = \mathbf{167\text{ cycles}}$
- **Exit Span (`HAB_PIM` exit start $\rightarrow$ `parkOut` end):**
  - ADD: $7366 \rightarrow 7549 = \mathbf{184\text{ cycles}}$
  - GEMV: $57582 \rightarrow 57765 = \mathbf{184\text{ cycles}}$
  - MUL: $14584 \rightarrow 14767 = \mathbf{184\text{ cycles}}$
  - ReLU: $15958 \rightarrow 16141 = \mathbf{184\text{ cycles}}$
- **Total Mode-Switch Overhead:**
  - $\mathbf{167\text{--}168\text{ cycles (entry)} + 184\text{ cycles (exit)} = \mathbf{351\text{--}352\text{ cycles}}}$ (**$351\text{--}352\text{ ns}$ at 1 ns/cycle**).

**Comparison with Paper §3:** The measured 351–352 ns full round-trip overhead matches the paper's reported $\sim 350\text{ ns}$ mode-switch latency with near-perfect fidelity.

### 7.4 What PIM-ACT Removes
Inspection of `executeGemvPIMACT` and `executeEltwisePIMACT` in `PIMKernel.cpp` reveals that PIM-ACT eliminates all mode-switch calls:
- PIM-ACT kernels issue **0** `parkIn()` transactions, **0** `changePIMMode()` writes, and **0** `parkOut()` transactions.
- Runtime logs confirm exactly **0** mode-switch events during PIM-ACT kernel execution.
- Therefore, PIM-ACT removes the entirety of the 351–352 ns baseline mode-switch overhead.

### 7.5 Potential Speedup from Mode-Switch Elimination

Because each benchmark in the simulation harness runs as a single contiguous compute payload flanked by exactly one entry and exit switch, the maximum speedup achievable strictly by eliminating mode switches is bounded by:

$$\text{Mode Switch Fraction} = \frac{\text{Saved Switch Cycles}}{\text{Baseline Cycles}} = \frac{351\text{--}352}{\text{Baseline Cycles}}$$

| Kernel | Baseline Cycles | Mode Switch Cycles | Potential Speedup from Mode Switches | Paper Claimed Speedup (AAO=0 / AAO=1) |
|:---|:---:|:---:|:---:|:---:|
| **GEMV** | 57,923 | 351 | **0.61%** | +2.54% / +9.65% |
| **ADD**  | 9,626  | 352 | **3.66%** | +9.68% / +10.00% |
| **MUL**  | 19,280 | 351 | **1.82%** | +11.38% / +11.53% |
| **ReLU** | 25,131 | 351 | **1.40%** | +4.51% / +4.65% |

**Architectural Hypothesis:**
Even after accounting for the complete 351–352 ns mode switch round trip (including park-in, park-out, and barriers), mode-switch elimination can contribute at most 0.61%–3.66% speedup to these workloads.

*Hypothesis:* The paper's claimed $\sim 10\%$ speedups may assume an evaluation environment where fine-grained mode switches occur frequently between host and PIM execution (e.g. dozens of round trips interspersed between fine-grained host memory accesses and short PIM bursts), amortizing a smaller compute payload against each 350 ns round trip. In this simulator's benchmark harness, each kernel runs as a single contiguous compute block with exactly one entry and one exit switch, so mode-switch elimination cannot yield the paper's headline numbers.

---

## 8. Status of Protocol Issues (B1–B10)

1. **Issue B1 (tRAS/tRC +1 over-penalization):** Fixed. Removed spurious `+1` from `MemoryController.cpp` and `Rank.cpp`.
2. **Issue B2 (AAO tRP never triggered):** Fixed. Precharge uses `activeOptype` from `pimRank->getActiveOptype()`.
3. **Issue B3 (tRCDWR write delay):** Fixed. Writes use `tRCDWR` via `PimActTiming.h`.
4. **Issue B4 (Same-Row Optype Mismatch):** Diagnostic instrumented. Counter confirmed operational via positive control (injected mismatch fired `total=1`). When tested against both the passing headline build and the failing loop build, `[B4-HIST]` printed `total=0`. No same-row optype collisions occur in paper kernels.
5. **Issue B5 & B6 (Optype-0 Sentinel & AAO Scope):** Fixed. `NO_OPTYPE = 0xFF` sentinel implemented across `BusPacket` and `Transaction`.
6. **Issue B8 (AAO Parameterization):** Fixed. Parameterized via `PimActTiming.h` with env var overrides.
7. **Issue B9 (All-Bank PRECHARGE isIssuable Check):** **Attempted and reverted.**
   - Adding an all-bank loop to `CommandQueue::isIssuable(PRECHARGE)` dropped 2,112 `PIM_ACT` commands and caused 57,270 FP16 element mismatches in GEMV.
   - Restoring the target-bank guard did not fix the regression.
   - The B4 counter stayed 0 during the failure, so the exact micro-architectural mechanism for why 2,112 activations were dropped remains unestablished.
   - B9 is reverted; the verified per-bank `isIssuable(PRECHARGE)` is retained.
8. **Issue B10 (Optype Enum Numbering):** Documented in Section 1. Cosmetic difference.

---

## 9. Remaining Architectural Gap: ReLU Slowdown & Parity-Scoped PIM-ACT
- ReLU remains **2.63% slower** under PIM-ACT (25,809 cycles vs. 25,131 baseline cycles).
- **Hypothesis:** In HBM-PIM, elementwise operations alternate between even and odd bank pairs. The simulator's rank-wide `PIM_ACT` precharges and opens all 16 banks together, whereas the baseline protocol can interleave activations across even/odd bank pairs. Investigating parity-scoped (even/odd) `PIM_ACT` is marked as future work.

---

## 10. Reproducibility Guide

### Repository State
- **Git Commit:** `b10b767` (and follow-up documentation tag `partA-final-r2`)
- **Git Tag:** `partA-final-r2`

### Build Command
From `PIMSimulator/`:
```bash
scons NO_LIBRARY=1 -j$(nproc)
```

### Dataset Generation (Fixed Seed 1113)
```bash
python3 data/generate_all_data.py
```

### Running the 12-Configuration Cycle Matrix

1. **Headline Run (PREA-Aware Scheduling):**
   ```bash
   ./run_step3_matrix.sh 2>&1 | tee results/step3_matrix/final.txt
   ```
2. **Comparison Run (Naive Per-Bank Precharge):**
   ```bash
   PIMACT_PREA_AWARE=0 ./run_step3_matrix.sh 2>&1 | tee results/step3_matrix/final_legacy_prea.txt
   ```

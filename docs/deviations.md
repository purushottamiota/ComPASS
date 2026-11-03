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

| Kernel | Naive PIM-ACT (AAO=0) | Headline PREA-Aware (AAO=0) | Headline PREA-Aware (AAO=1) | Paper Target (AAO=0 / AAO=1) | Status vs. Paper (Whole-Run) |
|:---|:---:|:---:|:---:|:---:|:---|
| **GEMV** | −18.46% | **+0.62%** | **+1.28%** | +2.54% / +9.65% | 1.9 pp short (AAO=0); 8.4 pp short (AAO=1) |
| **ADD**  | −17.03% | **+1.67%** | **+1.67%** | +9.68% / +10.00% | 8.0 pp short (AAO=0); 8.3 pp short (AAO=1) |
| **MUL**  | −16.46% | **+0.45%** | **+0.45%** | +11.38% / +11.53% | 10.9 pp short (AAO=0); 11.1 pp short (AAO=1) |
| **ReLU** | −18.68% | **−2.63%** | **−2.63%** | +4.51% / +4.65% | **Slower than baseline** (7.1 pp / 7.3 pp short) |

**Key Findings:**
- When measured across the entire simulation run (including host data preload and result readback), whole-run speedups fall short of the paper's headline numbers.
- **GEMV (AAO=0) is reproduced within about 0.4–0.7 percentage points (+2.92% to +3.25% vs +2.54%) when measured strictly over the PIM compute region** (see Section 7.5 and 7.6).
- **AAO Measurement:** Runtime instrumentation (`[AAO-HIST]`) reveals that:
  - In GEMV, `OP_MOV_HOST_TO_RF` (optype 32) fires 2,048 times out of 6,464 `PIM_ACT` commands, which is consistent with the 378-cycle gain (+0.66 percentage points).
  - In ADD, MUL, and ReLU, `[AAO-HIST]` confirmed exactly 0 activations trigger the AAO branch. In these kernels, all operations access DRAM banks (`OP_ADD_GRF_A_BANK`, `OP_MUL_GRF_A_BANK`, `OP_RELU_GRF_A`), which require standard bitline activation and restoration rather than shortened register-only timing.

---

## 6. Headline Policy: PREA-Aware Scheduling (§5.1)

### 6.1 Paper §5.1 Specification vs Implementation
The paper's §5.1 states that PREA is required only when the row address or optype changes, and that successive same-row, same-optype PIM requests are row hits that do not need repeated activation or precharging.

In naive PIM-ACT scheduling, when any single bank finishes its queue and becomes idle, it issues a rank-wide `PRECHARGE` that closes open rows across all 16 banks in the rank simultaneously. If other banks have pending requests on their currently open row, this premature rank-wide precharge closes an open row that is still needed, forcing subsequent redundant `PIM_ACT` activations.

To implement the behavior described in §5.1, our implementation in `CommandQueue::process_precharge()`:
1. When a bank requests a rank-wide `PRECHARGE` in PIM-ACT mode, the scheduler scans all 16 bank queues in the rank.
2. If any other bank in the rank has a pending command matching its currently open row address, the rank-wide precharge is suppressed, keeping the row open across the rank for successive same-row hits.
3. The queue scan stops early if it encounters a barrier-tagged packet (`BAR`), preventing cross-barrier command reordering.
4. The scan is optype-agnostic.

### 6.2 Configuration & Selection
- **Flag Name:** `PIMACT_PREA_AWARE`.
- **Initialization:** Evaluated once at startup using a `static const bool prea_aware_enabled` in `CommandQueue::process_precharge` to prevent per-cycle `getenv()` overhead.
- **Default:** `true` (enabled), faithful to Paper §5.1.
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

*Note on non-PIM commands:* Under PREA-aware scheduling, normal `#ACT` counts also shifted: in ReLU, normal `#ACT` rose from 63,488 to 64,384 (+896), while in ADD, normal `#ACT` dropped from 29,632 to 28,608 (−1,024). This indicates that keeping rows open across the rank alters the scheduling interleaving of non-PIM transactions as well.

### 6.4 Caveats & Clarification
- **Optype-Agnostic Caveat:** Because the scan checks row matching without checking optype matching, it could theoretically keep a row open for a subsequent packet requiring a different optype. However, runtime diagnostics measured 0 events at issue time for these four kernels under this scheduler (`[B4-HIST] total=0`).
- **Reading of Paper §5.1:** This mechanism represents our implementation of the paper's §5.1 principle that same-row accesses should be preserved as row hits without premature precharge, rather than an explicit scheduling algorithm detailed in the paper text.

---

## 7. Extended Mode-Switch Measurement & Architectural Analysis

The paper (§3) notes that conventional HBM-PIM incurs significant latency switching between Standard Bank (SB) mode and HAB (all-bank) mode, estimating a full round-trip mode switch at about 350 ns.

### 7.1 Anatomy of Baseline Mode Switching
In `PIMKernel.cpp`, the baseline protocol executes mode switches through distinct phases:
1. `parkIn()`: Issues a barrier, reads special row $(1 \ll 13)$ across every bank in the rank (`START_PARK_IN_` ... `END_PARK_IN_`), followed by a barrier.
2. `changePIMMode(SB -> HAB)`: Issues 4 command sequences to reserved rows (`START_SB_TO_HAB_` ... `END_SB_TO_HAB_`) configuring HAB (all-bank) mode, followed by a barrier.
3. `changePIMMode(HAB -> HAB_PIM)`: Issues a write tagged `PIM` to register `pim_reg_ra_` to reset the program counter, followed by a barrier.
4. **Kernel Compute:** Execution of compute tiles.
5. `changePIMMode(HAB_PIM -> HAB)`: Issues a write tagged `PIM` to register `pim_reg_ra_`, followed by a barrier.
6. `changePIMMode(HAB -> SB)`: Issues 2 command sequences to reserved rows (`START_HAB_TO_SB` ... `END_HAB_TO_SB`) returning to Standard Bank mode, followed by a barrier.
7. `parkOut()`: Reads special row across every bank (`START_PARK_OUT_` ... `END_PARK_OUT_`), followed by a barrier.

### 7.2 Measured Mode-Switch Windows (Baseline Kernels)

Instrumenting `MemoryController::updateCommandQueue` with tag filters across all mode-switch packets yielded the following exact timings (1 GHz clock $\implies$ 1 cycle = 1 ns):

| Window | Events Tagged | ADD Interval | ADD Duration | GEMV Interval | GEMV Duration | MUL Interval | MUL Duration | ReLU Interval | ReLU Duration | Packets |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **parkIn()** | `PARK_IN_` | [4242, 4329] | 88 cycles | [44283, 44369] | 87 cycles | [8482, 8568] | 87 cycles | [8482, 8568] | 87 cycles | 2048 |
| **SB $\rightarrow$ HAB** | `SB_TO_HAB_` | [4319, 4359] | 41 cycles | [44359, 44399] | 41 cycles | [8558, 8598] | 41 cycles | [8558, 8598] | 41 cycles | 256 |
| **HAB $\rightarrow$ HAB_PIM** | `PIM` (Burst 1) | [4399, 4409] | 11 cycles | [44439, 44449] | 11 cycles | [8638, 8648] | 11 cycles | [8638, 8648] | 11 cycles | 128 |
| **HAB_PIM $\rightarrow$ HAB** | `PIM` (Burst 2) | [7366, 7376] | 11 cycles | [57582, 57592] | 11 cycles | [14584, 14594] | 11 cycles | [15958, 15968] | 11 cycles | 128 |
| **HAB $\rightarrow$ SB** | `HAB_TO_SB` | [7416, 7437] | 22 cycles | [57632, 57653] | 22 cycles | [14634, 14655] | 22 cycles | [16008, 16029] | 22 cycles | 256 |
| **parkOut()** | `PARK_OUT_` | [7464, 7549] | 86 cycles | [57680, 57765] | 86 cycles | [14682, 14767] | 86 cycles | [16056, 16141] | 86 cycles | 2048 |

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

---

### 7.5 Measurement Scope: Preload, PIM Region, and Readback Decomposition

The cycle count reported by `kernel->getCycle()` measures the elapsed cycles from cycle 0 until the simulation pipeline drains. This duration naturally decomposes into three distinct phases:
1. **Preload Phase:** Host memory transactions set up vector inputs and weights prior to PIM execution.
2. **PIM Region:** Interval between the first and last PIM-related commands (`[PIM-REGION]`), enclosing all mode-switching and PIM compute bursts.
3. **Readback Phase:** Host read transactions transfer results back to host memory.

Mathematically, with 0-indexed cycles:
$$\text{Preload} = \text{first}, \quad \text{Region} = \text{last} - \text{first} + 1, \quad \text{Readback} = \text{Total} - \text{last} - 1$$
$$\text{Total Cycles} = \text{Preload} + \text{Region} + \text{Readback}$$

(For example, GEMV Baseline has $\text{first}=44,283$, $\text{last}=57,765$, $\text{Total}=57,923$: $\text{Preload}=44,283$, $\text{Region}=57,765 - 44,283 + 1 = 13,483$, $\text{Readback}=57,923 - 57,765 - 1 = 157$, summing to $57,923$).

#### Detailed Phase Measurement Table

| Benchmark | Mode | Preload | PIM Region | Readback | Total Cycles | Region Share |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| **GEMV (4K×4K)** | Baseline | 44,283 | 13,483 | 157 | **57,923** | 23.3% |
| | PREA-Aware (AAO=0) | 44,326 | 13,059 | 182 | **57,567** | 22.7% |
| | PREA-Aware (AAO=1) | 44,326 | 12,681 | 182 | **57,189** | 22.2% |
| | Naive PREA (AAO=0) | 44,326 | 26,508 | 199 | **71,033** | 37.3% |
| | Naive PREA (AAO=1) | 44,326 | 25,293 | 199 | **69,818** | 36.2% |
| **ADD (1M)** | Baseline | 4,242 | 3,308 | 2,076 | **9,626** | 34.4% |
| | PREA-Aware (AAO=0/1)| 4,279 | 3,087 | 2,102 | **9,468** | 32.6% |
| | Naive PREA (AAO=0/1)| 4,279 | 4,818 | 2,505 | **11,602** | 41.5% |
| **MUL (2M)** | Baseline | 8,482 | 6,286 | 4,512 | **19,280** | 32.6% |
| | PREA-Aware (AAO=0/1)| 8,515 | 6,141 | 4,538 | **19,194** | 32.0% |
| | Naive PREA (AAO=0/1)| 8,515 | 10,012 | 4,553 | **23,080** | 43.4% |
| **ReLU (4M)** | Baseline | 8,482 | 7,660 | 8,989 | **25,131** | 30.5% |
| | PREA-Aware (AAO=0/1)| 8,515 | 7,877 | 9,417 | **25,809** | 30.5% |
| | Naive PREA (AAO=0/1)| 8,515 | 13,357 | 9,031 | **30,903** | 43.2% |

#### Observations on Phase Discrepancy:
- **GEMV Execution is 77% Preload:** In GEMV, $44,283$ cycles ($76.5\%$) of the $57,923$ baseline cycles are spent initializing weights in memory. The actual PIM computation spans only **$13,483$ cycles** ($\approx 13.5\,\mu\text{s}$), consistent with the hardware paper's reported measurement of *"about 13 µs"*.
- **ReLU Slowdown Originates in Readback:** In ReLU, PIM-ACT runs 678 cycles slower overall ($25,809$ vs $25,131$). However, the PIM compute region is only 217 cycles slower ($7,877$ vs $7,660$), while **$428$ cycles ($63\%$ of the slowdown)** occur during the host readback phase ($9,417$ vs $8,989$).

#### Region Start Anchor Sensitivity & Like-for-Like Comparison

The baseline region starts at the first `parkIn` packet on the bus. In contrast, the PIM-ACT region starts at the first `PIM_ACT` command, which issues 33–43 cycles later in every kernel:
- **GEMV:** Baseline `parkIn` starts at cycle $44,283$; PIM-ACT first `PIM_ACT` starts at $44,326$ ($+43$ cycles).
- **ADD:** Baseline starts at cycle $4,242$; PIM-ACT starts at $4,279$ ($+37$ cycles).
- **MUL:** Baseline starts at cycle $8,482$; PIM-ACT starts at $8,515$ ($+33$ cycles).
- **ReLU:** Baseline starts at cycle $8,482$; PIM-ACT starts at $8,515$ ($+33$ cycles).

Inspection of the memory bus trace reveals why: the host preload write burst completes at almost identical cycles across both protocols (`last_preload_wr` is $44,282$ vs $44,286$ in GEMV, $4,239$ in ADD, and $8,475$ in MUL and ReLU). While baseline begins issuing mode-switch `parkIn` commands immediately upon write completion, PIM-ACT waits for all banks to become idle (turnaround after the last preload write) before issuing the first rank-wide `PIM_ACT`. 

This 33–43 cycle interval represents real PIM-ACT scheduling overhead, but booking it into "preload" slightly inflates the baseline region duration. Re-anchoring the baseline region start to coincide with the PIM-ACT start cycle yields a like-for-like comparison:

| Kernel | Mode | Region Gain (Reported / Protocol Start) | Baseline Re-anchored to PIM-ACT Start | Paper Target (AAO=0) | Reproduction Gap Range |
| :--- | :--- | :---: | :---: | :---: | :---: |
| **GEMV** | PREA-Aware (AAO=0) | **+3.25%** ($13,483 \rightarrow 13,059$) | **+2.92%** ($13,440 \rightarrow 13,059$) | **+2.54%** | **0.38 – 0.71 pp** (within 0.4–0.7 pp) |
| | PREA-Aware (AAO=1) | **+6.32%** ($13,483 \rightarrow 12,681$) | **+5.98%** ($13,440 \rightarrow 12,681$) | **+9.65%** | 3.33 – 3.67 pp short |
| **ADD** | PREA-Aware (AAO=0/1)| **+7.16%** ($3,308 \rightarrow 3,087$) | **+5.96%** ($3,271 \rightarrow 3,087$) | **+9.68%** | 2.52 – 3.72 pp short |
| **MUL** | PREA-Aware (AAO=0/1)| **+2.36%** ($6,286 \rightarrow 6,141$) | **+1.82%** ($6,253 \rightarrow 6,141$) | **+11.38%** | 9.02 – 9.56 pp short |
| **ReLU** | PREA-Aware (AAO=0/1)| **−2.75%** ($7,660 \rightarrow 7,877$) | **−3.17%** ($7,627 \rightarrow 7,877$) | **+4.51%** | 7.26 – 7.68 pp short |

*Conclusion:* Depending on whether the region is anchored at the protocol's first command or synchronized to the first `PIM_ACT`, **GEMV without AAO matches the paper within about 0.4–0.7 percentage points**.

---

### 7.6 Region-Only Speedup vs Whole-Run Speedup

Comparing speedups evaluated over the entire simulation run versus strictly over the PIM compute region:

| Kernel | Mode | Whole-Run Gain | Region-Only Gain | Predicted Gain from Switch Alone ($R / (R - 351) - 1$) | Paper Target | Gap (Region vs Paper) |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| **GEMV** | PREA-Aware (AAO=0) | **+0.62%** | **+3.25%** | **+2.67%** | **+2.54%** | **0.71 pp** (+3.25% vs +2.67% is 0.58 pp) |
| | PREA-Aware (AAO=1) | **+1.28%** | **+6.32%** | **+2.67%** | **+9.65%** | 3.33 pp short |
| **ADD** | PREA-Aware (AAO=0/1) | **+1.67%** | **+7.16%** | **+11.91%** | **+9.68% / +10.00%**| 2.52 / 2.84 pp short |
| **MUL** | PREA-Aware (AAO=0/1) | **+0.45%** | **+2.36%** | **+5.91%** | **+11.38% / +11.53%**| 9.02 / 9.17 pp short |
| **ReLU** | PREA-Aware (AAO=0/1) | **−2.63%** | **−2.75%** | **+4.80%** | **+4.51% / +4.65%** | 7.26 / 7.40 pp short |

#### Architectural Insight & Measurement Scope:
1. **GEMV Agreement:** In GEMV without AAO, the predicted speedup from eliminating one 351-cycle mode switch over the 13.5 µs PIM region is **+2.67%**, which matches the paper's claimed **+2.54%** within **0.13 percentage points**. Measured region speedup (+3.25%) is within **0.58 percentage points** of the prediction and within **0.71 percentage points** of the paper target (or within **0.38 pp** under common start re-anchoring).
2. **Whole-Run Dilution:** In the simulator harness, reporting whole-run cycles dilutes the mode-switch gain by the unaccelerated host preload (which accounts for up to 77% of GEMV runtime).
3. **Hardware Model Configuration:** The tests execute using `system_hbm_64ch.ini` with `NUM_CHANS=64` independent memory controllers and `ini/HBM2_samsung_2M_16B_x64.ini` (`NUM_BANKS=16`), which is consistent with the scale of 4 HBM-PIM stacks (16 pseudo-channels each). The ~13.5 µs baseline GEMV region is consistent with the hardware paper's reported ~13 µs for a 4096×4096 GEMV. The divergence in whole-run speedup is driven by measurement scope (whole harness run vs PIM kernel region), rather than system scale. Note that the tests run against `system_hbm_64ch.ini` (64 channels), not `system_hbm.ini` (16 channels).

---

### 7.7 Boundary Decomposition: Where the Net Cycle Gaps Originate

Decomposing the whole-run difference between the baseline and PREA-aware PIM-ACT into its constituent boundary shifts and region overheads:

$$\text{Net Difference} = (\text{Switch Removed} - \text{Region Saved}) + \text{Preload Shift} + \text{Readback Shift}$$

| Kernel | Switch Removed | Region Cycles Saved | Region Overhead (Switch Removed − Region Saved) | Preload Shift | Readback Shift | Total Net Difference | Net Whole-Run Cycles Saved |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **GEMV** | 351 | 424 | **−73** | +43 | +25 | **−5** | **+356** |
| **ADD**  | 352 | 221 | **131** | +37 | +26 | **194** | **+158** |
| **MUL**  | 351 | 145 | **206** | +33 | +26 | **265** | **+86** |
| **ReLU** | 351 | −217 (slowdown) | **568** | +33 | +428 | **1,029** | **−678** (slowdown) |

#### Structural Findings from Decomposition:
1. **Constant Boundary Shifts:** Every kernel pays a small, nearly constant boundary penalty under PIM-ACT:
   - **Preload Shift ($\approx 33\text{--}43$ cycles):** Bank turnaround latency following the final preload write before the memory controller can safely issue the first rank-wide `PIM_ACT` (44,283 vs 44,326 in GEMV; 4,242 vs 4,279 in ADD; 8,482 vs 8,515 in MUL and ReLU).
   - **Readback Shift ($\approx 25\text{--}26$ cycles for GEMV, ADD, MUL):** Minor queue drain and bus turnaround before host readback streams initiate.
2. **GEMV Region Savings ($424$ cycles saved, $73$ above switch alone):** In GEMV, the PIM compute region saves $424$ cycles ($13,483 \rightarrow 13,059$), which is $73$ cycles more than the 351-cycle mode switch alone. This is supported by row activation counts in the compute region: baseline GEMV issues $9,152$ per-bank row activations, whereas PREA-aware PIM-ACT issues $6,464$ rank-wide activations ($2,688$ fewer row activations, a $29.4\%$ reduction). This region saving absorbs the $+43$ preload shift and $+25$ readback shift, netting $-5$ cycles difference ($+356$ whole-run cycles saved over baseline).
3. **Elementwise Compute Overhead is Not Driven by Row Count:** In ReLU, the PIM region opens **fewer rows** than baseline ($8,256$ PIM_ACT vs $11,136$ baseline ACT, a $25.9\%$ reduction), yet the PIM region is **$217$ cycles slower** ($7,877$ vs $7,660$), which is $568$ cycles above the 351-cycle switch saving ($351 - (-217) = 568$). Because activation counts are lower, the region overhead cannot be explained by row opens. This directly supports the hypothesis in §9 that rank-wide all-bank synchronization in `PIM_ACT` serializes memory transactions that HBM-PIM hardware pipelines across even/odd bank pairs.
4. **ReLU Readback Outlier:** ReLU's $+428$-cycle readback shift is the single massive outlier across the entire benchmark suite.

---

### 7.8 Empirical Command Analysis: GEMV and ReLU Phase Breakdowns

To examine the micro-architectural command distribution across execution phases, we instrumented phase-split command tracking (`[PHASE-CMDS]`) capturing `#ACT`, `#PIM_ACT`, `#PRE`, `#PMPRE` (PIM-mode rank-wide precharges), `#RD`, and `#WR`:

#### Phase-Split DRAM Command Counts (GEMV 4K×4K)

| Phase | Metric | Baseline (`gemv_paper_4kx4k`) | PREA-Aware PIM-ACT (`gemv_pim_act`) | Delta (PIM-ACT vs Baseline) |
| :--- | :--- | :---: | :---: | :---: |
| **Preload** | `#ACT` / `#PRE` / `#WR` | 33,472 / 33,408 / 1,048,512 | 33,472 / 33,472 / 1,048,576 | 0 / +64 / +64 |
| **Region**  | `#ACT` / `#PIM_ACT` | 9,152 / 0 | **0 / 6,464** | **−2,688 net activations (−29.4%)** |
| | `#PRE` / `#PMPRE` | 8,832 / 0 | 6,400 / 6,400 | −2,432 / +6,400 |
| | `#RD` / `#WR` | 133,120 / 17,536 | 131,072 / 16,896 | −2,048 / −640 |
| **Readback**| `#ACT` / `#PRE` / `#PMPRE` | 320 / 704 / 0 | 512 / 576 / 64 | +192 / −128 / +64 |
| | `#RD` | 4,096 | 4,096 | 0 |
| **Whole-Run**| Total Row Activations | **42,944** | **40,448** (33,984 ACT + 6,464 PIM_ACT) | **−2,496 (−5.8%)** |

*GEMV Observation:* In the compute region, PREA-aware PIM-ACT eliminates $2,688$ row activations compared to baseline, supporting the extra $73$ cycles saved beyond the 351-cycle switch ($424$ region cycles saved vs $351$ switch removed).

#### Phase-Split DRAM Command Counts (ReLU 4M)

| Metric | Baseline | Naive PREA PIM-ACT (`PIMACT_PREA_AWARE=0`) | PREA-Aware PIM-ACT (Headline) | Delta (PREA-Aware vs Baseline) | Delta (PREA-Aware vs Naive) |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Preload #ACT** | 53,248 | 53,248 | 53,248 | 0 | 0 |
| **Preload #PRE** | 52,672 | 53,248 | 53,248 | +576 | 0 |
| **Preload #WR**  | 262,144 | 262,144 | 262,144 | 0 | 0 |
| **Region #ACT**  | 11,136 | **0** | **0** | −11,136 | 0 |
| **Region #PIM_ACT** | 0 | 16,384 | **8,256** | +8,256 | **−8,128 (−49.6%)** |
| **Region #PRE**  | 11,328 | 16,320 | 8,192 | −3,136 | −8,128 |
| **Region #PMPRE**| 0 | 16,320 | 8,192 | +8,192 | −8,128 |
| **Readback #ACT** | **10,048** | **10,240** | **11,136** | **+1,088 (+10.8%)** | **+896 (+8.75%)** |
| **Readback #PRE** | **10,432** | **10,304** | **11,200** | **+768 (+7.36%)** | **+896 (+8.70%)** |
| **Readback #PMPRE**| 0 | 64 | 64 | +64 | **0** |
| **Readback #RD**  | 262,144 | 262,144 | 262,144 | 0 | 0 |

#### Analysis of the ReLU Readback Slowdown:
1. **Location in Readback:** Command counts locate the entire $+896$ `#ACT` and $+896$ `#PRE` delta against naive 100% inside the readback phase ($11,136 - 10,240 = \mathbf{+896}$). Across 64 channels, this corresponds to **$14$ extra row cycles per channel** ($896 / 64 = 14$).
2. **Cycle Cost Cross-Check:** Dividing the measured readback penalty by the per-channel extra row cycles gives:
   $$\frac{428\text{ extra readback cycles}}{14\text{ extra row cycles per channel}} \approx 30.6\text{ cycles per extra row cycle}$$
   This is consistent with the duration of one serialized $t_{RP} + t_{RCD}$ row-cycle delay (nominal $t_{RP}=14 + t_{RCDRD}=14 = 28$ cycles), assuming the extra row cycles serialize uniformly across the 64 channels.
3. **PIM-Mode Rank-Wide Precharge Test (`#PMPRE`):** The `#PMPRE` counter in readback measures exactly **64** in both PREA-aware and naive runs ($\Delta = 0$). This confirms that exactly one rank-wide precharge is issued per channel ($64 / 64 = 1$) at the readback boundary to clear PIM mode (`pimRank->setPIMACTMode(false)` in `Rank.cpp`), after which all remaining readback precharges ($11,136$ in aware, $10,240$ in naive) are standard per-bank precharges. This rules out the hypothesis that rank-wide precharges were repeatedly closing other banks' just-opened rows during readback. Instead, leaving rows open across the rank under PREA-aware scheduling forces host readback streams to incur extra closed-page conflict cycles.

---

## 8. Status of Protocol Issues (B1–B10)

1. **Issue B1 (tRAS/tRC +1 over-penalization):** Fixed. Removed spurious `+1` from `MemoryController.cpp` and `Rank.cpp`.
2. **Issue B2 (AAO tRP never triggered):** Fixed. Precharge uses `activeOptype` from `pimRank->getActiveOptype()`.
3. **Issue B3 (tRCDWR write delay):** Fixed. Writes use `tRCDWR` via `PimActTiming.h`.
4. **Issue B4 (Same-Row Optype Mismatch):** Diagnostic instrumented. Counter confirmed operational via positive control (injected mismatch fired `total=1`). When tested against both the passing headline build and the failing loop build, `[B4-HIST]` printed `total=0`. Measured 0 events at issue time for these four kernels under this scheduler.
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
- ReLU remains **2.63% slower** overall (and 2.75% slower in the PIM region).
- **Readback Slowdown (428 cycles):** Command tracking locates the slowdown in the readback phase (+896 ACT / +896 PRE, about 14 per channel); consistent with extra row cycles of about $t_{RP} + t_{RCD}$ each ($428 / 14 \approx 30.6$ cycles), with the `#PMPRE` counter showing exactly 1 mode-clearing precharge per channel.
- **PIM Region Slowdown (217 cycles):** In the region, PIM-ACT issues fewer row activations than baseline (8,256 vs 11,136, −25.9%) but is 217 cycles slower (568 cycles above the 351-cycle switch saving). The region overhead is not driven by row count, supporting the hypothesis that rank-wide all-bank synchronization in `PIM_ACT` serializes execution that HBM-PIM hardware pipelines across even/odd bank pairs. Investigating parity-scoped (even/odd) `PIM_ACT` remains marked as future work.

---

## 10. Reproducibility Guide

### Repository State
- **Git Commit:** `64eb5f3` (`partA-final-r5`), follow-up `partA-final-r6`
- **Git Tag:** `partA-final-r6`

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

### Running Mode-Switch Traces
```bash
PIM_TRACE_MODE_SWITCH=1 ./sim --gtest_filter="PIMKernelFixture.gemv_paper_4kx4k:PIMKernelFixture.add:PIMKernelFixture.mul_paper_2m:PIMKernelFixture.relu_paper_4m:PIMKernelFixture.gemv_pim_act:PIMKernelFixture.add_pim_act:PIMKernelFixture.mul_pim_act:PIMKernelFixture.relu_pim_act" > results/mode_switch/raw_mode_switch.txt 2>&1
python3 scripts/analyze_mode_switch.py results/mode_switch/raw_mode_switch.txt
```

---

## 11. Final Assessment & Part A Conclusion

Whole-run, Fig. 7 is not reproduced. Measured over the PIM region, GEMV without AAO matches the paper within about 0.4–0.7 points, consistent with one ~350 ns switch over a ~13.5 µs kernel. GEMV with AAO (+6.3% vs +9.65%), ADD, MUL and ReLU are not reproduced. Their gaps are decomposed by phase above. Empirical phase-split command tracking locates the ReLU slowdown in the readback phase (+896 ACT / +896 PRE, about 14 per channel), consistent with extra row cycles of about tRP + tRCD each; the #PMPRE counter shows exactly 1 mode-clearing precharge per channel, ruling out repeated rank-wide precharges during readback. Remaining region gaps are quantified by the overhead decomposition.

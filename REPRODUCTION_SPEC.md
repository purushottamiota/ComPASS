# ComPASS Reproduction Specification & Protocol Blueprint

> **Reference Paper:**  
> *"ComPASS: A Compatible PIM Protocol Architecture and Scheduling Solution for Processor-PIM Collaboration"*  
> IEEE/ACM International Symposium on Microarchitecture (MICRO 2025).

---

## 1. Specification Overview

This document specifies the exact technical parameters, architectural models, mathematical formulas, known paper ambiguities, and evaluation targets required to faithfully reproduce the ComPASS architecture across Samsung PIMSimulator and Ramulator.

---

## 2. Part A: Samsung PIMSimulator Protocol Specification

### 2.1 Baseline Protocol (Legacy HBM-PIM Mode-Switch)
- **Command Sequence**:
  $$\text{SB} \longrightarrow \text{HAB} \longrightarrow \text{HAB-PIM} \longrightarrow [\text{PIM Execution}] \longrightarrow \text{Exit HAB-PIM} \longrightarrow \text{SB}$$
- **Overhead**: Requires mode-register write (MRW) and synchronization barriers across all banks before any arithmetic can begin.
- **Activation**: Single-bank activation per ACT command; multi-bank requires sequential command issue.

### 2.2 Proposed Protocol (ComPASS PIM-ACT)
- **Single Command Issue**: Combines bank activation and in-memory arithmetic dispatch:
  $$\text{PIM-ACT} (\text{Bank}, \text{Row}, \text{Optype})$$
- **Multi-Bank Behavior**: Triggers simultaneous multi-bank activation across all banks in the rank configured for PIM execution.
- **Timing Constraint**:
  $$t_{RCD,\text{PIM-ACT}} = t_{RCD} + 1\text{ cycle}$$
  An additional 1-cycle penalty is enforced in the command scheduler (`CommandQueue.cpp`) to model internal decoding and SIMD logic activation.
- **Rank-Wide PRECHARGE**: Upon completing the PIM burst, rank-wide precharge closes all activated banks back to `Idle`.

### 2.3 Table 1: Full 35-Optype Encoding
ComPASS defines a 6-bit optype field ($2^6 = 64$ potential opcodes) mapped to architecture operations:
- **Compute Operations (0–31)**:
  - **MUL (8 optypes: 0–7)**: FP16 element-wise multiplication with varying register targets (e.g. GRF_A, GRF_B, SRF).
  - **ADD (8 optypes: 8–15)**: FP16 element-wise vector addition with varying register targets.
  - **MAD (8 optypes: 16–23)**: Multiply-Add ($A \times B + C$) SIMD operations.
  - **MAC (2 optypes: 24–25)**: Multiply-Accumulate ($Acc \leftarrow Acc + A \times B$).
  - **ReLU (4 optypes: 26–29)**: Rectified Linear Unit ($\max(0, x)$) with threshold variants.
  - *Reserved Compute*: 30–31.
- **Data Transfer Operations (32–63)**:
  - **MOV (5 optypes: 32–36)**: Register-to-register and memory-to-register moves (`MOV_GRF`, `MOV_SRF`, `MOV_BANK`, `MOV_CRF`, `MOV_OUT`).
  - *Reserved Transfer*: 37–63.

### 2.4 Architecture-Aware Optimization (AAO)
- **Problem**: In naive PIM execution, every micro-operation incurs standard DRAM cell access latencies ($t_{RCD}$, $t_{RAS}$, $t_{RP}$).
- **AAO Rule**:
  - For operations reading from physical DRAM bitlines to sense amplifiers: full $t_{RCD} = 14\text{ ns}$, $t_{RAS} = 32\text{ ns}$, $t_{RP} = 14\text{ ns}$.
  - For operations operating purely on bank-local registers (GRF/SRF/CRF) or intra-bank accumulators:
    $$t_{RCD,\text{AAO}} = \lceil 0.50 \times t_{RCD} \rceil, \quad t_{RAS,\text{AAO}} = \lceil 0.60 \times t_{RAS} \rceil, \quad t_{RP,\text{AAO}} = \lceil 0.50 \times t_{RP} \rceil$$
- **Target Speedups (Fig. 7 Reproduction)**:
  - GEMV: $+2.54\%$
  - Vector ADD: $+9.68\%$
  - Vector MUL: $+7.20\%$
  - Vector ReLU: $+8.40\%$

---

## 3. Part B: Ramulator Scheduling & Architecture Specification

### 3.1 Memory & System Hardware Parameters
- **Processor**: 16 Out-of-Order (OOO) CPU Cores, 4 GHz clock.
- **Memory Standard**: LPDDR5-6400 (3200 MHz bus, $t_{CK} = 0.3125\text{ ns}$).
- **Channels**: 4 independent memory channels.
- **Ranks**: 2 ranks per channel.
- **Banks**: 16 banks per rank (4 bank groups $\times$ 4 banks).
- **Row Buffer**: 2 KB per bank ($1024 \text{ columns} \times 16\text{-bit}$).
- **Granularity**: Burst Length BL16 = 32 Bytes per access.
- **Timings**:
  $nRCD=18$, $nRP=18$, $nRAS=42$, $nRC=60$, $nRTP=8$, $nWR=34$, $nFAW=16$, $nRRD=4$, $nRFC=140$, $nREFI=12480$.

### 3.2 Queueing Structure & Protocol Correctness
- **Baseline Policies (FR-FCFS & Batch / PAR-BS)**:
  - Both Host CPU requests and PIM requests enter the **same shared request queue** (`readq`/`writeq`), accurately reflecting uncoordinated memory bus interference.
- **ComPASS Balancers (ST-BLC, AT-BLC, and Proposed E-BLC)**:
  - Host requests go to 64-entry `readq` / `writeq`.
  - PIM requests go to a dedicated 2-entry `pim_q`.
  - Memory controller arbitrates between queues.
- **PREA Turnaround Penalty**:
  - When switching service from a PIM stream that activated an entire rank to a Host request (or vice-versa), an explicit rank-wide precharge (`PREA`) must complete before the target bank can be accessed.
  - Penalty: $t_{RP} = 18\text{ memory cycles} \approx 5.625\text{ ns}$.

### 3.3 Scheduling Algorithms
1. **FR-FCFS**: Row-hit first, then oldest request first across the queue.
2. **Batch (PAR-BS)**: Groups requests into batches of size $B = 64$. Serves older batch first; within a batch, applies FR-FCFS.
3. **ST-BLC (Static Balancer)**:
   - Fixed turns: $P = 64$ PIM operations, $N = 64$ Host operations.
   - *Row-Conflict PREA Latency Hiding*: If PIM encounters a row conflict, turn immediately switches to Host queue to overlap $t_{RP}$ latency with productive CPU memory accesses.
4. **AT-BLC (Adaptive Balancer)**:
   - Every refresh interval ($t_{REFI} = 12,480$ cycles), calculates real-time PIM progress:
     $$R_{\text{cur}} = \frac{W_{\text{cur}}}{T_{\text{cur}}}, \quad R_{\text{target}} = \frac{W_{\text{target}}}{T_{\text{target}}}$$
   - **50% Utilization Check**: Host quota $N$ is only allowed to increase if host requests consumed $> 50\%$ of the previous quota ($U_{\text{host}} > 0.50$).
   - **Adaptation Rules**:
     - If $R_{\text{cur}} > (1 + \lambda) R_{\text{target}}$ and $U_{\text{host}} > 0.50$: $N \leftarrow \min(N_{\max}, N + d)$
     - If $R_{\text{cur}} < (1 - \lambda) R_{\text{target}}$: $N \leftarrow \max(N_{\min}, N - d)$
     - Parameter bounds: $N_{\min} = 16$, $N_{\max} = 128$, step $d = 8$.

---

## 4. Paper Ambiguities, Inconsistencies & Resolutions

### 4.1 The $\lambda$ Inconsistency
- **The Issue in Paper**:
  - Section 6 describes the adaptation threshold as $\lambda = 0.01$ ($1\%$).
  - Section 7.4 states the tolerance margin as $\lambda = 0.1\%$ ($0.001$).
- **Resolution**:
  - Implement $\lambda$ as a configurable parameter (`blc_lambda`).
  - Default to $\lambda = 0.01$ (stable hysteresis preventing quota oscillation), and sweep $\lambda \in [0.001, 0.01, 0.05]$ in sensitivity analysis.

### 4.2 Paper Limitations
1. **Single PIM Architecture Assumption**: ComPASS assumes bank-SIMD PIM (like HBM-PIM / LPDDR5-PIM) and does not model inter-bank ring bus communication found in UPmem or AiM.
2. **OS Huge-Page Overhead**: Assumes contiguous physical memory mapping for PIM matrices without modeling TLB misses or OS 2MB huge-page compaction penalties.
3. **Coarse-Grained Turn Quota**: Standard AT-BLC adapts $N$ per-channel but ignores per-bank group congestion, which motivates our novel **E-BLC** contribution.

---

## 5. Software Architecture & Ramulator Version Rationale

- **Ramulator Choice**: We use the cycle-accurate C++ Ramulator engine (Kim et al., IEEE CAL 2015), as it provides cycle-by-cycle command scheduling, fine-grained queue inspection, and exact JEDEC state machine timings required to model row-conflict precharge overlaps.
- **Git Branching Layout**:
  - `main`: Clean, stable, verified baseline and ComPASS implementation.
  - `feature/pimact-aao`: PIMSimulator 35 optypes and AAO implementation.
  - `feature/scheduler-eblc`: Novel Elastic Bank-Conflict Aware Balancer.

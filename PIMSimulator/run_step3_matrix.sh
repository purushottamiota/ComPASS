#!/usr/bin/env bash
# =============================================================================
# Step 3: Initial Ground Truth Run -- 12-Configuration Cycle Matrix
# (compass_correction_doc.md § "Step 3: Initial Ground Truth Run")
#
# Runs the 4 paper-sized kernels in 3 protocol modes and records cycle counts.
# Expected output format per test: [CYCLES] <label>  total_cycles=<N>
#
# Usage (from PIMSimulator/ directory):
#   chmod +x run_step3_matrix.sh
#   ./run_step3_matrix.sh 2>&1 | tee step3_results.txt
# =============================================================================

SIM=./sim
LOG=${LOG:-step3_results.txt}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "=======================================================" | tee "$LOG"
echo " Step 3: 12-configuration ground-truth cycle matrix"   | tee -a "$LOG"
echo " $(date)"                                               | tee -a "$LOG"
echo "=======================================================" | tee -a "$LOG"

# Step 0: Ensure dataset .npy files exist
if [ ! -f "data/gemv/gemv_weight_4096x4096.npy" ] || [ ! -f "data/mul/eltmul_output_2097152.npy" ]; then
    echo "Dataset .npy files missing. Generating with python3 data/generate_all_data.py..." | tee -a "$LOG"
    python3 data/generate_all_data.py 2>&1 | tee -a "$LOG"
fi

# Step 1: Always rebuild incrementally
echo "Building (incremental)..." | tee -a "$LOG"
scons NO_LIBRARY=1 -j"$(nproc)" || { echo "Build failed" | tee -a "$LOG"; exit 1; }

run_mode() {
    local label="$1"
    local aao="$2"
    local filter="$3"
    echo ""                                                    | tee -a "$LOG"
    echo "--- MODE: $label (AAO=$aao) ---"                    | tee -a "$LOG"
    echo "Running filter: $filter"                             | tee -a "$LOG"
    
    # Run simulator with AAO env var, capturing output
    AAO="$aao" "$SIM" --gtest_filter="$filter" 2>&1 | tee -a "$LOG" | \
        grep -E "^\[CYCLES\]|\[CMD-COUNTS\]|\[B4-HIST\]|\[A3-(OK|FAIL|MISMATCH)\]|PASSED|FAILED|rror" || true
    echo "[EXIT] $label rc=${PIPESTATUS[0]}" | tee -a "$LOG"
}

# 1. Baseline (standard HBM-PIM protocol) with AAO=0 and AAO=1
# Filter specifically to PIMKernelFixture tests to avoid running extraneous bench tests
BASELINE_FILTER="PIMKernelFixture.gemv_paper_4kx4k:PIMKernelFixture.add:PIMKernelFixture.mul_paper_2m:PIMKernelFixture.relu_paper_4m"

echo ""                                                        | tee -a "$LOG"
echo "=== 1. BASELINE (standard HBM-PIM protocol, paper dims) ===" | tee -a "$LOG"
run_mode "Baseline (AAO=0)" 0 "$BASELINE_FILTER"
run_mode "Baseline (AAO=1)" 1 "$BASELINE_FILTER"
echo "(Sanity check: Baseline cycles under AAO=0 and AAO=1 must be identical)" | tee -a "$LOG"

# 2. PIM-ACT w/o AAO (AAO=0)
PIMACT_FILTER="PIMKernelFixture.gemv_pim_act:PIMKernelFixture.add_pim_act:PIMKernelFixture.mul_pim_act:PIMKernelFixture.relu_pim_act"

echo ""                                                        | tee -a "$LOG"
echo "=== 2. PIM-ACT w/o AAO (AAO=0) ==="                     | tee -a "$LOG"
run_mode "PIM-ACT w/o AAO" 0 "$PIMACT_FILTER"

# 3. PIM-ACT w/ AAO (AAO=1)
echo ""                                                        | tee -a "$LOG"
echo "=== 3. PIM-ACT w/ AAO (AAO=1) ==="                      | tee -a "$LOG"
run_mode "PIM-ACT w/ AAO" 1 "$PIMACT_FILTER"

# 4. A3 / Step 4 Cross-protocol bitwise verification
A3_FILTER="PIMKernelFixture.a3_*"
echo ""                                                        | tee -a "$LOG"
echo "=== 4. A3 / Step 4: Cross-protocol correctness + cycle comparison ===" | tee -a "$LOG"
run_mode "A3 Cross-check (AAO=1)" 1 "$A3_FILTER"
run_mode "A3 Cross-check (AAO=0)" 0 "$A3_FILTER"

echo ""                                                        | tee -a "$LOG"
echo "=======================================================" | tee -a "$LOG"
echo " Summary of Extracted [CYCLES]:"                         | tee -a "$LOG"
grep -E "\[CYCLES\]" "$LOG" | sort -u                          | tee -a "$LOG" || true
echo "=======================================================" | tee -a "$LOG"
echo " Grep checklist:"
echo "   [CYCLES]      -> see summary above"
echo "   [A3-FAIL]     -> $(grep -c "\[A3-FAIL\]" "$LOG" || true) mismatches"
echo "   [B4-HIST]     -> $(grep -c "\[B4-HIST\]" "$LOG" || true) mismatch events"
echo "=======================================================" | tee -a "$LOG"

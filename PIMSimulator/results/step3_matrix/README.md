# Step 3 Matrix Log Directory Manifest

This directory archives the ground-truth cycle matrices produced throughout the Part A protocol verification and tuning process.

## File Mapping & History

| Filename | Date (UTC) | Commit | Configuration & Flags | Description |
| :--- | :--- | :--- | :--- | :--- |
| `step3_prefix.txt` | 2026-11-01 07:05 | Prefix | Default | Initial 12-configuration run prior to B1–B10 fixes. |
| `after_B1.txt` | 2026-11-01 07:38 | `b805a02` | Default | Matrix after removing spurious `+1` from `tRAS`/`tRC` in `Rank.cpp`. |
| `after_B3.txt` | 2026-11-01 07:52 | `a8fccfa` | Default | Matrix after applying `tRCDWR` write latency in `PimActTiming.h`. |
| `after_B2.txt` | 2026-11-01 08:25 | `aa37334` | Default | Matrix after passing `activeOptype` to `PRECHARGE` for AAO `tRP` halving. |
| `after_step10.txt` | 2026-11-01 08:40 | `1f8acda` | Default | Matrix after implementing `NO_OPTYPE = 0xFF` sentinel (Issues B5 & B6). |
| `after_step12.txt` | 2026-11-01 08:54 | Uncommitted | B9 loop test | Failed run with B9 all-bank loop; GEMV failed with 57,270 mismatches. |
| `final_prea_aware.txt` | 2026-11-01 12:58 | `c171524` | `PIMACT_PREA_AWARE=1` | First PREA-aware run resolving GEMV regression. |
| `final_legacy_prea.txt`| 2026-11-01 17:59 | `b10b767` | `PIMACT_PREA_AWARE=0` | Legacy naive per-bank PRECHARGE policy comparison. |
| `final.txt` | 2026-11-01 17:45 | `b10b767` | Default (`PIMACT_PREA_AWARE=1`) | **Headline** final ground-truth matrix (PREA-aware scheduling). |

## Reproducibility Commands

To reproduce the headline results (`final.txt`):
```bash
cd PIMSimulator
python3 data/generate_all_data.py   # Seed 1113
scons NO_LIBRARY=1 -j$(nproc)
./run_step3_matrix.sh 2>&1 | tee results/step3_matrix/final.txt
```

To reproduce the legacy naive precharge comparison (`final_legacy_prea.txt`):
```bash
cd PIMSimulator
PIMACT_PREA_AWARE=0 ./run_step3_matrix.sh 2>&1 | tee results/step3_matrix/final_legacy_prea.txt
```

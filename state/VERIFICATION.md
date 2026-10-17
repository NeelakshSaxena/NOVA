# Verification Ledger

Only record commands that were actually run. Include environment details where relevant. Do not paste fabricated sample output into this ledger.

| Date/time | Phase | Command | Exit code | Result | Evidence path |
|---|---|---|---:|---|---|
| Not run | V0.1 | — | — | No verification has been performed yet | — |

For verbose output, save logs in `state/evidence/` and link them here. If logs cannot be saved, summarize the actual output and explain why.

## V0.1 — 2026-10-14 (Windows 11, MinGW.org GCC 6.3.0, no CMake)

| Command | Exit | Result | Evidence |
|---|---:|---|---|
| toolchain inspection (`command -v cmake ctest`, `g++ --version`) | — | cmake/ctest NOT FOUND; g++ 6.3.0, GNU Make 3.82.90 | state/evidence/V0.1/00_toolchain.log |
| CMake configure / build / CTest | not run | **Could not run: cmake missing (B-001)** | — |
| SUBSTITUTE: `g++ -std=c++17 -Wall -Wextra ...` build of `nova` and `test_cli` into a fresh temp dir | 0 / 0 | builds, no warnings | state/evidence/V0.1/01_direct_build.log |
| SUBSTITUTE: `test_cli`, `nova --help`, `nova --version`, `nova --bogus` | 0 / 0 / 0 / 2 | all expected | state/evidence/V0.1/02_direct_run.log |

Not verified: CMakeLists.txt itself (never parsed by CMake), CTest registration, the `PASS_REGULAR_EXPRESSION`/`WILL_FAIL` tests. Git status/diff review not possible: directory is not a git repository.

## V0.1 re-verification with CMake — 2026-10-14 (CMake 4.4.4, MinGW GCC 6.3.0, Windows 11)
Commands (env: `PATH` includes `C:\Program Files\CMake\bin` and `C:\MinGW\bin`; `build/` removed first):

| Command | Exit | Result | Evidence |
|---|---:|---|---|
| `cmake --version; ctest --version; g++ --version` | 0 | 4.4.4 / 4.4.4 / GCC 6.3.0 | state/evidence/V0.1/10_toolchain_cmake.log |
| `cmake -S . -B build -G "MinGW Makefiles"` | 0 | configured | 11_configure.log |
| `cmake --build build` | 0 | nova, test_cli built | 12_build.log |
| `ctest --test-dir build --output-on-failure` | 0 | 4/4 passed | 13_ctest.log |
| `nova --help` / `--version` / `--bogus` | 0 / 0 / 2 | as expected | 14_smoke.log |

V0.1 gate: PASS. Git status review not possible (not a git repository).

## V0.2 — 2026-10-14 (same environment as V0.1 re-verification)
| Command | Exit | Result | Evidence |
|---|---:|---|---|
| clean `cmake -S . -B build -G "MinGW Makefiles"` + `cmake --build build` (build/ removed first) | 0 / 0 | no warnings (-Wall -Wextra) | state/evidence/V0.2/01_clean_build.log |
| `ctest --test-dir build --output-on-failure` | 0 | 8/8 passed (4 V0.1 regression + lexer_unit + 3 tokens CLI tests) | 02_ctest.log |
| `nova tokens examples/basic.nova` / `examples/invalid_char.nova` | 0 / 1 | token dump; `invalid_char.nova:2:11: error: unexpected character '@'` | 03_smoke.log |
| `g++ -D_GLIBCXX_DEBUG -D_GLIBCXX_ASSERTIONS` build+run of test_lexer | 0 / 0 | passed with checked containers (substitute: MinGW 6.3 has no ASan/UBSan) | 04_bounds_checked.log |

Note: one lexer test initially failed (20000-line stress test); the test's expected token count (12/line) was wrong, the line has 11 tokens. Test expectation corrected, lexer unchanged.

## V0.2 re-verification note — 2026-10-14
While writing V0.3 tests I found that CTest `PASS_REGULAR_EXPRESSION` values containing `;` are split on `;` by CMake (list separator), which made `cli_tokens_basic` (and the first draft of `cli_ast_invalid_location`) pass vacuously. The V0.2 CTest result in `state/evidence/V0.2/02_ctest.log` was therefore partly unreliable for that test. Fixed: regexes no longer contain `;` (comment in CMakeLists.txt), and negative controls (deliberately wrong regexes) were run and failed as they should. The corrected `cli_tokens_basic` expectation is `4:8` (the earlier `4:9` was wrong, masked by the defect). The V0.2 lexer gate was re-checked by the V0.3 full run below (`lexer_unit`, `cli_tokens_*` pass genuinely).

## V0.3 — 2026-10-14
| Command | Exit | Result | Evidence |
|---|---:|---|---|
| clean configure (`-G "MinGW Makefiles"`) + build, `build/` removed first | 0 / 0 | no warnings (-Wall -Wextra) | state/evidence/V0.3/01_clean_build.log |
| `ctest --test-dir build --output-on-failure` | 0 | 13/13 passed (V0.1: 4, V0.2: lexer_unit + 3 token CLI tests, V0.3: parser_unit + 4 ast CLI tests, cli_unit) | 02_ctest.log |
| `nova ast` on basic / invalid_syntax / invalid_char / no args; `nova --help` | 0 / 1 / 1 / 2 / 0 | tree printed; `invalid_syntax.nova:2:14: error: expected expression, found ';'`; `invalid_char.nova:2:11: ...` | 03_smoke.log |
| `-D_GLIBCXX_DEBUG -D_GLIBCXX_ASSERTIONS` build+run of test_parser and test_lexer | 0 / 0 | pass (substitute: MinGW 6.3 has no ASan/UBSan) | 04_bounds_checked.log |
| Negative controls: wrong regex in `cli_ast_invalid_location`, `cli_tokens_basic`, `cli_ast_basic` | — | each reported Failed as intended, then restored | (run interactively; not saved to a log) |

Test-correction history (not weakening): wrong expected values in my own tests were corrected after checking actual behavior against the grammar (lexer stress count 11 not 12 tokens/line; `invalid_syntax.nova` error column 14 not 16; `basic.nova` final `;` at 4:8). In each case the implementation output was verified by hand against the source text.

## V0.4 — 2026-10-14 (CMake 4.4.4, MinGW GCC 6.3.0)
Run by `scripts/verify_phase.sh V0.4` (removes `build/`, clean configure+build, full CTest).
| Command | Exit | Result | Evidence |
|---|---:|---|---|
| clean configure + build (-Wall -Wextra) | 0 / 0 | no warnings | state/evidence/V0.4/01_clean_build.log |
| `ctest --test-dir build --output-on-failure` | 0 | 22/22 passed (all earlier gates + semantic_unit, tac_unit, 6 tac/run CLI tests) | 02_ctest.log |
| `nova tac` basic/undeclared/duplicate/divzero; `nova run` basic/divzero | 0/1/1/0, 0/1 | golden TAC; located diagnostics; `divzero.nova:4:9: error: runtime error: division by zero` after printing `10` | 03_smoke.log |
| `_GLIBCXX_DEBUG` builds of test_semantic, test_tac | 0 / 0 | pass | 04_bounds_checked.log |
| Negative controls (deliberately wrong regexes for cli_tac_basic, cli_run_divzero_msg) | — | both Failed as intended, then restored | interactive, not saved |

## V0.5 — 2026-10-14
Run by `scripts/verify_phase.sh V0.5` (clean build, full CTest).
| Command | Exit | Result | Evidence |
|---|---:|---|---|
| clean configure + build (-Wall -Wextra) | 0 / 0 | no warnings | state/evidence/V0.5/01_clean_build.log |
| `ctest --test-dir build --output-on-failure` | 0 | 25/25 passed (includes optimizer_unit and 2 opt CLI tests) | 02_ctest.log |
| `nova opt examples/optimize_demo.nova --show-all`, `nova opt/run examples/divzero.nova`, bad pass | 0 / 0 / 1 / 2 | before/after TAC per pass; failing division kept by `opt`; `run` still errors with exit 1 | 03_smoke.log |
| `test_optimizer` direct run | 0 | 4000-program differential corpus: 1836 with runtime errors, 3999 shrunk | 04_optimizer_unit_output.log |
| Mutation controls: 4 deliberately broken optimizer variants (DCE deletes failing div; fold folds /0; simplify x/x->1; propagate keeps stale constants) | — | each made the suite report failures (2576, 4240, 37, 608 failed checks); source restored and diffed identical | 05_mutation_controls.log |

## V0.6 — 2026-10-14 (CMake 4.4.4, MinGW.org GCC 6.3.0 32-bit target, Windows 11 build 26300, Intel Core Ultra 7 155H, 22 logical CPUs, 15789 MiB)
Run by `scripts/verify_phase.sh V0.6` (clean build, full CTest) plus experiments run on an otherwise idle machine.
| Command | Exit | Result | Evidence |
|---|---:|---|---|
| clean configure + build | 0 / 0 | no warnings (-Wall -Wextra) | state/evidence/V0.6/01_clean_build.log |
| `ctest --test-dir build --output-on-failure` | 0 | 32/32 passed: bench_unit (SHA-256 vectors, JSON, stats, records, CSV, oracle), bench_process (arg quoting/no shell, exit codes, stderr, timeout, output cap), bench_integration (all kernels build + pass oracle, incorrect kernel detected, build failure recorded, serialization round trip, missing-metadata detection, config errors, clean rerun, relative-path regression), 4 CLI tests | 02_ctest.log |
| `scripts/mutation_check.py scripts/mutations/v06.json` | 0 | 9/9 mutants killed (oracle disabled, sample dropped, quoting removed, timeout disabled, relative-path fix removed, ...) | 05_mutation_controls.log |
| `nova bench run --id=V0.6-baseline --reps=15 --warmup=2` (unpinned, default OS scheduling, tuning sizes, 3 kernels x {O2,O3}) | 0 | 12 records, all builds OK, all oracle PASS, 15 samples each | 10_baseline_run.log, benchmarks/results/V0.6-baseline/ |
| same with `--affinity=10 --high-priority` (pinned to logical CPU 4) | 0 | 12 records PASS; IQR/median mostly 0.6-9% (vs up to ~25% unpinned) | 11_baseline_pinned_run.log, benchmarks/results/V0.6-baseline-pinned/ |
| same pinned with `--include-ofast` (separate fast-math condition) | 0 | 18 records PASS; Ofast shown as `[fast-math]`, not compared with strict | 12_ofast_disclosure_run.log, benchmarks/results/V0.6-ofast-disclosure/ |
| `nova bench report <dir>` for each experiment | 0 / 0 / 0 | all records validate, environment complete | 13_report_*.log |
| `nova bench list` / `report <missing>` / bad option | 0 / 1 / 2 | as expected | 03_smoke.log |

Observations (descriptive, this machine/compiler only; every number is in the saved results): matmul O2 vs O3 was inconclusive (overlapping IQRs) at both sizes in all three experiments; stencil O3 median ratio O2/O3 was 1.31-1.39 with disjoint IQRs at both sizes in all three experiments; vector_add was **unstable across experiments** (n=1M: ratio 1.24 disjoint unpinned, 1.08 overlap pinned, 1.08 disjoint in the Ofast-disclosure run; n=4M: 1.055 overlap unpinned, 1.049 and 1.034 disjoint pinned) and must be treated as inconclusive. Holdout sizes were NOT run (reserved). No speedup claim is made from these runs.

Defects found by real use and fixed in this phase: (1) a '/'-separated *relative* program path could not be launched on Windows when the working directory differed from the launcher's directory (all earlier tests used absolute paths); regression test `test_relative_paths` changes directory first and the mutation `relative program path fix removed` is killed. (2) My first mutation-test attempt for that fix survived, exposing that the regression test passed vacuously; the test was strengthened before being accepted.

## V0.7 — 2026-10-14 (same environment as V0.6)
Run by `scripts/verify_phase.sh V0.7`, then equal-budget experiments on an idle machine (pinned: `--affinity=10 --high-priority`).
| Command | Exit | Result | Evidence |
|---|---:|---|---|
| clean configure + build | 0 / 0 | no warnings | state/evidence/V0.7/01_clean_build.log |
| `ctest --test-dir build --output-on-failure` | 0 | 39/39 passed (adds tune_unit, tune_integration — all lifecycle paths, budget 0/1/2/3/6/10, determinism, invalid-config rejection before execution, result files, CLI exit codes — and 5 CLI tests) | 02_ctest.log |
| `scripts/mutation_check.py scripts/mutations/v07.json` | 0 | 11/11 mutants killed (budget off-by-one, incorrect candidate ranked, whitelist off, policy ignored, baselines dropped, seed ignored, holdout accepted, exit code 0 without winner, counter, failure reasons dropped) | 05_mutation_controls.log |
| `nova tune <k> --budget=4 --strategy=fixed|random --seed=1..3 --reps=15 --warmup=2 --affinity=10 --high-priority` for k in stencil, vector_add, matmul (12 runs) | 0 x12 | every run: 4 searched + 2 baseline builds, all eligible; saved under benchmarks/results/V0.7-*/ | 10_equal_budget_runs.log, 20_explain_*.log |

Observations (descriptive; single tuning size per family, 1 fixed + 3 random seeds at B=4, one machine; no conclusion about fixed vs random is drawn): the selected candidate beat the best baseline's median in every run, with disjoint IQRs for stencil and matmul and overlapping IQRs for 3 of 4 vector_add runs. See the explain logs for the per-candidate tables. These runs only exercise the pipeline; the adaptive-vs-random question is V0.8.

## V0.8 — 2026-10-14/15 (same environment; all timing pinned: `--affinity=10 --high-priority`, 15 repetitions, 2 warm-ups)
| Command | Exit | Result | Evidence |
|---|---:|---|---|
| `scripts/verify_phase.sh V0.8` (clean build + full CTest) | 0 | 47/47 passed (adds adaptive_unit, study_unit, adaptive_integration + 7 CLI tests) | state/evidence/V0.8/01_clean_build.log, 02_ctest.log |
| `scripts/mutation_check.py scripts/mutations/v08.json` | 0 | 14 mutants: 13 killed; 1 documented equivalent (study-level holdout filter is duplicated by a policy-level guard, which is mutation-tested separately) | 05_mutation_controls.log |
| `nova table <k> --role=all --reps=15 --warmup=2 --affinity=10 --high-priority` for stencil, vector_add, matmul | 0 x3 | 3 families x 4 sizes (2 tuning + 2 holdout) x 24 configurations = 288 records, all oracle-PASS | 10_table_*.log, benchmarks/results/V0.8-table-* |
| `nova history --tables=...` | 0 | 132 tuning-only history entries | benchmarks/history/V0.8-tuning-history.json |
| `nova study --seeds=30` over the 12 tables (methods fixed, random, adaptive, 3 ablations, baseline-O2/O3; budgets 1,2,3,4,6,8,12,22) | 0 | 7260 replayed runs | 20_study_run.log, benchmarks/results/V0.8-study/ |
| Real end-to-end: 27 x `nova tune` (3 families x {random, adaptive, adaptive-no-features} x seeds 1-3, B=6, adaptive with history of other workloads) each followed by `nova evaluate` on the 2 holdout sizes | all 0 | 54 result directories, 0 failures | 30_real_done.log, 31_*.log, 32_*.log, benchmarks/results/V0.8-real-* |

Results (descriptive; full tables in benchmarks/results/V0.8-study/study_summary.csv; interpretation belongs to the V0.9 report):
- Replay study, mean over 3 families x 2 tuning sizes x 30 seeds, speedup vs best of -O2/-O3, tuning size / holdout sizes: B=1 random 1.078/1.055, adaptive 1.114/1.098, adaptive-no-features 1.122/1.083; B=4 random 1.135/1.097, adaptive 1.152/1.109; B=8 random 1.148/1.106, adaptive 1.157/1.112; B=22 (exhaustive) every method 1.159/1.109. Paired by seed, adaptive minus random on holdout: B=1 +0.043 (146/180 wins), B=2 +0.027, B=4 +0.012 (114/180), B=8 +0.006 (79/180, 73 ties). The advantage shrinks as the budget grows and vanishes at exhaustive budget. Removing the workload-feature prior (adaptive-no-features) did NOT lower quality at B=1 (tuning 1.122 vs 1.114), so these data do not show that the descriptors help; the history signal helps slightly at B=1,4 (adaptive vs adaptive-no-history 1.114 vs 1.105 at B=1). Fixed was the worst method at small budgets.
- Real runs (B=6, means over 3 seeds per family): tuning wall time random 16.2 s vs adaptive 20.5 s (+27%, the pilot stage); holdout speedup random 1.100, adaptive 1.111, adaptive-no-features 1.095. With only 3 seeds per cell and visible run-to-run noise these differences are within noise; no real-run conclusion is drawn.

## V0.9 — 2026-10-15
| Command | Exit | Result | Evidence |
|---|---:|---|---|
| `scripts/verify_phase.sh V0.9` (clean build + full CTest) | 0 | 48/48 passed (adds `report_generation`: 21 Python tests) | state/evidence/V0.9/01_clean_build.log, 02_ctest.log |
| `python scripts/make_report.py` | 0 | report/ generated from saved raw data: 700 traced values, 0 warnings; template lint clean | report/report.md, report/traceability.json, report/manifest.json |
| `python tests/report/test_make_report.py` | 0 | determinism (two runs byte-identical), every value has existing source files, every decimal number in the report is a traced value, independent recomputation of 4 values from raw CSV/JSON matches, missing/malformed inputs are errors (and `--warn-only` degrades without inventing values), template lint rejects typed numbers, verdict-rule unit tests | (CTest) |
| report mutation controls | - | 4/5 killed; 1 equivalent (defense in depth) | state/evidence/V0.9/05_report_mutation_controls.log |
| reference check | - | 6 of 8 sources fully verified, OpenTuner partly (publication metadata not re-verified), Google Benchmark user guide not re-fetched; one wording fix in docs/smartpass.md | state/evidence/V0.9/10_references_check.md |

Hypothesis verdicts produced by the fixed rule (report section 9): H1 SUPPORTED, H2 SUPPORTED, H3 INCONCLUSIVE, H3t INCONCLUSIVE, H4 INCONCLUSIVE, H4t INCONCLUSIVE, H5 INCONCLUSIVE, H5t INCONCLUSIVE.

## V1.0 release gate — 2026-10-15
| Command | Exit | Result | Evidence |
|---|---:|---|---|
| `scripts/verify_phase.sh V1.0` (clean build + CTest in the tree) | 0 | 49/49 | state/evidence/V1.0/01_clean_build.log, 02_ctest.log |
| clean copy of the repository (no build products/caches) -> configure, build, CTest | 0 / 0 / 0 | 49/49, no warnings | 30_cleancopy_build.log, 31_cleancopy_ctest.log |
| `python scripts/make_report.py` in the clean copy | 0 | report.md and traceability.json byte-identical to the tree's | 32_cleancopy_report.log |
| whole suite with `-D_GLIBCXX_DEBUG -D_GLIBCXX_ASSERTIONS -fstack-protector-all` | 0 (after fix) | first run FAILED 5 tests: checked containers aborted on `std::swap(v[i-1], v[j])` with `j == i-1` in `bench::shuffle` (self-move of a struct holding a vector). Fixed with a guard; second run 48/48. Regression test `shuffle_checked` aborts on the old code. | 20_checked_build.log, 21_checked_ctest.log |
| sanitizers (`-fsanitize=address/undefined/thread/leak`) | link error | NOT_RUN: runtimes not installed for MinGW.org GCC 6.3.0 | (command output recorded in docs/RELEASE.md) |
| `python tests/report/test_make_report.py` | 0 | 21 tests OK | CTest `report_generation` |
| `scripts/make_demo.sh` | 0 | docs/DEMO.md regenerated from real runs (17 transcripts with exit codes) | docs/DEMO.md |

Checklist and outcome: docs/RELEASE.md (RELEASED for the verified configuration; items outside it listed as not verified).

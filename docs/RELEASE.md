# NOVA V1.0 release gate

**Outcome: RELEASED for the verified configuration** (Windows 11, CMake 4.4.4, MinGW.org GCC 6.3.0 / 32-bit x86 target,
one Intel Core Ultra 7 machine). Every mandatory check below was run on the current tree and passed; checks the toolchain
cannot perform are reported as NOT_RUN with the reason and a substitute. Nothing outside the verified configuration is claimed
(see "Not verified"). No independent reviewer audited this work; the checks were run by the implementer.

Evidence lives in `state/evidence/<phase>/` and is indexed in `state/VERIFICATION.md`. Statuses: PASS / FAIL / NOT_RUN / BLOCKED.

| # | Checklist item | Status | Evidence |
|---|---|---|---|
| 1 | Clean configure/build succeeds | PASS | `state/evidence/V1.0/01_clean_build.log` (tree), `30_cleancopy_build.log` (copy of the repository without build products or caches: configure and build exit 0, no warnings with `-Wall -Wextra`) |
| 2 | All CTest tests pass | PASS | `state/evidence/V1.0/02_ctest.log`: 49/49; clean copy `31_cleancopy_ctest.log`: 49/49; includes 21 Python report tests |
| 3 | Sanitizer tests run where supported; status reported | NOT_RUN (unsupported) + substitute PASS | AddressSanitizer, UBSan, TSan and LeakSanitizer runtimes are not installed for MinGW.org GCC 6.3.0 (`cannot find -lasan/-lubsan/-ltsan/-llsan`). Substitute: the whole suite rebuilt with `-D_GLIBCXX_DEBUG -D_GLIBCXX_ASSERTIONS -fstack-protector-all` passes 48/48 (`20_checked_build.log`, `21_checked_ctest.log`) — and that run **found a real defect** (a self-move in `shuffle`), fixed and covered by a regression test that aborts with the old code (`tests/test_shuffle_checked.cpp`) |
| 4 | NOVA Script valid/invalid examples behave as documented | PASS | CTest `cli_*` tests (tokens/ast/tac/run/opt, locations, exit codes); real transcripts in `docs/DEMO.md`; `docs/grammar.md`, `docs/language.md` |
| 5 | Optimizer differential tests pass | PASS | `optimizer_unit`: 4000 deterministic programs, each pass alone and two pipeline orders, output/error/location preserved; 4 scripted mutants killed (`state/evidence/V0.5/`) |
| 6 | SmartPass budget and correctness gates pass | PASS | `tune_unit`, `tune_integration`: budgets 0/1/2/3/6/10 build exactly baselines + min(B, space); incorrect, non-building, hanging and oversized candidates never win; 11 mutants killed (`state/evidence/V0.7/05_mutation_controls.log`) |
| 7 | Fixed and random search baselines exist | PASS | `nova tune --strategy=fixed` or `--strategy=random`; runs in `benchmarks/results/V0.7-*`; GCC -O2/-O3 baselines in `V0.6-*` |
| 8 | Adaptive strategy evaluated under equal budgets | PASS | replay study `benchmarks/results/V0.8-study` (budgets 1..22, 30 seeds, 8 methods incl. 3 ablations) and 27 real end-to-end runs; report section 8.2 and 9 |
| 9 | Holdout sizes evaluated | PASS | holdout tables `V0.8-table-*` and `nova evaluate` runs `V0.8-real-*-holdout`; leakage rules enforced and mutation-tested (`state/evidence/V0.8/05_mutation_controls.log`; one mutant is equivalent by design and documented) |
| 10 | Raw timing samples and metadata are present | PASS | every `results.json` holds all raw samples with environment, flags, source hash, seed; `raw_timings.csv`; `bench report` validates records |
| 11 | Reports are generated from raw data | PASS | `python scripts/make_report.py`; byte-identical regeneration in the clean copy (`32_cleancopy_report.log`); `tests/report/test_make_report.py` |
| 12 | No unsupported claims or invented numbers | PASS (with scope) | template lint forbids typed-in numbers; every decimal number in `report/report.md` is a traced value (test); docs were searched for numeric claims and the one unmeasured claim (rounding magnitude in `docs/benchmarks.md`) was removed. Qualitative claims are limited to the measured workloads |
| 13 | All references are checked | PASS (partial) | `state/evidence/V0.9/10_references_check.md`: GCC manual, Google Benchmark interleaving, SPEC rules, Souper, egg, LLVM tutorial verified; OpenTuner site verified, its ACM metadata and the Google Benchmark user guide were not re-fetched — stated in the report |
| 14 | Every phase status is supported by evidence | PASS | `state/ROADMAP_STATUS.md` links evidence for V0.1 to V0.9; the final gate re-ran the full suite (every earlier phase's tests) in the tree and in a clean copy; the mutation-control logs were produced at each phase and not repeated, but the only source changes after them are a self-swap guard in `shuffle` (covered by its own checked test) and one help-text line |
| 15 | Known issues and scope limits are visible | PASS | README "Known limitations", `state/KNOWN_ISSUES.md`, report sections 10 and 11 |

## Not verified (explicitly out of the release claim)
- Any platform other than Windows 11 with MinGW.org GCC 6.3.0; any newer GCC; any other CPU. The POSIX process runner
  (`fork`/`execvp`, Linux affinity) has never been compiled or run.
- Sanitizer-clean status (no sanitizer available).
- A git checkout: the project directory is not a git repository, so "clean checkout" was verified with a copy that excludes
  build products, caches and untracked scratch directories.
- Generalization of any search result beyond the three kernels and twelve measured workloads.

## Mandatory-check summary
No mandatory correctness, reproducibility or research-integrity check was skipped. The only NOT_RUN item (sanitizers) is
"where supported" by the checklist and is not supported by the toolchain; a substitute was run and is reported.

# Phase Log

Append entries; do not erase history.

## Entry template

### YYYY-MM-DD — Vx.y — short objective
- Status before:
- Change:
- Files:
- Verification commands:
- Exit codes/results:
- Evidence:
- Failure diagnosis / retries:
- Status after:
- Next action:

### 2026-10-14 — V0.1 — foundation scaffold
- Status before: NOT_STARTED
- Change: added CMake project, `nova_core` library, `nova` CLI (--help/--version/unknown-arg error), unit test executable, CTest registrations, .gitignore, README build instructions.
- Files: CMakeLists.txt, include/nova/cli.hpp, src/cli.cpp, src/main.cpp, tests/test_cli.cpp, .gitignore, README.md, state/*
- Verification commands: see state/VERIFICATION.md
- Exit codes/results: direct-g++ substitute passes; CMake/CTest not runnable (cmake absent).
- Evidence: state/evidence/V0.1/
- Failure diagnosis / retries: none for code. An initial PowerShell g++ attempt exited 1 with no output (environment/PATH artifact, not investigated further); same compile succeeded under bash with MinGW on PATH.
- Status after: BLOCKED
- Next action: install CMake, run the documented build/test, record evidence, then re-evaluate.

### 2026-10-14 — V0.1 — re-verification after CMake install
- Status before: BLOCKED
- Verification: clean CMake configure/build, CTest (4/4), smoke tests — all exit as expected.
- Evidence: state/evidence/V0.1/10-14_*.log
- Status after: COMPLETE
- Next action: V0.2 lexer.

### 2026-10-14 — V0.2 — lexer
- Status before: NOT_STARTED (V0.1 COMPLETE)
- Change: TokenKind/Token/SourceLocation/Diagnostic, `lex()`, `dump_tokens()`, `nova tokens <file>`, examples/invalid_char.nova, tests/test_lexer.cpp, shared tests/check.hpp.
- Policies: 1-based line/col, bytes, tab = 1 column, no comments, keywords case-sensitive, `12ab` is one invalid-number diagnostic, ints must fit int64, `nova tokens` prints nothing to stdout when diagnostics exist (exit 1).
- Verification: state/evidence/V0.2/
- Failure diagnosis: stress-test expected count miscounted (test error), fixed; a shell-quoting slip in test_cli.cpp (literal newline) fixed.
- Status after: COMPLETE

### 2026-10-14 — V0.3 — recursive-descent parser and AST
- Status before: NOT_STARTED (V0.2 COMPLETE)
- Change: AST node types (include/nova/ast.hpp, src/ast.cpp), recursive-descent parser (include/nova/parser.hpp, src/parser.cpp), `nova ast <file>`, docs/grammar.md, examples/invalid_syntax.nova, tests/test_parser.cpp (structural tree checks, 40+ error locations/messages, prefix truncation and 5000 pseudo-random token soups, depth-limit tests), CLI tests.
- Policies: first syntax error only; at most one unary minus per operand (grammar); expression depth limit 1000 (keeps recursion/destruction bounded); lexer errors reported instead of parsing.
- Verification: state/evidence/V0.3/
- Failure diagnosis: CTest regexes containing ';' were silently split (vacuous pass) — found via a mismatching expectation, fixed, negative controls added; shell/heredoc escape mangling of `
` in generated files was fixed by checking compile errors.
- Status after: COMPLETE

### 2026-10-14 — V0.4 — semantics, symbol table, TAC
- Change: SymbolTable + analyzer (reports all errors), TAC IR/generator/printer, AST and TAC reference interpreters, `nova tac`, `nova run`, docs/language.md (explicit overflow wrap, truncating division, division-by-zero = runtime error, declaration rules), scripts/verify_phase.sh.
- Verification: state/evidence/V0.4/. No failures encountered this phase.
- Status after: COMPLETE

### 2026-10-14 — V0.5 — basic TAC optimizer
- Change: fold, propagate, simplify, dce passes + fixed-point driver (cap 10000 sweeps, `converged` flag), `nova opt [--passes=] [--show-all]`, docs/optimizer.md, tests/test_optimizer.cpp incl. 4000-program differential corpus.
- Failures during development: one wrong test expectation (top-level op writes directly into target var) corrected; a shell-escaping slip in a test string fixed. No optimizer defects found by the corpus.
- Status after: COMPLETE

### 2026-10-14 — V0.6 — benchmark lab and GCC baselines
- Change: kernels (matmul, vector_add, stencil) + common.hpp; harness (process runner with timeout/caps/affinity, environment capture, SHA-256 source hash, independent oracle with documented tolerance, interleaved seeded timing, JSON/CSV storage and validation, report); `nova bench list|run|report`; docs/benchmarks.md; scripts/mutation_check.py + mutation specs.
- Experiments saved under benchmarks/results/ (unpinned, pinned, -Ofast disclosure). Holdout sizes untouched.
- Failure diagnosis: see VERIFICATION.md (relative-path launch bug, vacuous regression test).
- Status after: COMPLETE

### 2026-10-14 — V0.7 — bounded SmartPass
- Change: whitelist + canonical search space (2 baselines + 22 candidates), fixed/random strategies, candidate lifecycle, correctness-gated ranking with policy, budget accounting, `nova tune`/`nova explain`, shared `validate_and_time` stage refactored out of the V0.6 harness, RunRecord gained `lifecycle`/`rank` (optional on load for old files), docs/smartpass.md, fixture kernels for failure paths.
- V0.6 regression: all V0.6 tests and saved results still load (report on V0.6 results validated through the optional-field path).
- Failure diagnosis: two ad-hoc test bugs (iterator-from-two-temporaries UB in a test; a stale mutation anchor) fixed; no product defects found by the mutation controls.
- Status after: COMPLETE (strategy is NOT adaptive).

### 2026-10-14/15 — V0.8 — adaptive, workload-aware search
- Change: workload descriptors, transparent scoring policy (prior + history + feedback) with three ablations, persisted tuning-only history, adaptive branch of `nova tune` (pilot stage, manifest records every score component and pilot sample), `nova evaluate` (one-way holdout scoring), `nova table` (exhaustive ground truth), `nova study` (replay with leakage rules), `nova history`; docs/adaptive.md.
- Defects found by tests: `--record-history` without `--history` was validated only after a full tuning run (fixed: validated up front); two mutation-test gaps closed (study holdout-leak test strengthened; "reports oracle speed" mutant added); one mutant is equivalent by design (documented).
- Honest outcome: adaptive beat random at small budgets in the replay study and converges to equal at exhaustive budget; features did not show an independent benefit; real-run differences are within noise.
- Status after: COMPLETE

### 2026-10-15 — V0.9 — evaluation report
- Change: scripts/make_report.py (stdlib only; traceability registry; template lint; deterministic bootstrap; clustered verdict rule), report/template.md, generated report/, tests/report/test_make_report.py (CTest), reference check.
- Defects found by tests: repository-relative path helper crashed when `--data` was on another drive (fixed); a missing-column input crashed instead of reporting (fixed: fail early with a message); the report did not notice a missing table directory that the study had used (fixed: study manifest vs. loaded tables consistency check incl. source hashes); a first verdict rule ignoring workload clustering gave over-confident SUPPORTED verdicts and was replaced.
- Status after: COMPLETE

### 2026-10-15 — V1.0 — release gate
- Actions: docs audit (developer guide and roadmap no longer advertise commands that do not exist; README rewritten; `nova history` added to --help), unmeasured numeric claim removed from docs/benchmarks.md, demo transcripts generated, clean-copy build/test/report reproduction, checked-container full-suite run (found and fixed the `shuffle` self-move), sanitizer availability probed, release checklist written.
- Outcome: RELEASED for the verified configuration; limitations and unverified areas listed in docs/RELEASE.md and README.
- Status after: RELEASED

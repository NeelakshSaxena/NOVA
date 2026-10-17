# Architecture and Research Decisions

## D-001 — Two separate tracks
**Decision:** NOVA Script compiler and SmartPass are separate modules.
**Reason:** The educational compiler demonstrates course concepts; SmartPass controls experiments on C++ compiled by GCC.
**Consequence:** Do not describe SmartPass as NOVA compiling arbitrary C++.

## D-002 — C++17, CMake, terminal-only
**Decision:** Use C++17, CMake and a terminal CLI.
**Reason:** Scope is explainable and course-aligned.
**Consequence:** No GUI or full C++ frontend in the initial releases.

## D-003 — Initial optimizer is educational and conservative
**Decision:** Begin with constant folding, propagation, safe algebraic simplification and dead-code elimination on NOVA TAC.
**Reason:** Each pass can be explained and tested.
**Consequence:** No e-graphs, SMT synthesis or LLVM pass integration in initial scope.

## D-004 — Research contribution is a hypothesis
**Decision:** Evaluate adaptive, workload-aware search under a fixed budget against fixed and random baselines.
**Reason:** Autotuning is prior art; the value must be shown experimentally.
**Consequence:** No speedup or novelty claims without evidence.

## D-005 — CLI structure and test style (V0.1)
**Decision:** CLI logic lives in `nova_core` (`run_cli(args, out, err)`); `main.cpp` is a thin wrapper. Tests are a dependency-free executable plus CTest end-to-end checks of the real binary.
**Reason:** Testable without spawning processes; no third-party download needed.
**Consequence:** Exit code 2 for usage errors/unknown arguments, 0 for --help/--version. Extra arguments after --help/--version are rejected.

## D-006 — Evidence substitution when CMake missing
**Decision:** Direct `g++` build used as a clearly labelled substitute check; it does not satisfy the CMake/CTest gate.

## D-007 — Lexer error recovery (V0.2)
Invalid characters/numbers produce a diagnostic and no token; lexing continues so all lexical errors are reported. Comments are not part of the language (`#` etc. are rejected).

## D-008 — Parser policy (V0.3)
Stop at first syntax error (one deterministic diagnostic). Grammar allows a single unary minus (`--x` rejected). Expression depth limit 1000 reported as a diagnostic rather than risking stack overflow. Semantics (undeclared variables, division by zero) are deliberately not checked until V0.4.

## D-009 — Language semantics (V0.4)
64-bit wrapping arithmetic; division truncates toward zero; INT64_MIN/-1 wraps; division by zero is a runtime error (not a compile error); single global scope; initializer checked before declaration. TAC temporaries are `$tN` (cannot clash with identifiers). `nova run` added as the recommended reference interpreter.

## D-010 — Optimizer safety rules (V0.5)
A division that is not by a non-zero constant counts as side-effecting (may stop execution) and is never deleted or folded when the divisor is zero. Passes see only straight-line TAC; no copy propagation or CSE (out of scope). Iteration cap 10000 with explicit `converged` flag.

## D-011 — Measurement protocol choices (V0.6)
Kernels take n/seed at run time and time only the compute region in-process (QueryPerformanceCounter); each sample is a fresh process. Fixed flags `-std=c++17 -msse2 -mfpmath=sse` (32-bit MinGW GCC defaults to x87 excess precision). Oracle = independent long-double reference with relative tolerance 1e-9. Interleaved rounds with seeded shuffle. Because the hybrid CPU made unpinned timing noisy (IQR/median up to ~25%), pinned runs (`--affinity=10 --high-priority`, logical CPU 4) are recorded as a separate, labelled condition; later phases that compare candidates must use one condition for all candidates (planned: pinned).

## D-012 — Benchmark sizes
Tuning sizes and holdout sizes are fixed in the registry, disjoint, and holdout sizes are not run in V0.6.

## D-013 — SmartPass accounting and policy (V0.7)
Budget B counts searched candidates (built+validated); baselines O2/O3 always evaluated in addition and not counted; no retries. Winner = lowest median among ELIGIBLE (baselines included). Policy: binary size <= 2.0x baseline -O2. Whitelist excludes all fast-math flags. Tuning uses one input size and refuses registered holdout sizes. Adaptive is a separate strategy (V0.8).

## D-014 — Adaptive policy and experiment design (V0.8)
Heuristic scoring policy, not ML (constants in docs/adaptive.md, fixed before the study; author had seen V0.6/V0.7 timings). Study = replay against exhaustive saved tables (24 configurations x 4 sizes x 3 families) with pilot-sample resampling, complemented by 27 real end-to-end runs for overhead. Holdout tables only score finished choices. Adaptive costs extra pilot time which is reported.

## D-015 — Report policy (V0.9)
Reports are generated from saved raw data only; prose may not contain typed-in numbers (lint). Verdicts follow a fixed rule that also requires consistency across workloads, because seeds of one workload are not independent evidence. Inference is limited to the measured workloads.

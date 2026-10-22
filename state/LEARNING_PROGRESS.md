# NOVA Learning Progress & Mastery Tracker

**Student:** Technical Learner / Viva Candidate  
**Tutor:** NOVA Technical Mentor & Compiler Lab Instructor  
**Current Date:** 2026-10-18  
**Repository Working Tree:** `G:\Projects\NOVA`  
**Active Phase:** V1.0 Release Inspection & Hands-On Foundation  
**Active Lesson:** Lesson 1 — Reality Map & System Architecture  

---

## 1. Mastery Checklist & Lesson Roadmap

| # | Lesson Title | Status | Date Completed | Key Concept / Skill Verified |
|---|---|---|---|---|
| 1 | Reality Map & System Architecture | IN_PROGRESS | — | Two-track separation, non-C++ compiler boundary, actual tree state |
| 2 | Build System & CLI Architecture | NOT_STARTED | — | CMakeLists.txt, CLI dispatch, exit codes, process isolation |
| 3 | C++ Project Layout & Entry Point | NOT_STARTED | — | `main.cpp`, `nova_core`, memory management, RAII |
| 4 | Lexer & Token Stream | NOT_STARTED | — | Lexemes, source locations, error recovery vs termination |
| 5 | Grammar, Parser & Abstract Syntax Tree (AST) | NOT_STARTED | — | Recursive descent, precedence climbing, tree nodes |
| 6 | Semantic Analysis & Symbol Table | NOT_STARTED | — | Declare-before-use, scoping, static checks vs runtime |
| 7 | Three-Address Code (TAC) & Linear IR | NOT_STARTED | — | Quadruples, temporaries `$tN`, instruction flattening |
| 8 | Optimizer Passes & Semantics Preservation | NOT_STARTED | — | Constant fold, propagate, algebraic simplify, DCE, fixed point |
| 9 | GCC Optimization Pipeline & Flag Anatomy | NOT_STARTED | — | -O0..-O3, -Ofast caveats, loop vectorization, SSE2/AVX |
| 10 | Benchmark Kernels & Correctness Oracle | NOT_STARTED | — | matmul/stencil/vector_add, 5 statistical invariants |
| 11 | Timing Noise, CPU Affinity & Statistics | NOT_STARTED | — | QueryPerformanceCounter, core pinning, median vs mean, IQR |
| 12 | SmartPass Candidate Lifecycle & Budgets | NOT_STARTED | — | Evaluation budgets, candidate rejection, binary size policy |
| 13 | Adaptive Workload-Aware Search | NOT_STARTED | — | Static feature extraction, history weighting, pilot scoring |
| 14 | Research Novelty, Ablation & Holdout Generalization | NOT_STARTED | — | Preventing data leakage, holdout input evaluation |
| 15 | Reproducible GCC vs NOVA SmartPass Experiment | NOT_STARTED | — | Fair benchmark execution, speedup & runtime reduction math |
| 16 | Viva & Project Defense Simulation | NOT_STARTED | — | Live demo execution, answering trick questions |

---

## 2. Lab Verification Log

| Timestamp | Command Executed | Exit Code | Verified Outcome |
|---|---|---|---|
| 2026-10-18 | `git status` | 0 | Branch `main`, up to date with `origin/main`, clean except untracked `presentation_survival_guide.md` |
| 2026-10-18 | `git log -n 5 --oneline` | 0 | Head commit `f7fed83` ("first sync") |
| 2026-10-18 | `.\build\nova.exe --help` | 0 | Full CLI command suite verified (tokens, ast, tac, run, opt, bench, tune, explain, evaluate, table, study, history) |
| 2026-10-18 | `.\build\nova.exe --version` | 0 | `nova 0.1.0` |
| 2026-10-18 | `.\build\nova.exe tokens examples/basic.nova` | 0 | Token stream with line:col emitted correctly |
| 2026-10-18 | `ctest --test-dir build --output-on-failure` | 0 | 49/49 tests passed (100% pass rate in 378.45s: unit, integration, CLI, and Python report tests) |

---

## 3. Notes & Active Observations

- **Track Separation:** Track 1 (NOVA Script compiler) is completely separate from Track 2 (SmartPass). NOVA does NOT compile C++ and does NOT replace GCC.
- **Environment Verified:** Windows 11, MinGW.org GCC 6.3.0 (32-bit x86 target), CMake 4.4.4, Intel Core Ultra 7 155H.
- **Artifacts & Data:** Pre-existing raw timing datasets are preserved in `benchmarks/results/` (`V0.6-*`, `V0.7-*`, `V0.8-*`). Report data in `report/` is traced via `report/traceability.json`.

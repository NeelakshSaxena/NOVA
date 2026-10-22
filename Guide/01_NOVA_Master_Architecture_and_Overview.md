# 01 — NOVA Master Architecture & Executive Overview

> **Target Platform:** Google NotebookLM Source Document  
> **Project:** NOVA — An Explainable, Workload-Aware Compiler Optimization Framework for Numerical Programs  
> **Authors & Tech Stack:** C++17, CMake 4.4.4, MinGW.org GCC 6.3.0 (32-bit x86 target), Windows 11  

---

## 1. Executive Summary
NOVA is an explainable compiler optimization framework designed with two completely separate tracks:
1. **Track 1: NOVA Script Educational Compiler:** A custom C++17 compiler implementing lexical analysis, recursive-descent parsing with precedence climbing, AST representation, semantic analysis with a symbol table, Three-Address Code (TAC) linear intermediate representation, a 4-pass fixed-point optimizer, and reference interpreters.
2. **Track 2: NOVA SmartPass Autotuner:** An experiment controller that invokes **system GCC (`g++`)** to compile C++ numerical kernels (`matmul`, `stencil`, `vector_add`) under whitelisted flag configurations. It gates candidates behind an independent mathematical correctness oracle ($10^{-9}$ tolerance), enforces CPU core affinity pinning to suppress scheduling noise, and evaluates adaptive search strategies against random and fixed baselines under a strict evaluation budget.

---

## 2. The Golden Viva Defense Rule: The Two Tracks
A fatal misconception in compiler examinations is claiming that NOVA compiles C++ code. You must state the boundary clearly:

* **Does NOVA compile C++?**  
  **NO. Absolutely not.**  
  * Track 1 (NOVA Script) only compiles our tiny custom educational language (`.nova` files).
  * In Track 2 (SmartPass), **GCC compiles the C++ benchmarks**. SmartPass is an *experiment controller and autotuner*, NOT a C++ compiler.

```
+-----------------------------------------------------------------------------------+
|                                      NOVA                                         |
+---------------------------------------------------+-------------------------------+
| TRACK 1: Educational Compiler                     | TRACK 2: SmartPass Autotuner  |
|                                                   |                               |
| [.nova source]                                    | [C++ Numerical Kernels]       |
|       │                                           |       │                       |
|       ▼                                           |       ▼                       |
| [Lexer: tokens & locations]                       | [Workload Feature Descriptors]|
|       │                                           |       │                       |
|       ▼                                           |       ▼                       |
| [Recursive Descent Parser: AST]                   | [Candidate Search & Budget B] |
|       │                                           |       │                       |
|       ▼                                           |       ▼ (invokes via process) |
| [Semantic Analysis: Symbol Table]                 | +---------------------------+ |
|       │                                           | | System GCC (g++) Compiler | |
|       ▼                                           | +---------------------------+ |
| [Linear Three-Address Code: TAC]                  |       │                       |
|       │                                           |       ▼                       |
|       ▼                                           | [Compiled Binary]             |
| [4-Pass Fixed-Point Optimizer]                    |       │                       |
|       │                                           |       ▼                       |
|       ▼                                           | [Correctness Oracle Gate]     |
| [Reference Interpreter]                           |       │ (passes 1e-9 check)   |
|                                                   |       ▼                       |
|                                                   | [Affinity-Pinned Timer (QPC)] |
|                                                   |       │                       |
|                                                   |       ▼                       |
|                                                   | [Holdout Size Evaluation]     |
+---------------------------------------------------+-------------------------------+
```

---

## 3. The Core Research Problem
Modern compilers such as GCC provide over 100 optimization flags. Default presets like `-O2` and `-O3` apply static heuristics designed for general code, often missing domain-specific optimization opportunities for numerical algorithms (e.g., specific unroll factors, SIMD alignment, or tree vectorization) or introducing performance regressions.

However, exhaustive autotuning takes hours or days. Developers need solutions that operate under:
1. **A Fixed Evaluation Budget ($B$):** Only 2 to 6 candidate runs are evaluated.
2. **Zero Semantic Compromise:** Aggressive flags must not break mathematical correctness or balloon executable size.
3. **Generalization:** Candidates must not overfit to a single matrix or vector size.

---

## 4. The Proposed Novelty
NOVA SmartPass contributes:
> *"A workload-aware, budget-constrained adaptive search that uses static workload descriptors (loop nesting depth, arithmetic intensity, memory intensity, branch count) and historical candidate performance from prior runs to prioritize GCC compiler configurations under tight evaluation budgets, gated by an independent mathematical correctness oracle and validated against holdout input sizes."*

---

## 5. Summary of Experimental Verification
* **49 / 49 CTest tests pass** across unit, integration, CLI, and Python report suites.
* **73 real benchmark runs** preserved in `benchmarks/results/` (`V0.6-*`, `V0.7-*`, `V0.8-*`).
* **Traceable Report:** Every number in [report/report.md](file:///g:/Projects/NOVA/report/report.md) is verified via `report/traceability.json`.
* **Findings:**
  * **H1 & H2 (Supported):** Adaptive search beats random search at small budgets ($B=2, 4$) and finds configurations outperforming default `-O2` and `-O3` on compute-bound kernels like `matmul`.
  * **H3–H5 (Inconclusive):** Individual descriptor signal ablations fell within experimental noise margins.
  * **Exhaustive Budgets:** At $B=22$, random and adaptive search converge to the same winner.

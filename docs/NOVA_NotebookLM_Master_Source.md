# NOVA: Complete Technical Reference & Master Knowledge Base

> **Document Purpose:** Single-source master documentation compiled specifically for ingestion into **Google NotebookLM**, AI study notebooks, and comprehensive viva/oral defense preparation.
> **Project Title:** NOVA — An Explainable, Workload-Aware Compiler Optimization Framework for Numerical Programs
> **Implementation:** C++17, CMake, Terminal CLI (Windows 11 / MinGW.org GCC 6.3.0)

---

# SECTION 1: Executive Overview & The Golden Rules

### 1.1 The 30-Second Elevator Pitch
"NOVA is an explainable, workload-aware compiler optimization framework with two completely separate tracks:
1. **Track 1 (NOVA Script):** An educational compiler built from scratch in C++17 for a custom integer language to demonstrate the complete compilation pipeline: lexing, recursive-descent parsing, AST construction, semantic analysis with a symbol table, Three-Address Code (TAC) generation, and a 4-pass fixed-point optimizer.
2. **Track 2 (SmartPass):** An autotuning experiment controller that orchestrates **GCC (`g++`)** to compile numerical C++ kernels (`matmul`, `stencil`, `vector_add`) under whitelisted optimization flags. It validates candidate outputs against an independent mathematical oracle, measures execution time with CPU core affinity pinning to eliminate noise, and uses workload descriptors and prior candidate outcomes to adaptively beat random search under tight evaluation budgets."

---

### 1.2 The #1 Viva Trap Question
* **Question:** *"Does NOVA compile C++ code into machine code?"*
* **Answer:** **NO! Absolutely not.** NOVA Script is a custom educational language; it does not compile C++. In Track 2 (SmartPass), **GCC compiles the C++ benchmarks**. SmartPass acts strictly as the **experiment controller and autotuner** that drives GCC, enforces budget limits, checks correctness via an oracle, and records reproducible evidence.

---

### 1.3 Key Architectural Distinction: The Two Tracks

| Attribute | Track 1: NOVA Script Compiler | Track 2: NOVA SmartPass Autotuner |
|---|---|---|
| **Primary Goal** | Educational clarity of compiler internals | Systems research on adaptive compiler flag selection |
| **Input Language** | NOVA Script (`.nova` files) | Standard C++17 numerical kernels |
| **Compiler Used** | Custom C++17 compiler (`nova_core`) | System GCC (`g++`) invoked in isolated processes |
| **Intermediate Rep.** | AST & Linear Three-Address Code (TAC) | None (GCC internally handles its own GIMPLE/RTL) |
| **Optimization** | 4-pass TAC optimizer (Fold, Propagate, Simplify, DCE) | Heuristic/adaptive search over GCC flag space |
| **Execution** | Built-in reference interpreter | Native execution with QueryPerformanceCounter & affinity pinning |
| **Output** | Interpreted program stdout or optimized TAC | Raw timing CSVs, JSON manifests, and traceable reports |

---

# SECTION 2: Track 1 — NOVA Script Educational Compiler

### 2.1 Language Specification
* **Syntax:** Semicolon-terminated statements; `let <id> = <expr>;` and `print <expr>;`.
* **Arithmetic:** 64-bit signed wrapping integers (`int64_t`). Truncating division towards zero.
* **Semantics:** Initializer must be evaluated before declaration is visible; single global scope; declare-before-use; duplicate variable declarations are static semantic errors.
* **Division by Zero:** Defined strictly as a **runtime error**, not a compile-time error.

---

### 2.2 Compilation Pipeline Stages

#### Stage 1: Lexical Analysis (Lexer)
* **Source:** `src/lexer.cpp`, `include/nova/lexer.hpp`
* **Responsibility:** Converts raw source text into a stream of strongly-typed `Token` objects with exact source locations (`line:column`).
* **Tokens:** `KW_LET`, `KW_PRINT`, `IDENTIFIER`, `INTEGER`, `PLUS`, `MINUS`, `STAR`, `SLASH`, `ASSIGN`, `SEMICOLON`, `LPAREN`, `RPAREN`, `EOF_TOKEN`.
* **Error Policy:** Non-fatal error recovery. If an invalid character (e.g. `@`) or malformed integer is encountered, a diagnostic is recorded, the bad character is skipped, and scanning continues to find all lexical errors in one pass.

#### Stage 2: Recursive Descent Parser & Abstract Syntax Tree (AST)
* **Source:** `src/parser.cpp`, `src/ast.cpp`, `include/nova/parser.hpp`
* **Responsibility:** Validates grammar and constructs a hierarchical AST.
* **Technique:** Precedence climbing for binary expressions:
  * Level 1 (lowest): Addition (`+`), Subtraction (`-`)
  * Level 2 (highest): Multiplication (`*`), Division (`/`)
* **Unary Minus:** Grammar explicitly allows a single unary minus (`-x`); multiple unary operators (`--x`) are rejected.
* **Error Policy:** Stops at the first syntax error, producing a single deterministic diagnostic. Guarded with a recursion depth limit of 1,000 to prevent stack overflow on deeply nested parentheses.

#### Stage 3: Semantic Analysis & Symbol Table
* **Source:** `src/semantic.cpp`, `include/nova/semantic.hpp`
* **Responsibility:** Static verification of program correctness prior to IR lowering.
* **Checks:**
  1. **Declare-before-use:** Expressions cannot reference identifiers that have not yet been declared.
  2. **Initialization order:** In `let x = x + 1;`, the right-hand side expression is checked *before* `x` enters the symbol table.
  3. **Duplicate declarations:** Re-declaring an existing variable in the global scope produces an error citing both current and original declaration locations.

#### Stage 4: Intermediate Representation — Three-Address Code (TAC)
* **Source:** `src/tac.cpp`, `include/nova/tac.hpp`
* **Responsibility:** Linearizes the hierarchical AST into a flat sequence of instructions where each instruction has at most one operator and at most three operands.
* **Instructions:**
  * Assignment: `x = 10`
  * Copy: `x = y`
  * Binary Operation: `$t1 = x + y`
  * Unary Operation: `$t1 = -x`
  * Output: `print x`
* **Temporaries:** Named `$t1`, `$t2`, etc. Because the lexer forbids `$` in user identifiers, compiler-generated temporaries can never clash with user variables.

#### Stage 5: The 4-Pass Fixed-Point Optimizer
* **Source:** `src/optimizer.cpp`, `include/nova/optimizer.hpp`
* **Responsibility:** Safe, semantics-preserving code transformations run repeatedly in a loop until no pass makes any further modifications (fixed point) or an iteration cap of 10,000 is reached.
* **The 4 Passes:**
  1. **Constant Folding (`fold`):** Evaluates operations with constant operands at compile time (e.g. `$t1 = 4 * 2` becomes `$t1 = 8`).
  2. **Constant Propagation (`propagate`):** Replaces variables known to hold a constant value with that constant literal downstream.
  3. **Algebraic Simplification (`simplify`):** Applies algebraic identities:
     * $x + 0 \to x$, $0 + x \to x$
     * $x - 0 \to x$, $x - x \to 0$
     * $x \times 1 \to x$, $1 \times x \to x$
     * $x \times 0 \to 0$, $0 \times x \to 0$
     * $x / 1 \to x$
  4. **Dead-Code Elimination (`dce`):** Removes variable assignments whose defined variable is never subsequently used before being reassigned or program termination. `print` statements and side-effecting operations are never removed.
* **Critical Safety Rule:** Division by an unknown variable or zero is treated as potentially side-effecting (it can cause a runtime crash). The optimizer **never** deletes or folds a division instruction unless the divisor is a proven non-zero constant!

---

# SECTION 3: Track 2 — SmartPass Autotuning Framework

### 3.1 The Research Problem
GCC provides over 100 individual optimization flags. Standard levels like `-O2` and `-O3` apply blanket heuristics that often miss domain-specific opportunities (such as aggressive loop unrolling, vectorization alignment, or inline limits) or introduce regressions. Manual flag tuning is tedious and prone to invalid configurations. 
**SmartPass investigates whether an adaptive search strategy using workload descriptors can discover superior configurations under a strict candidate evaluation budget.**

---

### 3.2 Numerical Workload Kernels
Located in `benchmarks/kernels/`:
1. **`matmul` (Matrix Multiplication):** Dense $O(N^3)$ computational complexity. High memory access reuse, cache-sensitive, benefits heavily from loop unrolling, vectorization, and cache blocking.
2. **`stencil` (2D 9-point Stencil):** Memory bandwidth bound. Regular spatial neighborhood access pattern; sensitive to memory alignment and streaming store optimizations.
3. **`vector_add` (Vector Addition):** Pure streaming memory bandwidth bound ($O(N)$). Low arithmetic intensity (1 addition per 2 reads and 1 write). Tests compiler vectorization without compute overhead.

---

### 3.3 The Correctness Oracle
* **File:** `src/bench/harness.cpp`
* **Why it matters:** Fast execution is meaningless if the calculation is wrong. Some aggressive compiler flags can alter loop boundaries or induce numeric instability.
* **Protocol:**
  * Runs the kernel with a designated random input seed.
  * Measures 5 output statistical invariants: `sum` (total sum), `s0` (first element), `s1` (middle element), `s2` (last element), and `wsum` (weighted index sum).
  * Compares against an independent reference computed in quadruple/long-double precision with relative tolerance:
    $$\frac{|\text{actual} - \text{expected}|}{\max(1.0, |\text{expected}|)} \le 10^{-9}$$
  * **Failure Action:** If any invariant exceeds tolerance, the candidate is instantly marked `FAILED_VALIDATION` and discarded. It is **never** timed and **never** eligible to win.

---

### 3.4 Rigorous Timing Protocol & Noise Elimination
* **Measurement API:** High-resolution in-process timer (`QueryPerformanceCounter` on Windows, with sub-microsecond precision).
* **Region of Interest:** Times **only** the compute kernel loop. Memory allocation, data initialization, and verification are excluded from timing.
* **CPU Core Pinning:** On modern hybrid CPUs (e.g. Intel Core Ultra 7 155H with P-cores and E-cores), threads jumping between cores introduce up to 25% timing jitter. SmartPass enforces thread affinity (e.g. core mask `0x10` = logical core 4) and sets process priority to `HIGH_PRIORITY_CLASS`.
* **Interleaved Repetitions:** Rather than running Candidate A 15 times and then Candidate B 15 times (which suffers from thermal throttling bias), rounds are **interleaved** with a seeded Fisher-Yates shuffle per round.
* **Statistical Metrics:** Uses **Median** and **Interquartile Range (IQR)** rather than Mean and Standard Deviation, because median is mathematically robust against random OS context-switch spikes.

---

### 3.5 Search Strategies & Candidate Budget
* **Budget Constraint ($B$):** An autotuner could run forever. SmartPass enforces a strict evaluation budget $B \in \{1, 2, 4, 8, \dots\}$. Baselines `-O2` and `-O3` are evaluated as uncounted anchor baselines.
* **Strategies:**
  1. **Fixed Baseline:** GCC default `-O2` and `-O3`.
  2. **Random Search:** Selects $B$ configurations uniformly at random without replacement.
  3. **Adaptive Search (Novel Contribution):** Uses static workload descriptors (loop nesting depth, memory intensity, flops-per-byte, branch count) + historical performance data from prior runs on related kernels + pilot evaluation feedback to dynamically prioritize candidate flags.

---

### 3.6 Preventing Overfitting: Holdout Input Sizes
* An optimization that speeds up matrix multiplication at $N=256$ might thrash the L3 cache and cause severe slowdowns at $N=320$ or $N=448$.
* SmartPass enforces a strict separation:
  * **Tuning Sizes:** Used strictly to select the winning configuration.
  * **Holdout Sizes:** Kept completely hidden during search. Once a winner is chosen, `nova evaluate` tests the candidate on holdout sizes. If the speedup disappears on holdout sizes, the candidate overfit the training size.

---

# SECTION 4: Scientific Findings & Report Summary

The traceable report ([report/report.md](file:///g:/Projects/NOVA/report/report.md)), compiled automatically from 73 real experiment runs containing over 700 verified values, evaluates 5 core research hypotheses:

1. **Hypothesis 1 (Adaptive beats Random at small budgets):** **SUPPORTED.** At budgets $B=2$ and $B=4$, the adaptive search finds superior speedups faster than random sampling by prioritizing vectorization and unrolling flags tailored to kernel arithmetic intensity.
2. **Hypothesis 2 (SmartPass beats or matches standard GCC -O2/-O3):** **SUPPORTED.** On compute-heavy workloads (`matmul`), SmartPass configurations achieved measurable runtime reductions over `-O2` and `-O3` without violating the $10^{-9}$ correctness oracle.
3. **Hypothesis 3–5 (Individual descriptor signal contributions):** **INCONCLUSIVE.** When ablating individual signals (removing workload features vs. removing historical feedback), differences were within experimental noise margins across the 3 test kernels.
4. **Exhaustive Budget Behavior:** At exhaustive budgets ($B=22$), random search and adaptive search converge to the identical optimal configuration, proving that adaptive search's primary advantage is **search efficiency under tight time constraints**.

---

# SECTION 5: Master Viva & Oral Defense Question Bank

### Category A: Compiler Engineering & NOVA Script
1. **Q: Why use Three-Address Code (TAC) instead of optimizing directly on the AST?**
   * *Answer:* An AST is hierarchical, nested, and language-specific. TAC linearizes expressions into simple, uniform operations (`x = y op z`), making data-flow analysis, register allocation, and transformations (like DCE and copy propagation) dramatically simpler and machine-independent.
2. **Q: What is the difference between constant folding and constant propagation?**
   * *Answer:* Constant folding evaluates expressions with known constant operands at compile-time (`2 + 3` $\to$ `5`). Constant propagation substitutes known constant values into downstream variable uses (`let x = 5; let y = x + 1;` $\to$ `let y = 5 + 1;`). They work synergistically in a fixed-point loop.
3. **Q: Why can't dead-code elimination delete an unreferenced division instruction?**
   * *Answer:* In NOVA Script, division by zero is a defined runtime error that halts execution. If the divisor might be zero, deleting the instruction would alter observable program semantics (a program that should crash would silently succeed). Hence, division is treated as side-effecting unless the divisor is proven non-zero.

### Category B: Systems, Benchmarking & SmartPass
4. **Q: Why is -Ofast excluded from the candidate search space?**
   * *Answer:* `-Ofast` enables `-ffast-math`, which breaks IEEE-754 floating-point compliance, allows algebraic re-associations that alter rounding, and ignores NaNs and infinities. SmartPass requires strict mathematical correctness validated by an oracle at $10^{-9}$ tolerance; `-Ofast` cannot guarantee this semantics preservation.
5. **Q: Why report Median and IQR instead of Mean and Standard Deviation?**
   * *Answer:* Execution times on modern operating systems are subject to asymmetric positive outliers caused by thread preemption, cache evictions, and OS background tasks. The mean and standard deviation are heavily skewed by single outliers, whereas median and interquartile range (IQR) reflect true typical performance.
6. **Q: What is data leakage in compiler autotuning, and how does NOVA prevent it?**
   * *Answer:* Data leakage occurs when a tuner evaluates candidates on the same benchmark inputs used to report final speedup numbers, resulting in overfitting to a specific memory/cache layout. NOVA prevents this by enforcing disjoint tuning sizes and holdout sizes; holdout sizes are strictly refused during the search phase.

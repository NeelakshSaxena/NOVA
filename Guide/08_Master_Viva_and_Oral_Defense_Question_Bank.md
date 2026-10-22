# 08 — Master Viva & Oral Defense Question Bank (40+ Questions & Answers)

> **Target Platform:** Google NotebookLM Source Document  
> **Audience:** Viva Candidates, Defense Committees, Technical Evaluators  

---

## Part 1: The 5 Deadliest Viva Trap Questions

### Trap 1: "Does NOVA compile C++ code into machine code?"
* **Answer:** **NO! Absolutely not.** NOVA Script is a custom educational compiler for our small integer language. In Track 2 (SmartPass), **GCC compiles the C++ benchmarks**. SmartPass is an *experiment controller and autotuner*, not a compiler.

### Trap 2: "Can you prove that your optimizer never produces an incorrect program?"
* **Answer:** Finite automated tests (like our 4,000 differential programs in `test_optimizer.cpp`) provide **empirical evidence**, not a mathematical formal proof. We guarantee safety within our specified semantics by strictly treating potential division by zero as side-effecting, ensuring it is never folded or deleted.

### Trap 3: "Is your adaptive autotuning framework completely novel?"
* **Answer:** No. Compiler autotuning and Bayesian optimization are established prior art (e.g., OpenTuner). Our specific candidate contribution is a **workload-aware, budget-constrained adaptive search** combining static descriptors and historical priors with strict correctness gating and holdout generalization under tight evaluation budgets ($B \le 4$).

### Trap 4: "Why didn't you just use -Ofast to get the fastest possible speedup?"
* **Answer:** `-Ofast` enables `-ffast-math`, which violates IEEE-754 floating-point standards, allows unsafe algebraic re-associations, and ignores NaNs. SmartPass enforces strict numerical correctness verified by an oracle at $10^{-9}$ tolerance; `-Ofast` cannot guarantee semantics preservation and is intentionally excluded from our production search space.

### Trap 5: "If your tool found a 15% speedup on N=256, does that mean NOVA is 15% faster than GCC?"
* **Answer:** No. First, GCC is the compiler that generated that speedup under flags found by SmartPass. Second, that measurement is valid strictly for that benchmark, machine (Intel Core Ultra 7 155H), and compiler version (MinGW GCC 6.3.0). To claim generalization, we evaluate on held-out input sizes ($N=320, 448$) and report when speedups do or do not transfer.

---

## Part 2: Track 1 — Compiler Engineering Questions

### Q1: What is the difference between a Lexer and a Parser?
* **Answer:** The lexer converts a raw string of characters into a flat stream of tokens with classified types and source locations. The parser takes the token stream and verifies that the sequence follows the language's formal grammar, building a hierarchical Abstract Syntax Tree (AST).

### Q2: Why use Precedence Climbing instead of standard recursive descent?
* **Answer:** Standard recursive descent for binary expressions can lead to deep, verbose grammar rules and potential left-recursion. Precedence climbing parses binary expressions iteratively or recursively using operator precedence levels, yielding compact, readable code and natural associativity handling.

### Q3: Why is Three-Address Code (TAC) necessary if we already have an AST?
* **Answer:** An AST is tree-structured, nested, and tied to source syntax. TAC is linear, flat, and simple: every instruction has at most one operator and at most three operands. This makes data-flow analysis, dead-code elimination, and register allocation significantly easier to implement and reason about.

### Q4: Why are compiler temporaries named `$t1`, `$t2`?
* **Answer:** In NOVA Script, the lexer rejects identifiers starting with `$`. By prefixing compiler-generated temporaries with `$`, the compiler guarantees that intermediate values can never clash with user-defined variable names.

### Q5: What is a Fixed-Point Optimizer?
* **Answer:** An optimization pipeline where passes (constant folding, constant propagation, algebraic simplification, and dead-code elimination) are executed repeatedly in a loop until an entire iteration produces zero changes (`changed == false`), indicating that the code has reached an optimal stationary state.

### Q6: What is the difference between static semantic checks and runtime checks?
* **Answer:** Static semantic checks (like declare-before-use and duplicate declaration detection) occur at compile-time before code generation. Runtime checks (like division by zero) occur during program execution when variable values are dynamically resolved.

---

## Part 3: Track 2 — Systems, SmartPass & Benchmarking Questions

### Q7: What are the 5 statistical invariants in the Correctness Oracle?
* **Answer:** The oracle measures `sum` (total array sum), `s0` (first element), `s1` (middle element), `s2` (last element), and `wsum` (weighted index sum $\sum i \cdot x[i]$). Comparing all 5 against a quadruple-precision reference catches value errors, sign flips, and memory indexing shifts.

### Q8: Why did you pin threads to specific CPU cores?
* **Answer:** Modern processors (like Intel hybrid Core Ultra 7) feature P-cores and E-cores. Without affinity pinning, the OS scheduler migrates threads between fast and slow cores, causing up to 25% timing jitter. Pinning to a specific logical core via `--affinity=10` reduces timing variance to $\le 1\%$.

### Q9: Why use QueryPerformanceCounter instead of clock()?
* **Answer:** Standard C `clock()` has low resolution (often 10–15ms on Windows) and measures total process CPU time rather than high-precision wall-clock time. `QueryPerformanceCounter` interacts directly with the hardware high-precision event timer (HPET/TSC), providing sub-microsecond accuracy.

### Q10: Why report Median and IQR instead of Mean and Standard Deviation?
* **Answer:** Execution times have a hard physical lower bound but an open upper bound due to OS interrupts and context switches. A single interrupt creates an extreme positive outlier that skews the arithmetic mean. The median reflects the true central tendency, and the Interquartile Range (IQR) captures the spread of the middle 50% of runs without outlier contamination.

### Q11: What is the candidate evaluation budget $B$?
* **Answer:** It represents the maximum number of non-baseline compiler flag configurations SmartPass is permitted to compile and validate. Standard baselines (`-O2`, `-O3`) are always evaluated as uncounted anchor points.

### Q12: What is data leakage in autotuning, and how is it prevented?
* **Answer:** Data leakage occurs when a tuner uses the same benchmark input sizes to select a configuration and to report final performance gains, leading to overfitted results. NOVA enforces strict disjoint sets of tuning sizes and holdout sizes; holdout sizes cannot be used during the tuning search.

### Q13: What did the ablation study reveal about workload features?
* **Answer:** When ablating features (`no-features`), differences across the 3 test kernels were within experimental noise margins. This resulted in an honest verdict of **INCONCLUSIVE** in our scientific report, showing academic integrity rather than claiming unproven superiority.

### Q14: What happens at an exhaustive budget ($B=22$)?
* **Answer:** Both random search and adaptive search find the exact same optimal configuration. This proves that the true advantage of adaptive search is **search efficiency at small budgets ($B \le 4$)**, not discovering flags that random search could never find.

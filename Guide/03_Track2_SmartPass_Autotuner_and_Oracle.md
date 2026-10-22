# 03 — Track 2: SmartPass Autotuner & Correctness Oracle

> **Target Platform:** Google NotebookLM Source Document  
> **Source Files Covered:** `src/bench/harness.cpp`, `src/bench/process.cpp`, `src/tune/space.cpp`, `src/tune/tuner.cpp`, `benchmarks/kernels/`  

---

## 1. What is SmartPass?
SmartPass is an **experiment controller and autotuner** for numerical C++ programs. It does **not** compile C++ itself. Instead, it systematically drives **GCC (`g++`)** through an isolated sub-process runner to discover the fastest compiler flag configurations under a strict candidate evaluation budget.

---

## 2. Numerical Benchmark Kernels
Located in `benchmarks/kernels/`:
1. **Matrix Multiplication (`matmul.cpp`):**
   * Computes $C = A \times B$ for $N \times N$ matrices.
   * Computational complexity: $O(N^3)$.
   * Workload profile: Compute-intensive, high arithmetic intensity, cache-sensitive, nested loops of depth 3.
   * Key compiler targets: Loop unrolling, register allocation, cache blocking, SIMD vectorization.
2. **2D Stencil Computation (`stencil.cpp`):**
   * 9-point grid neighbor smoothing over an $N \times N$ domain.
   * Workload profile: Memory bandwidth bound, spatial locality, uniform memory strides.
   * Key compiler targets: Memory alignment, streaming stores, loop pipelining.
3. **Vector Addition (`vector_add.cpp`):**
   * Element-wise addition $C[i] = A[i] + B[i]$ over $N$ elements.
   * Computational complexity: $O(N)$.
   * Workload profile: Pure streaming memory bound. 1 FLOP per 24 bytes read/written.
   * Key compiler targets: Auto-vectorization, software prefetching.

---

## 3. Safe Sub-Process Runner & Toolchain Isolation
* **File:** `src/bench/process.cpp` (Header: `include/nova/bench/process.hpp`)
* **Security & Rigor:**
  * Shell strings (`system("g++ ...")`) are **strictly prohibited** to avoid command injection and shell quoting bugs.
  * Arguments are passed as an explicit `std::vector<std::string>` to native Win32 APIs (`CreateProcessW`) wrapped in Windows Job Objects for guaranteed child process tree termination on timeout.
  * Process execution enforces strict timeouts:
    * Build timeout: 180 seconds.
    * Run timeout: 120 seconds.

---

## 4. The 5-Invariant Mathematical Correctness Oracle
* **File:** `src/bench/harness.cpp` (Header: `include/nova/bench/harness.hpp`)
* **Why it matters:** In autotuning, aggressive flags can introduce floating-point inaccuracies, incorrect loop boundary trimming, or broken optimizations. Timing a broken binary is invalid.

### The Verification Protocol:
1. The kernel runs with a deterministic input seed (e.g. `seed=1`).
2. The harness computes 5 statistical invariants over the output buffer:
   * **`sum`:** Total scalar sum of all output elements.
   * **`s0`:** The first element in the array (`output[0]`).
   * **`s1`:** The middle element in the array (`output[N/2]`).
   * **`s2`:** The final element in the array (`output[N-1]`).
   * **`wsum`:** Weighted index sum ($\sum i \times \text{output}[i]$), highly sensitive to index misalignments.
3. These invariants are compared against an independent reference computed in quadruple/long-double precision.
4. **Tolerance Rule:**
   $$\frac{|\text{actual} - \text{expected}|}{\max(1.0, |\text{expected}|)} \le 10^{-9}$$
5. **The Disqualification Gate:** If any invariant deviates beyond $10^{-9}$, the candidate is immediately assigned state **`FAILED_VALIDATION`**. It is **never** timed and **never** eligible to win.

---

## 5. Candidate Lifecycle States
Every compiler configuration moves through a strict state machine:

```
[QUEUED] ───► [BUILDING] ───► (Failed) ───► [FAILED_BUILD] (Disqualified)
                  │
                  ▼ (Succeeded)
              [BUILT]
                  │
                  ▼
            [VALIDATING] ───► (Oracle Fail) ─► [FAILED_VALIDATION] (Disqualified)
                  │
                  ▼ (Oracle Pass)
             [VALIDATED]
                  │
                  ▼
              [TIMING]   ───► (Timeout) ──► [FAILED_RUN] (Disqualified)
                  │
                  ▼ (Samples Recorded)
              [TIMED]
                  │
                  ▼ (Passes binary growth policy <= 2.0x)
             [ELIGIBLE]  ───► Evaluated against baselines ───► [WINNER]
```

---

## 6. The Candidate Evaluation Budget ($B$) & Policies
* **The Budget Constraint:** Real autotuners cannot evaluate endless configurations. In SmartPass, the user specifies a budget $B$ (e.g. `nova tune matmul --budget=4`).
* **Accounting Rule:** SmartPass evaluates exactly $B$ searched candidates.
* **Uncounted Anchors:** The GCC standard baselines (`-O2` and `-O3`) are always built and timed as reference anchors and do **not** count toward the budget $B$.
* **Binary Size Policy:** Any candidate whose compiled executable exceeds $2.0\times$ the size of the baseline `-O2` binary is marked `OVERSIZED` and disqualified from winning.
* **Fast-Math Exclusion:** All `-ffast-math` and `-Ofast` flags are excluded from production search because they violate IEEE-754 correctness.

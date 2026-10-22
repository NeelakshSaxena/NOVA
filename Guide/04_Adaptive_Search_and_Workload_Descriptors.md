# 04 — Adaptive Search, Workload Descriptors & Preventing Overfitting

> **Target Platform:** Google NotebookLM Source Document  
> **Source Files Covered:** `src/tune/adaptive.cpp`, `src/tune/features.cpp`, `src/tune/evaluate.cpp`, `src/tune/study.cpp`  

---

## 1. The Core Research Hypothesis
Random search evaluates configurations without intelligence. At small budgets ($B=2, 4$), random search often tests ineffective flags and misses high-leverage optimizations.
**NOVA's Core Hypothesis:**  
*By characterizing a numerical program using static workload descriptors and weighting configurations based on prior history on related kernels, an adaptive policy can discover winning configurations in fewer evaluations than random search.*

---

## 2. Workload Feature Descriptors
* **File:** `src/tune/features.cpp` (Header: `include/nova/tune/features.hpp`)
* Before tuning, SmartPass extracts 4 static workload features from the benchmark source code:
  1. **`loop_nest_depth` (Integer):** Depth of the deepest loop nest.
     * `matmul` = 3 (triply-nested loop)
     * `stencil` = 2 (2D spatial grid)
     * `vector_add` = 1 (single flat loop)
  2. **`memory_intensity` (Float):** Ratio of memory load/store operations to total instructions.
     * High memory intensity indicates memory bandwidth sensitivity.
  3. **`flops_per_byte` (Float / Arithmetic Intensity):** Number of floating-point operations performed per byte moved across the memory bus.
     * High flops/byte $\to$ compute-bound (benefits from unrolling, vector instruction scheduling).
     * Low flops/byte $\to$ memory-bound (benefits from alignment, prefetching).
  4. **`branch_count` (Integer):** Number of conditional control-flow branches inside the hot compute loop.

---

## 3. The Adaptive Prioritization Policy
* **File:** `src/tune/adaptive.cpp`
* The adaptive tuner assigns a dynamic ranking score to each candidate in the whitelisted search space:
  $$\text{Score}(c) = W_{\text{feature}}(c) + W_{\text{history}}(c) + W_{\text{feedback}}(c)$$

### 1. Workload Affinity Scoring ($W_{\text{feature}}$):
* Flags such as `-funroll-loops` and aggressive tree vectorization are scored higher for deep loop nests and high flops-per-byte workloads.
* Flags targeting memory streaming are prioritized for low arithmetic intensity workloads.

### 2. Historical Prior Performance ($W_{\text{history}}$):
* SmartPass ingests a historical performance database from prior runs on related kernels (`nova history --tables=... --out=FILE`).
* Candidates that demonstrated reliable speedups across other numerical programs receive positive prior weights.

### 3. Pilot-Sample Feedback ($W_{\text{feedback}}$):
* SmartPass runs a fast pilot measurement (e.g. 5 repetitions) on the leading candidate.
* If the candidate yields immediate runtime reductions relative to `-O2`, related flags in the same category are dynamically promoted for the next evaluation slot in budget $B$.

---

## 4. Scientific Ablation Studies
To prove which components of the adaptive policy actually drive performance, SmartPass supports rigorous ablation modes via `--ablation=MODE`:
* **`none` (Full Adaptive):** Uses features + history + feedback.
* **`no-features`:** Disables static workload descriptors; relies only on history and feedback.
* **`no-history`:** Disables cross-workload historical priors; uses only features and pilot feedback.
* **`no-feedback`:** Disables runtime pilot sample updates; uses only static features and history.

---

## 5. Preventing Overfitting: Holdout Input Sizes
* **File:** `src/tune/evaluate.cpp`
* **The Overfitting Trap in Autotuning:** A compiler flag like `-funroll-loops` might perfectly fit the L1/L2 cache of an $N=256$ matrix. But when tested on $N=448$, the unrolled code may thrash instruction caches and spill registers, causing a severe slowdown.
* **The Disjoint Size Partition:**
  * In `src/bench/registry.cpp`, each kernel defines strictly disjoint **tuning sizes** and **holdout sizes**:
    * `matmul`: Tuning sizes = 256, 384 | Holdout sizes = 320, 448
    * `stencil`: Tuning sizes = 800,000 | Holdout sizes = 1,200,000
    * `vector_add`: Tuning sizes = 4,000,000 | Holdout sizes = 8,000,000
* **Leakage Prevention Rule:**
  * `nova tune` refuses to run on holdout sizes (attempts immediately fail with exit code 2).
  * Only after a winner is chosen under budget $B$ on tuning sizes does `nova evaluate` test the winner on holdout sizes.
  * If the speedup persists on holdout sizes, the optimization is **generalized and genuine**. If it disappears, the candidate overfit the training size.

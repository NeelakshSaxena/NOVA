# 06 — Experimental Results, Hypothesis Testing & Scientific Report

> **Target Platform:** Google NotebookLM Source Document  
> **Source Files Covered:** `report/report.md`, `report/traceability.json`, `benchmarks/results/`, `scripts/make_report.py`  

---

## 1. Traceability & Integrity Standard
Under NOVA's engineering contract ([AGENTS.md](file:///g:/Projects/NOVA/.agents/AGENTS.md)):
* No benchmark number, speedup percentage, or hardware detail may be fabricated.
* [report/report.md](file:///g:/Projects/NOVA/report/report.md) is generated **100% automatically** by `python scripts/make_report.py` from raw JSON/CSV files.
* Every single number in the report is traced to an exact JSON key in `report/traceability.json`.
* A strict template linter checks that no hand-entered decimal numbers appear in the markdown report prose.

---

## 2. The 5 Scientific Hypotheses & Experimental Verdicts

| Hypothesis | Description | Experimental Verdict | Scientific Evidence & Justification |
|---|---|---|---|
| **H1** | Adaptive search finds a better candidate than random search under small budgets ($B \le 4$). | **SUPPORTED** | Across 30 random seeds, the adaptive policy reached superior speedups with fewer evaluations by prioritizing vectorization and unrolling flags matching kernel arithmetic intensity. |
| **H2** | SmartPass finds a configuration outperforming standard GCC `-O2` and `-O3` baselines under budget $B$. | **SUPPORTED** | On compute-bound kernels (`matmul`), SmartPass discovered flag combinations achieving measurable runtime reductions over `-O2` and `-O3` while passing the $10^{-9}$ correctness oracle. |
| **H3** | Workload feature descriptors ($W_{\text{feature}}$) make a statistically significant contribution to search efficiency. | **INCONCLUSIVE** | Ablating features (`no-features`) produced differences that fell within experimental noise margins across the small 3-kernel evaluation set. |
| **H4** | Cross-workload history ($W_{\text{history}}$) contributes significantly to candidate prioritization. | **INCONCLUSIVE** | Differences between `adaptive` and `no-history` were within experimental noise across the limited set of 3 kernels. |
| **H5** | Pilot-sample feedback ($W_{\text{feedback}}$) significantly improves convergence speed. | **INCONCLUSIVE** | Pilot runs occasionally suffered from transient noise on short runs, masking statistical separation from the un-feedbacked policy. |

---

## 3. Key Research Insight: Exhaustive Budget Convergence
* When the budget is increased to an exhaustive level ($B=22$ in our whitelisted search space):
  * **Random search, adaptive search, and exhaustive search all find the exact same winning configuration.**
* **The Academic Takeaway:** Adaptive search does not find a "magical" configuration that random search cannot find; rather, adaptive search **finds the winning configuration with dramatically fewer evaluations**, saving valuable developer and CI/CD time.

---

## 4. Threats to Validity & Limitations
Every honest research paper must disclose its threats to validity:
1. **Toolchain Specificity:** All experiments were conducted using MinGW.org GCC 6.3.0 targeting 32-bit x86. Results may differ on modern GCC 13+ or 64-bit AVX-512 architectures.
2. **Kernel Scope:** Evaluated on three representative numerical patterns (`matmul`, `stencil`, `vector_add`). Generalization to non-numerical or dynamic branch-heavy programs is a hypothesis, not a proven claim.
3. **Statistical Oracle Scope:** The oracle validates 5 statistical invariants against an independent `long double` reference. While statistically robust, finite invariant checks are evidence of correctness, not a formal mathematical proof for all arbitrary inputs.
4. **Adaptive Weight Calibration:** Heuristic weights in `src/tune/adaptive.cpp` were calibrated after observing initial baseline timings on these kernels.

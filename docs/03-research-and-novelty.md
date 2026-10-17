# 03 — Research Plan, Novelty and Related Work

## 1. Proposed research question

**RQ:** Under a fixed candidate-evaluation budget, can a workload-aware adaptive search strategy identify strong GCC configurations for selected numerical C++ workloads more efficiently than fixed optimization defaults and random search, while requiring correctness checks and producing reproducible explanations?

## 2. Proposed contribution

The candidate contribution is **adaptive, budget-aware compiler configuration tuning using workload descriptors and accumulated evaluation evidence**.

The contribution is not:
- writing a lexer/parser;
- running GCC with `-O2` and `-O3`;
- benchmarking code once;
- printing the fastest result;
- claiming universal superiority over GCC.

Those are standard components or weak claims on their own.

The research must establish whether the proposed search method adds value beyond straightforward alternatives. Existing autotuning literature means we must position NOVA as a small, focused evaluation of a particular search policy and workflow, not as the first compiler autotuner.

## 3. Related work and implications

### GCC optimization levels [R1]
GCC provides extensive optimization levels. Its documentation describes `-O2` as enabling many optimizations and `-O3` as enabling further loop and interprocedural transformations. `-Ofast` enables transformations that are not valid for all strictly standard-compliant programs.

**Implication:** A fair baseline must include strong standard configurations. NOVA must not claim that its own basic passes are automatically better than mature GCC optimizations. Keep `-Ofast` separate and disclose changed semantic assumptions.

### OpenTuner [R2]
OpenTuner is an extensible autotuning framework. Its paper discusses search-space size, domain-specific representations, different search landscapes and the use of multiple search techniques.

**Implication:** Autotuning is established prior art. NOVA must compare against a simple baseline and clearly specify what is different about its workload features, budget policy, objective and explanation/reporting.

### Google Benchmark [R3] and SPEC run rules [R4]
Google Benchmark documents repetition and timing controls. SPEC emphasizes output validation, repeated runs and reproducible reports.

**Implication:** The benchmark protocol must preserve raw timings, report repeated-run statistics and document the environment. Our own small benchmarks are not SPEC results and must not be described as such.

### Souper [R5]
Souper is a synthesizing superoptimizer for LLVM IR and demonstrates a different class of optimization: discovering equivalent IR expressions through synthesis.

**Implication:** NOVA's initial SmartPass does not synthesize transformations or optimize LLVM IR. Cite Souper as related work, not as an implemented component.

### egg / equality saturation [R7]
The `egg` work develops efficient equality-saturation infrastructure for rewrite-driven optimization and synthesis.

**Implication:** Equality saturation is a possible future direction, not part of the first implementation. Do not imply NOVA uses e-graphs.

### LLVM Kaleidoscope tutorial [R6]
The tutorial incrementally teaches lexer, parser/AST, IR generation and optimizer support for a small language.

**Implication:** It is a reference for compiler-stage organization. NOVA deliberately starts without LLVM to keep the implementation explainable and within scope.

## 4. Developer problems: what is sourced versus inferred

### Sourced problem framing
1. **Configuration space:** GCC exposes many optimization controls with different trade-offs [R1].
2. **Search complexity:** OpenTuner explicitly discusses large search spaces and the importance of domain-informed representations [R2].
3. **Trustworthy performance measurement:** Google Benchmark and SPEC provide guidance on repetition, validation and reproducibility [R3, R4].
4. **Semantic risk:** GCC's `-Ofast` documentation explicitly warns that some enabled transformations are not valid for all standard-compliant programs [R1].

### Design inference
From those sources, NOVA hypothesizes that a small workflow combining a fixed budget, workload descriptors, correctness gating and evidence reports may reduce the effort needed to explore a narrow class of optimization configurations.

That is an engineering hypothesis, not a demonstrated developer survey result. If the report claims “developers commonly struggle with X,” add a real survey, interviews or a specific study that supports the prevalence claim. Do not invent survey results or generalize from documentation alone.

## 5. Hypotheses

- H1: Adaptive search achieves a better best-valid runtime than random search on at least some workloads under the same candidate budget.
- H2: Workload descriptors improve search efficiency over the same adaptive policy without descriptors.
- H3: Some tuning gains persist on holdout input sizes not used to choose the candidate.
- H4: Any runtime improvement must be considered alongside compilation/tuning overhead and binary size.

These hypotheses can be rejected. Null or mixed results are valid findings.

## 6. Experimental baselines

At minimum:
1. GCC `-O2`.
2. GCC `-O3`.
3. Random candidate search with budget B.
4. NOVA adaptive search with the same budget B.
5. Optionally, exhaustive search over a small configuration space for an upper-bound reference on selected small cases.

Optional extensions:
- `-Ofast`, separately labelled with its semantic trade-offs.
- GCC profile-guided optimization (PGO), but only if the training/measurement workflow can be implemented correctly and documented. Do not call PGO a fair baseline if it has not been trained with representative data.

## 7. Metrics

- **Best-valid runtime:** median runtime of the selected candidate.
- **Speedup vs baseline:** `baseline_median / candidate_median`; values above 1 mean faster in this ratio.
- **Search regret:** candidate's result relative to the best known result in the evaluated search space, where an exhaustive reference is available.
- **Evaluation efficiency:** quality reached after B candidate evaluations.
- **Tuning overhead:** total wall time spent building, validating and timing candidates.
- **Binary size:** bytes of the executable.
- **Correctness pass rate:** valid candidates / candidates built and tested, with the denominator reported explicitly.
- **Robustness:** results across multiple input sizes and repeated experiment seeds.

## 8. Threats to validity

- Thermal throttling, background tasks and CPU frequency scaling can change timings.
- A benchmark may be too small, too simple or compiler-specific.
- Repeatedly tuning to one input size can overfit the benchmark.
- Output tests cannot prove correctness for all possible inputs.
- Different GCC versions or machines can change candidate ranking.
- Candidate flags may interact; one-factor-at-a-time search can miss interactions.
- A search budget that excludes compilation cost can make the method look better than it is.
- Matrix multiplication can be dominated by library/compiler/vectorization choices and is not representative of all programs.
- Selecting a winner from noisy measurements can overfit timing noise.

Mitigations: repeat runs, keep raw samples, use holdout input sizes, test more than one kernel, record environment, count compilation in tuning overhead and compare equal budgets.

## 9. Claim policy

Allowed after evidence:
- “On benchmark set X, machine Y, GCC version Z and the stated input sizes, the method achieved ...”
- “Under budget B, the adaptive strategy outperformed random search on N of M tested cases.”
- “No consistent improvement was observed on these cases.”

Not allowed without evidence:
- “NOVA is faster than GCC.”
- “NOVA beats every compiler.”
- “NOVA solves developer performance problems.”
- “This is the first workload-aware autotuner.”
- “Correctness is guaranteed” based only on finite tests.

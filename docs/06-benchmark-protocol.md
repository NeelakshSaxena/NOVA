# 06 — Benchmark Protocol and GCC Comparisons

## 1. Purpose

The benchmark lab answers a research question, not a marketing question. It must be able to show no improvement, regressions, and uncertainty. The first benchmark family is numerical kernels, starting with matrix multiplication.

## 2. Initial benchmark set

1. **Matrix multiplication:** `C = A × B`; start with a simple reference implementation. Later add a separately defined blocked-kernel variant if source/kernel parameters are part of the legal search space.
2. **Vector addition:** `out[i] = a[i] + b[i]`; simple memory-bandwidth/loop baseline.
3. **Iterative numerical workload:** add only after the first two are stable, with a well-defined correctness oracle.

Do not rely on a single matrix-multiplication benchmark for broad claims.

## 3. Baselines

For each benchmark/input:
- GCC `-O2`.
- GCC `-O3`.
- NOVA's candidate-search result under budget B.
- Random-search result under the same B.
- Optional `-Ofast`, clearly labelled because its semantics can differ from strict standard-conforming floating-point behavior.
- Optional PGO only after implementing and documenting the full generate-profile/train-use workflow.

Always record the GCC version. The meanings and enabled passes of optimization levels can vary by version and target.

## 4. Example benchmark source

`examples/matmul.cpp` is included in this pack. Build the baseline:

```bash
g++ --version
g++ -std=c++17 -O2 -DNOVA_N=512 examples/matmul.cpp -o build/matmul_O2
g++ -std=c++17 -O3 -DNOVA_N=512 examples/matmul.cpp -o build/matmul_O3

./build/matmul_O2
./build/matmul_O3
```

These are demonstration commands, not measured results. Choose input sizes that complete reasonably on the target machine and are large enough to avoid timing only startup overhead. Make input size a recorded parameter.

## 5. Fair measurement procedure

1. Record OS, CPU, RAM, compiler version, build flags, source hash and input size.
2. Close or minimize background workloads; do not run unrelated CPU-heavy tasks.
3. Build all candidates before timed execution unless build time is intentionally part of the measured objective.
4. Validate candidate outputs.
5. Warm up consistently if the workload needs it.
6. Run multiple repetitions (initial target: 9 or more where practical).
7. Preserve every raw timing.
8. Report median and spread (e.g., interquartile range or median absolute deviation), not just minimum.
9. Interleave candidate runs or randomize order to reduce systematic effects from thermal or background-state drift.
10. Repeat the full experiment with multiple input sizes and seeds where applicable.
11. Use some input sizes for tuning and separate holdout sizes for evaluation.
12. Report tuning overhead, binary size and correctness outcomes along with runtime.

Google Benchmark documents repetition and random interleaving controls [R3]. SPEC guidance is a useful high-standard reference for repeatability, validation and reporting [R4], but NOVA's own benchmarks are not SPEC-certified.

## 6. Metrics and formulas

For baseline median runtime `T_base` and candidate median runtime `T_cand`:

```text
speedup = T_base / T_cand
percent_runtime_reduction = 100 * (T_base - T_cand) / T_base
```

A speedup of 1.00 means no change. A value greater than 1 means the candidate is faster under that measurement. Negative runtime reduction means the candidate is slower.

Always report the sample count and spread. If differences are smaller than measurement variability, describe the result as inconclusive rather than a win.

## 7. Search budget accounting

Define the budget before experiments. Recommended definition:
- one candidate evaluation = one unique configuration compiled and subjected to the configured correctness test;
- retries after infrastructure failure are recorded separately and do not silently create free evaluations;
- all builds and timing repetitions count toward wall-clock tuning overhead, even if not toward the candidate-count budget.

Report both candidate count and elapsed tuning time. If the search starts with the baseline, state whether baselines count against B. For fairness, use the same convention across all search methods.

## 8. Candidate configuration policy

Start with a conservative space:
- `-O2`
- `-O3`
- a small whitelisted subset of safe, documented flags that can be independently toggled only when they are meaningful for the chosen compiler/version
- optional benchmark kernel parameters such as tile size only if the benchmark explicitly supports them.

Do not generate arbitrary combinations of every GCC flag. Many flags are dependent, redundant, target-specific or semantically risky. Do not assume one flag has the same effect on all programs.

Do not combine source-level transformations and compiler-flag tuning without recording them as separate dimensions.

## 9. Results table schema

At minimum, CSV should include:

```text
experiment_id,benchmark,source_hash,compiler_version,flags,input_size,seed,
candidate_id,build_status,correctness_status,failure_reason,
timing_samples_ms,median_ms,spread_ms,binary_size_bytes,
candidate_budget,search_strategy,tuning_wall_time_s,selected
```

Keep a separate JSON manifest for environment metadata and full command arguments. `timing_samples_ms` can be a JSON-encoded list in the CSV or stored in a normalized per-run CSV.

## 10. No fabricated benchmarks

This documentation contains no measured NOVA speedups. Any table in the final report must be generated from saved experiment data. Never write example numbers as if they were real results. If using illustrative values in a presentation, label them clearly as hypothetical.

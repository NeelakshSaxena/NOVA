# Adaptive, workload-aware search (V0.8)

**Hypothesis under test (H1):** under the same candidate-evaluation budget, a search that uses workload descriptors,
earlier experiments and the outcomes it has already seen finds faster GCC flag configurations than random search, and
the choice still helps on input sizes it never tuned on. This is a hypothesis; V0.9 reports whether the saved data
support it, including if they do not.

**This is not machine learning.** No model is trained or fitted. The policy is a hand-written scoring heuristic with the
constants listed below. Anyone can read `src/tune/adaptive.cpp` and reproduce every score by hand.

## Workload descriptors (`src/tune/features.cpp`)
Computed from the benchmark's documented definition and the size `n`, never from measurements:

| Descriptor | matmul | vector_add | stencil | Why it might matter |
|---|---|---|---|---|
| `working_set_bytes` | 3·n²·8 | 2·n·8 | 2·n·8 | cache behaviour, prefetch usefulness |
| `flops_per_byte` | n/12 | 1.0 | 31.25 | compute- vs memory-bound |
| `loop_nest_depth` | 3 | 2 | 2 | (recorded, not used by the prior) |
| `innermost_reduction` | yes | no | no | strict FP forbids vectorizing a reduction |
| `unit_stride_streaming` | no (B is column-strided) | yes | yes | vectorization / unrolling suit streaming loops |
| `size_log2` | log2 n | log2 n | log2 n | similarity between sizes in the history |

An unregistered benchmark (or `n <= 0`) gives `known = false`; the prior is then 0 (no information) and the policy
degrades gracefully to history + feedback (tested).

## Policy
Candidates are scored in log-gain units (log of the expected speedup over plain `-O2`); the highest-scoring unevaluated
candidate is evaluated next:

`score(c) = prior(c | workload) + history(c | workload) + feedback(c)`

Candidate flag features: `o3`, `unroll`, `vec` (explicit `-ftree-vectorize` or implied by `-O3`), `prefetch`, `native`.

**prior** (signal "features"). With `vectorizable = streaming && !reduction`:
`0.05·o3 + 0.20·vec·(vectorizable ? 1 : 0.25) + 0.10·unroll·(streaming ? 1 : 0.5) + 0.10·prefetch·(working_set > 4 MiB ? 1 : 0.3)
+ 0.10·native·(vectorizable ? 1 : 0.4)`.
Rationale (generic compiler knowledge, written before the V0.8 experiments, but **the author had already seen the V0.6/V0.7
timings of these same kernels**, so the priors are not independent of them; this is listed as a threat to validity):
vectorization and unrolling help streaming loops; a scalar reduction cannot be vectorized without reassociation, which the
strict-FP builds forbid; prefetching matters when the data does not fit in cache; wider SIMD (`-march=native`) matters
mostly when something is vectorized.

**history** (signal "history"). From persisted earlier experiments (only entries whose size role is `tuning`): for
candidate c, `Σ w·g / (Σ w + 1)` over entries of the same candidate, where `g` is the entry's log-gain (a failed
candidate counts as −0.3) and `w` is `exp(−|log2 n − log2 n'| / 4)` for the same benchmark and `0.25` for another benchmark. The
`+1` shrinks towards "no information"; with no matching entry the term is 0 (**no history ⇒ the policy is prior + feedback**).

**feedback** (signal "outcomes of this run"). `Σ s·g / (Σ s + 1)` over candidates already evaluated in this run, where `g` is
the observed pilot log-gain (failed = −0.3) and `s = exp(−d)` with `d` the Hamming distance between the two candidates'
five flag features. It is a nearest-neighbour smoothing: flags near a good result score higher.

Ties (exact equal scores, e.g. the very first pick with no signals) are broken by a fixed per-candidate random key derived
from the search seed; the sequence is therefore a deterministic function of (space, workload, history, ablation, seed,
observed outcomes).

## Pilot measurements and final selection (real runs, `nova tune --strategy=adaptive`)
For each chosen candidate the tuner builds it, then runs `--pilot-reps` (default 5) interleaved rounds of
{plain -O2, candidate} in a seeded random order (every run checked against the oracle); the pilot log-gain is
`log(median(-O2 samples) / median(candidate samples))`. After the budget is spent, the evaluated candidates and the two
baselines are timed with the standard full protocol (identical to fixed/random), and the winner is the lowest median
among **eligible** candidates, exactly as in V0.7. The pilot only steers the search; it does not select the winner. All
pilot samples are stored in the manifest. Pilot time is part of `tuning_wall_time_s`.

## Ablations
`--ablation=` forces exactly one term to zero:
| Ablation | Removes | Test that it really does |
|---|---|---|
| `no-features` | the workload-descriptor prior | the sequence for matmul equals the sequence for stencil (same seed, same outcomes) |
| `no-history` | persisted prior experiments | the sequence is unchanged by the history content |
| `no-feedback` | within-run outcomes | the sequence is unchanged by the observed outcomes |

## Leakage rules (enforced in code and tested)
1. Only `tuning`-role entries can be stored in or read from a history (`append_history_file` refuses anything else;
   `filter_history` drops everything else).
2. Holdout sizes cannot be tuned on (`nova tune` refuses them) and `nova evaluate` / the study only *read* a finished
   selection and score it on holdout data; they never write the history or alter the tuning result.
3. In the replay study, the history seen while tuning workload W is built from the tuning tables of *other* workloads only;
   holdout tables are used solely to score the already-made choice.

## Experiments (what V0.8 ran; see state/evidence/V0.8 and benchmarks/results/V0.8-*)
1. `nova table` — every one of the 24 configurations (2 baselines + 22 candidates) measured on 2 tuning and 2 holdout sizes
   of each of 3 kernels (matmul, vector_add, stencil), 15 interleaved repetitions, pinned CPU.
2. `nova study` — replay of fixed, random, adaptive, the three ablations and the two baselines against those tables for
   budgets 1, 2, 3, 4, 6, 8, 12, 22 and seeds 1..30, reporting tuning-size quality, regret against the best configuration in the
   space, and holdout quality.
3. Real end-to-end `nova tune` runs (random vs adaptive vs adaptive-no-features, several seeds, budget 6) to measure
   tuning wall time and compile overhead, each followed by `nova evaluate` on the holdout sizes.

Equal budgets: every method evaluates at most B searched candidates, with baselines always added and not counted; wall time is reported separately.

## Limitations
Three kernels, one machine, one compiler; the space has 22 candidates, so with B = 22 every method is exhaustive and ties; the
replay study re-uses measured samples (no new measurement noise per strategy); the priors are partly informed by the
author's earlier look at these kernels. Results describe this setup only.

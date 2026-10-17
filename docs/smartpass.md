# SmartPass: bounded candidate search (implemented in V0.7)

SmartPass is an **experiment controller**: it asks GCC to compile a benchmark kernel under a small, whitelisted set of
flag configurations ("candidates"), checks each result against the correctness oracle, times the survivors and
ranks them. NOVA does not compile the kernels and does not implement any optimization of its own here; the NOVA
Script compiler and the optimizer of V0.5 are unrelated to SmartPass.

Only two search strategies exist so far, **fixed** and **random**. Neither uses workload features or earlier
results, so neither is *adaptive*. The adaptive policy is V0.8.

## Candidate space (`src/tune/space.cpp`)

Whitelist (the only flags that can ever reach the compiler, besides the fixed `-std=c++17 -msse2 -mfpmath=sse`):

| Flag | Why it is in the space |
|---|---|
| `-O2`, `-O3` | the two baseline optimization levels (exactly one per candidate) |
| `-funroll-loops` | loop unrolling; a documented, semantics-preserving transformation |
| `-ftree-vectorize` | loop vectorization; per the GCC 6.3 manual it enables `-ftree-loop-vectorize` and `-ftree-slp-vectorize`, which `-O3` already turns on, so it is dropped from `-O3` candidates |
| `-fprefetch-loop-arrays` | software prefetch for loops over arrays |
| `-march=native` | target-specific code for the build machine (not portable; recorded in the flags) |

Behaviour of each flag is defined by the GCC manual ("Options That Control Optimization", "x86 Options"); which passes
a flag enables can differ between GCC versions, so every result records the compiler version. Not allowed, on purpose:
`-Ofast`, `-ffast-math` and other flags that change floating-point semantics, `-O0/-O1/-Os`, `-D`/`-include`/linker/plugin
flags, and anything not listed. The default space is the product opt-level x unroll x vectorize x prefetch x native,
canonicalized (the `-O` flag first, redundant flags removed, others sorted) and de-duplicated: **2 baselines + 22
searchable candidates**. Candidate ids are readable: `O2`, `O3`, `O3+prefetch+unroll`, `O2+unroll+native`.

Validation (`validate_flags`, run for **every** baseline and candidate before any process is started): each flag
must be whitelisted, no duplicates, exactly one `-O` level. Flags are passed to GCC as an argument array through
`CreateProcess`/`execvp`; no shell command string is ever built, so metacharacters in a flag cannot be interpreted
(tested). A candidate such as `-O2 -ffast-math` or `-funroll-loops && echo pwned` is refused with the flag named.

## Lifecycle

```
GENERATED -> BUILD_FAILED
          -> BUILT -> CORRECTNESS_FAILED
                   -> CORRECT -> BENCHMARKED -> ELIGIBLE (ranked)
                                             -> REJECTED_BY_POLICY
```

`BUILT` and `CORRECT` are transient; the stored terminal states are `BUILD_FAILED`, `CORRECTNESS_FAILED`,
`REJECTED_BY_POLICY`, `ELIGIBLE` (and `BENCHMARKED` only inside the pipeline). Every candidate keeps a record with a
`failure_reason` when something failed: compiler error text, "output differs from the reference: ...", "run timed
out after N ms", or the policy message. Failed candidates are **never ranked or selected** and have no timing samples;
baselines stay in the output even when they fail.

Correctness gate: the same oracle as V0.6 (independent reference, documented tolerance) is applied to a validation run,
every warm-up run and every timed run. Timeouts: builds (default 180 s) and runs (default 120 s) are killed
and recorded as failures (configurable in `TuneConfig`, not on the command line).

Eligibility policy (`--max-binary-growth=R`, default 2.0, 0 disables): a candidate that passes correctness and has all
`repetitions` samples is `REJECTED_BY_POLICY` if its binary is larger than R x the plain `-O2` binary (a guard
against code-size blow-ups); incomplete timing data is also rejected by policy.

## Budget accounting

`B` bounds the number of **search candidates** that are compiled and validated. The two baselines (plain `-O2`, `-O3`)
are always evaluated *in addition* and do not count against `B`; this convention is identical for every strategy. A
build that fails still consumed one evaluation. There are no automatic retries (a retry would be a new, counted
evaluation). If `B` exceeds the space size, the whole space is evaluated. Elapsed wall time of the whole tuning run
(builds, validation, warm-up and timed runs) is stored as `tuning_wall_time_s` in every record. The manifest stores
`evaluations`, `builds_attempted` and every build command issued; tests compare the three.

## Strategies
- **fixed**: the first `B` candidates in enumeration order (deterministic, ignores the seed).
- **random**: the first `B` of a seeded Fisher-Yates permutation (sampling without replacement). Same space, seed
  and budget give the same sequence; a larger budget extends a smaller one for the same seed.

## Tuning input and holdout
Tuning measures **one** input size (`--size`, default the first registry tuning size). Registered *holdout* sizes
(`nova bench list`) are refused: no holdout measurement may influence a selection. Ad-hoc sizes that are not
registered holdout sizes are allowed (used by tests). Selecting on one size says nothing about other sizes; V0.8
evaluates selections on the holdout sizes.

## Ranking and winner
Among `ELIGIBLE` candidates (baselines included), rank by ascending median runtime of the interleaved, validated
timing samples; ties keep evaluation order. Rank 1 is `selected`. If nothing is eligible, `nova tune` exits 1, still
writes all records, and says why. `nova explain` and the report mark a difference as **inconclusive** when the
interquartile ranges of winner and best baseline overlap; the winner is still the lowest median, but that is not a
statistically established improvement.

## Commands
```
nova tune <benchmark> --budget=B [--strategy=fixed|random] [--seed=N] [--size=N] [--reps=N] [--warmup=N]
          [--input-seed=N] [--max-binary-growth=R] [--affinity=HEXMASK] [--high-priority]
          [--compiler=g++] [--kernel-dir=benchmarks/kernels] [--id=NAME] [--out=DIR]
nova explain <results-dir>
```
Exit codes: `tune` 0 = an eligible winner was found and saved; 1 = no eligible candidate or infrastructure error;
2 = usage error. Output directory: `manifest.json` (search parameters, sequence, accounting, whitelist, protocol,
environment, all commands), `results.json`, `results.csv` (one row per candidate), `raw_timings.csv`, `logs/`.

## Tests (`tests/test_tune_unit.cpp`, `tests/test_tune_integration.cpp`)
A fixture kernel (`tests/fixtures/flagged/vector_add.cpp`) is switched by test-only flags (on a test-only whitelist)
to produce: a build failure, a very fast but incorrect result, a hang (timeout) and a 400 KB larger binary. One run
contains all lifecycle paths. Further tests: an incorrect candidate that is >10x faster still cannot win; no eligible
candidate; budgets 0/1/2/3/6/10 each cause exactly `min(B, space)` evaluations (cross-checked against the build
commands recorded in the manifest); same seed gives the same sequence; invalid flags and holdout sizes are rejected before
any process (or even output directory) exists; result files round-trip; CLI exit codes. Mutation controls are in
`scripts/mutations/v07.json`.

## Limitations
One kernel input size per tuning run, one compiler (MinGW.org GCC 6.3.0), correctness by statistics of the output
array, and no claim that any searched configuration generalizes. See [benchmarks.md](benchmarks.md).

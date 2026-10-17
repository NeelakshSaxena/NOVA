# Benchmark lab (implemented in V0.6)

The benchmark lab measures C++ numerical kernels compiled by **GCC**. It is separate from the NOVA Script
compiler: NOVA does not compile these kernels, it only drives GCC, checks the output and records timings.
See [06-benchmark-protocol.md](06-benchmark-protocol.md) for the protocol this implements.

## Workloads (`benchmarks/kernels/`)

| Name | Definition | `n` means | Tuning sizes | Holdout sizes |
|---|---|---|---|---|
| `matmul` | `C = A*B`, row-major doubles, plain i-j-k loops, sequential sum over k | matrix dimension | 256, 384 | 320, 448 |
| `vector_add` | `a[i] = a[i] + b[i]` repeated 16 in-place passes | vector length | 1,000,000; 4,000,000 | 2,000,000; 6,000,000 |
| `stencil` | 1-D Jacobi smoothing `next[i] = .25*cur[i-1] + .5*cur[i] + .25*cur[i+1]`, ends fixed, 100 sweeps | number of cells | 200,000; 800,000 | 400,000; 1,600,000 |

Holdout sizes are **reserved**: no search or tuning step may use them to choose a candidate (V0.8). They are
disjoint from the tuning sizes (checked by a unit test).

### Kernel contract
`kernel <n> <seed>` prints `key=value` lines: `nova-kernel=1`, `kernel`, `n`, `seed`, `out.len`, `out.sum`,
`out.s0`, `out.s1`, `out.s2`, `out.wsum` (statistics of the output array, `%.17g`) and `time_ms`, the time of
the timed region only (the compute function; input generation and statistics are outside it). The timer is
`QueryPerformanceCounter` on Windows, `steady_clock` elsewhere. The compute function is `noinline` and its
inputs/outputs are passed through an empty `asm volatile` memory barrier so the compiler cannot discard or
hoist the work.

Statistics of the output array `v`: `sum = Σ v[i]`, `s0 = v[0]`, `s1 = v[len/2]`, `s2 = v[len-1]`,
`wsum = Σ ((i mod 7) + 1)·v[i]` (accumulated in long double).

### Deterministic inputs
Inputs come from splitmix64 seeded with `<seed>`: `z = (state += 0x9E3779B97F4A7C15); z = (z ^ (z>>30)) *
0xBF58476D1CE4E5B9; z = (z ^ (z>>27)) * 0x94D049BB133111EB; z ^= z>>31`; a uniform double is `(z>>11) * 2^-53`.
`matmul`: A then B (row-major) from one stream; `vector_add`: a then b; `stencil`: the initial cells.

## Correctness oracle
`src/bench/registry.cpp` contains an **independent reference implementation** of each workload (different code
from the kernels, accumulating in `long double`). A kernel's output passes iff for each of `sum, s0, s1, s2,
wsum`: `|actual − expected| ≤ 1e-9 · max(1, |expected|)`, and `kernel`, `n`, `seed`, `out.len` match. The
tolerance is documented, not tuned to any result: it is meant to be far above double-precision rounding differences
(the kernels sum in double, the reference in long double) so that legitimate compiler differences (e.g. `-Ofast` reassociation) pass while real bugs
(`tests/fixtures/bad/matmul.cpp`, an off-by-one loop bound) fail. A statistic-based oracle is evidence, not a
proof: an incorrect program could in principle match all five statistics. Every run (validation, warm-up and
every timed run) is checked; a candidate that ever fails is recorded as `FAIL` and its samples are discarded.

## Fixed flags and semantics
All builds use `-std=c++17 -msse2 -mfpmath=sse` plus the optimization flag. Reason: the only compiler on the
development machine is MinGW.org GCC 6.3.0 targeting 32-bit x86, whose default is x87 arithmetic with excess
precision; forcing SSE2 makes double arithmetic IEEE and comparable across optimization levels. `-Ofast`
(optional, `--include-ofast`) is reported as `fp_semantics = fast-math` and is never compared with the strict
builds in the report.

## Measurement protocol
1. Record environment (OS, CPU, logical CPUs, RAM, compiler version/target) and every command in `manifest.json`.
2. Build each variant (`-O2`, `-O3`, optionally `-Ofast`) once; the size is a run-time argument.
3. For each size: **validation run** (discarded) against the oracle, then `--warmup` discarded runs.
4. `--reps` timed rounds (default 9, ≥ 9 required by the phase gate). In each round every still-correct variant
   runs once, in an order drawn by a seeded Fisher-Yates shuffle (`order_seed`), so drift affects variants equally.
   Each sample is the kernel's own `time_ms` of a fresh process.
5. All samples are stored (`raw_timings.csv`, `results.json`). Reported: median, quartiles, IQR, MAD, min, max.
6. The report's O2-vs-O3 line is deliberately cautious: it prints the ratio of medians and calls the result
   *inconclusive* when the two interquartile ranges overlap. It never reports a minimum as the result.

Optional noise controls: `--affinity=HEXMASK` pins benchmark processes (`SetProcessAffinityMask` /
`sched_setaffinity`) and `--high-priority` raises the Windows priority class. They change absolute times (a pinned
single logical CPU is slower for memory-bound kernels) but sharply reduce variance, so they are recorded in the
manifest and any comparison must use the same setting for every candidate.

## Files written to `<out>/`
`manifest.json` (environment, protocol, variants, per-benchmark source hashes, all commands), `results.json`
(array of records), `results.csv`, `raw_timings.csv`, `logs/build_<candidate>.log`, `bin/` (ignored by git).

`results.csv` columns: `experiment_id, benchmark, source_hash, compiler_version, flags, opt_level, fp_semantics,
input_size, size_role, seed, candidate_id, build_status, correctness_status, failure_reason, warmup_runs,
repetitions, timing_samples_ms (JSON list), median_ms, q1_ms, q3_ms, iqr_ms, mad_ms, min_ms, max_ms,
binary_size_bytes, build_time_s, candidate_budget, search_strategy, tuning_wall_time_s, selected`.
`source_hash` is the SHA-256 of the kernel file followed by `common.hpp`. `validate_record` rejects records with
missing/empty metadata, a sample count different from `repetitions`, or statistics that do not match the samples;
loading a file with a missing field fails with the field's name.

## Commands
```
nova bench list
nova bench run [--kernels=a,b] [--sizes=n1,n2 | --role=tuning|holdout|all] [--reps=N] [--warmup=N] [--seed=N]
               [--include-ofast] [--affinity=HEXMASK] [--high-priority]
               [--compiler=g++] [--kernel-dir=benchmarks/kernels] [--id=NAME] [--out=DIR]
nova bench report <dir>
```
Process execution uses argument arrays (no shell) with timeouts and output caps. The Windows implementation is
tested; the POSIX (fork/exec) implementation is written but **has not been compiled or run** in this project's
environment.

## Limitations
- One machine, one compiler (MinGW.org GCC 6.3.0, 32-bit target). Results say nothing about other setups.
- Windows scheduling on a hybrid CPU is noisy; see the variance columns of any saved result.
- The kernels are small and simple; "faster at -O3 than -O2 here" does not generalize.

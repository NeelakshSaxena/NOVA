# Benchmark Lab

- `kernels/` — C++ benchmark kernels (`matmul`, `vector_add`, `stencil`) and `common.hpp`.
- `results/<experiment-id>/` — saved experiments: `manifest.json`, `results.json`, `results.csv`, `raw_timings.csv`,
  `logs/` (build logs). `bin/` holds the built executables and is not tracked.
- Method, schema and limitations: [../docs/benchmarks.md](../docs/benchmarks.md). Run `nova bench --help`-style
  usage via `nova --help`.

Saved so far (V0.6, tuning sizes only; holdout sizes reserved):
`V0.6-baseline` (unpinned), `V0.6-baseline-pinned` (CPU-pinned, high priority) and `V0.6-ofast-disclosure`
(pinned, includes `-Ofast` as a separate fast-math condition). These are measurements on one machine with
MinGW.org GCC 6.3.0 and support no general claim about GCC.

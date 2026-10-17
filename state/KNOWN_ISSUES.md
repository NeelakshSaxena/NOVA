# Known Issues and Blockers

No implementation issues have been verified yet because implementation has not started in this repository pack.

Add issues using:
- ID:
- Severity:
- Affected phase:
- Reproduction:
- Expected:
- Actual:
- Evidence:
- Attempts:
- Status:
- Next action:

## B-001 — CMake/CTest unavailable
- Severity: Blocking for V0.1 completion
- Affected phase: V0.1
- Reproduction: `command -v cmake` / `command -v ctest`
- Expected: CMake >= 3.16 and CTest on PATH
- Actual: not found; also not under C:\Program Files\CMake, VS, or MSYS2
- Evidence: state/evidence/V0.1/00_toolchain.log
- Attempts: searched PATH and common install dirs. Did not install anything (project rules forbid running downloaded code without user approval).
- Status: RESOLVED 2026-10-14 (user installed CMake 4.4.4)

## N-001 — Old compiler
- Severity: Low (non-blocking)
- Affected phase: V0.1 onward
- Detail: only compiler found is MinGW.org GCC 6.3.0. It accepts `-std=c++17` but has an incomplete C++17 library (e.g. no `<filesystem>`, `std::string_view` may be missing/partial). Code so far uses no C++17-only library features. A newer GCC will be needed before later phases rely on them.

## N-002 — No sanitizer coverage
- Severity: Low. Affected: all phases. MinGW GCC 6.3 has no ASan/UBSan; only `_GLIBCXX_DEBUG` bounds-checked builds were run. Revisit with a newer compiler.

## N-003 — POSIX process runner untested
- Severity: Medium for portability. `src/bench/process.cpp` has a fork/execvp (Linux affinity via sched_setaffinity) implementation that has never been compiled or run here. Only Windows is verified. Status: OPEN; verify on Linux before claiming support.

## N-004 — Old 32-bit compiler and noisy timing
- Severity: Medium for research claims. Only MinGW.org GCC 6.3.0 (i586 target) is available; modern GCC flags/passes (e.g. -march=native vectorization, -flto behaviors) differ. Unpinned timing on this hybrid CPU has IQR/median up to ~25%; pinned runs are much tighter but run on one logical CPU, so absolute times differ from unpinned. Claims must stay limited to this configuration.

## N-005 — Oracle is statistical
- Severity: Low. Correctness is judged from five output statistics, not the whole output array; a wrong program could theoretically pass. Mitigated by testing several seeds/sizes and a deliberately incorrect fixture.

## N-006 — Sanitizers unavailable
- Severity: Medium. MinGW.org GCC 6.3.0 has no ASan/UBSan/TSan/LSan runtimes (`cannot find -lasan` etc.). Substitute: full suite under checked standard containers (passes; found one real defect, fixed). Status: OPEN until a toolchain with sanitizers is available.

## N-007 — Not a git repository
- Severity: Low. No commit history exists, so evidence cites file hashes (source_hash in results) instead of commit ids; "clean checkout" was verified with a copy of the tree minus build products.

## N-008 — Evidence scope
- Severity: Info. Results come from one machine, three kernels, 12 workloads; the replay study re-uses measured samples; the adaptive policy's constants were written after the author had seen earlier timings of the same kernels (docs/adaptive.md, report section 10).

## Resolved: R-001 — `bench::shuffle` self-move
- Found 2026-10-15 by the checked-container run; fixed; regression test `shuffle_checked`.

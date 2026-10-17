# NOVA Roadmap Status

**Current phase:** V1.0 — Release (RELEASED for the verified configuration)  
**Current status:** V1.0 RELEASED (scoped; see docs/RELEASE.md)  
**Rule:** This status is a starting default for a new repository. Verify actual repository state before relying on it.

| Phase | Status | Completion evidence |
|---|---|---|
| V0.1 Foundation and CLI | COMPLETE | `state/evidence/V0.1/10_toolchain_cmake.log` … `14_smoke.log` (CMake 4.4.4, 4/4 CTest pass) |
| V0.2 Lexer | COMPLETE | state/evidence/V0.2/ (8/8 CTest, token dump, invalid-char location) |
| V0.3 Parser and AST | COMPLETE | state/evidence/V0.3/ (13/13 CTest, AST dump, syntax error locations) |
| V0.4 Semantic analysis and TAC | COMPLETE | state/evidence/V0.4/ (22/22 CTest, golden TAC, located diagnostics, AST/TAC interpreters agree) |
| V0.5 Basic optimizer | COMPLETE | state/evidence/V0.5/ (25/25 CTest, differential corpus, mutation controls) |
| V0.6 Benchmark lab and GCC baselines | COMPLETE | state/evidence/V0.6/, benchmarks/results/V0.6-* (raw samples + manifests; 32/32 CTest; 9/9 mutants killed) |
| V0.7 Bounded SmartPass | COMPLETE | state/evidence/V0.7/ (39/39 CTest, 11/11 mutants killed, 12 equal-budget runs saved in benchmarks/results/V0.7-*) |
| V0.8 Adaptive workload-aware search | COMPLETE | state/evidence/V0.8/, benchmarks/results/V0.8-* (47/47 CTest, 13/14 mutants killed + 1 equivalent, 288 table records, 7260 replayed runs, 27 real tune+holdout runs) |
| V0.9 Evaluation and report | COMPLETE | report/ (700 traced values), state/evidence/V0.9/, 48/48 CTest incl. 21 report tests |
| V1.0 Demonstration-ready release | RELEASED (verified configuration) | docs/RELEASE.md, state/evidence/V1.0/ |

## Active objective
All phases V0.1–V1.0 complete with evidence.

## Blockers
None.

## Next action
No further phase. Open items: N-003 (POSIX runner), N-006 (sanitizers) — need other environments.

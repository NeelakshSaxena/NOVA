# 04 — Phases, Versions and Release Gates

> **Status note (V1.0):** this file is the original plan. The authoritative status of every phase is `state/ROADMAP_STATUS.md`; commands named here may differ from the implemented CLI (see README).

Treat the version labels as a proposed release plan. Do not mark a version complete until its gate passes.

## V0.1 — Foundation and CLI scaffold
**Goal:** buildable, testable C++17 project.

Deliverables:
- CMake project and `nova` executable.
- `--help`, version output, consistent exit codes.
- Unit-test target and CI-ready commands.
- Directory structure and style/lint conventions.

Gate:
- Clean configure/build succeeds.
- Tests run with CTest.
- No compiler warnings introduced by the project under the chosen warning policy.

## V0.2 — Lexer
**Goal:** source text becomes located tokens.

Deliverables:
- token enum, token structure, lexer.
- line/column tracking and useful lexical errors.
- token-dump command.

Gate:
- tests cover every token, whitespace/comments, end-of-file, invalid characters and locations.

## V0.3 — Parser and AST
**Goal:** parse the defined NOVA Script grammar.

Deliverables:
- recursive-descent parser.
- precedence-aware expression parser.
- AST nodes and AST printer.
- parser diagnostics.

Gate:
- valid examples produce expected ASTs.
- invalid examples fail cleanly without crashes.
- precedence and parentheses tests pass.

## V0.4 — Semantic analysis and TAC
**Goal:** catch invalid variable use and generate deterministic intermediate code.

Deliverables:
- symbol table.
- declaration/use checks.
- TAC generator and `tac` command.

Gate:
- tests cover use-before-declaration, duplicate declaration, valid assignment and deterministic temporary names.

## V0.5 — Basic optimizer
**Goal:** implement small, testable TAC transformations.

Deliverables:
- constant folding.
- constant propagation.
- safe algebraic simplification.
- dead-code elimination.
- before/after IR display.

Gate:
- unit tests per rule.
- semantic differential tests against an interpreter/reference evaluator.
- optimizer terminates and preserves observable behavior for tested cases.

## V0.6 — Benchmark lab and fixed baselines
**Goal:** produce reliable GCC baseline measurements before adaptive search.

Deliverables:
- matrix multiplication and vector-add benchmarks.
- correctness tests and deterministic inputs.
- GCC `-O2` and `-O3` build/run scripts.
- repeated timing collection and environment metadata.
- JSON/CSV result schema.

Gate:
- raw timings retained.
- each baseline passes correctness checks.
- repeatability/variance reviewed before any performance claim.

## V0.7 — Bounded SmartPass search
**Goal:** evaluate a finite set of candidate configurations.

Deliverables:
- candidate registry and whitelisted configuration space.
- `nova tune <benchmark> --budget B`.
- baseline always included.
- build/correctness/benchmark state machine.
- report with every candidate and rejection reason.

Gate:
- budget enforced.
- invalid candidates never selected.
- same candidate config can be replayed.
- random search baseline available.

## V0.8 — Adaptive workload-aware search
**Goal:** add the candidate research contribution.

Deliverables:
- workload descriptors.
- explicit adaptive candidate-priority policy.
- persistent experiment history.
- deterministic seeds and equal-budget comparisons.
- ablation: with and without workload features.

Gate:
- adaptive and random search run with equal budgets.
- results across multiple workloads/seeds.
- holdout sizes evaluated.
- no claim of generality from one benchmark.

## V0.9 — Evaluation and report
**Goal:** turn experiments into a reproducible research artifact.

Deliverables:
- plots/tables generated from raw data.
- methodology and threats-to-validity section.
- reproducibility command and environment manifest.
- report distinguishes measured outcomes from hypotheses.

Gate:
- a clean checkout can reproduce the pipeline.
- all claims trace to saved raw results.
- negative results and regressions are included.

## V1.0 — Demonstration-ready release
Requirements:
- all selected features pass tests;
- README build/run instructions are accurate;
- sample NOVA Script output is documented;
- benchmark results have provenance and repeated measurements;
- no feature is described as implemented if it is only planned.

## Scope control

Do not start LLVM integration, a full C++ frontend, machine-learning models, parallel/GPU tuning or GUI work before V0.9. If the core research cannot be evaluated reliably, extra features do not improve the quality of the result.

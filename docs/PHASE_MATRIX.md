# Phase Matrix

| Version | Focus | Main pass gate |
|---|---|---|
| V0.1 | Foundation/CLI | Clean CMake build, real tests, help/version/invalid-argument behavior |
| V0.2 | Lexer | All tokens and source locations tested; invalid characters fail cleanly |
| V0.3 | Parser/AST | Grammar coverage, precedence/associativity, invalid syntax diagnostics |
| V0.4 | Semantics/TAC | Symbol rules and deterministic TAC verified |
| V0.5 | Optimizer | Pass unit tests, differential semantics, termination |
| V0.6 | Benchmark baselines | Correctness, raw repeated timings, metadata and variance |
| V0.7 | Bounded SmartPass | Budget enforcement, candidate lifecycle, correctness gating |
| V0.8 | Adaptive search | Equal-budget random comparison, ablation and holdout tests |
| V0.9 | Evaluation | Reproducible reports generated from raw evidence |
| V1.0 | Release | Full checklist, no unsupported claims, complete evidence |

# 01 — Product Requirements and Problem Definition

## 1. Problem statement

Developers who want faster numerical code face several related tasks: understanding optimization levels and compiler options, deciding which configurations to test, validating that transformed/build variants preserve correct output, and determining whether measured speedups are repeatable. NOVA addresses a deliberately narrow version of this problem for selected numerical C++ programs.

This is not a claim that every developer has this problem or that NOVA solves all performance engineering. The problem framing is grounded in the existence and complexity of compiler configuration choices, established autotuning research, and formal benchmark-reproducibility guidance. A user study or developer survey would be needed to make stronger claims about prevalence.

## 2. Evidence used to source the problem

| Observed problem area | Evidence | What NOVA does about it | What the evidence does *not* prove |
|---|---|---|---|
| Compiler optimization choices have trade-offs and many options | GCC's official optimization manual documents distinct `-O` levels, many individual flags, and different speed/size/debugging trade-offs. [R1] | Starts with a small, declared search space; records exact flags and compares results for each workload. | It does not prove that developers universally find flags confusing. |
| Search spaces can be large and no one search method works best for every problem | The OpenTuner paper discusses domain-informed search-space representations, intractable spaces, and differences among problem search landscapes. [R2] | Tests a workload-feature-guided adaptive search against fixed and random baselines under equal evaluation budgets. | It does not establish that NOVA's proposed strategy is new or superior. |
| Performance results can be noisy and need repeatable methodology | Google Benchmark documents repetition controls and random interleaving; SPEC's run rules emphasize repeated runs, validation and reproducibility. [R3, R4] | Uses repeated measurements, preserves raw samples, records the environment and reports median plus spread. | NOVA's small educational benchmarks are not SPEC-certified results. |
| Compiler optimizations can affect semantics or require proof/validation | GCC documents that `-Ofast` enables transformations not valid for all strictly standard-compliant programs; Souper demonstrates synthesis-based optimization of LLVM IR. [R1, R5] | Correctness-checks candidate outputs and keeps aggressive floating-point modes separate. | Output tests are not a formal proof of equivalence for every possible input. |
| Compiler construction itself has many stages | LLVM's Kaleidoscope tutorial presents a staged implementation of lexer, parser/AST, IR generation and optimizer/JIT extensions. [R6] | Builds a small, explainable language frontend without taking on full C++ parsing. | NOVA does not implement LLVM or its complete backend. |

## 3. User-facing outcomes

A user should be able to:
- See the token stream, AST and TAC for a NOVA Script program.
- See which semantic rule rejected an invalid program.
- Inspect each basic optimization before and after.
- Run a numerical benchmark with GCC baseline settings.
- Run SmartPass with a declared budget.
- See every candidate, build status, correctness status, flags, runtime samples, binary size and reason for acceptance/rejection.
- Re-run an experiment using the recorded configuration.

## 4. Functional requirements

### Compiler track
- FR-C1: Tokenize identifiers, integer literals, keywords, operators, punctuation and comments.
- FR-C2: Track source line/column for useful diagnostics.
- FR-C3: Parse expressions with correct operator precedence and parentheses.
- FR-C4: Construct an AST independent of printed formatting.
- FR-C5: Maintain a symbol table and reject undeclared/redeclared variables according to the language rules.
- FR-C6: Generate deterministic TAC.
- FR-C7: Apply each optimizer pass in isolation and expose before/after IR.
- FR-C8: Stop optimization when no changes occur or a documented pass limit is reached.

### SmartPass track
- FR-S1: Define benchmarks and candidate configurations declaratively.
- FR-S2: Include baseline candidates in every experiment.
- FR-S3: Compile candidates through an argument-list process invocation, not a shell-concatenated string.
- FR-S4: Capture compiler exit code and diagnostics.
- FR-S5: Run correctness tests before benchmarking.
- FR-S6: Run multiple timing repetitions and retain raw data.
- FR-S7: Enforce a candidate-evaluation budget.
- FR-S8: Record failed candidates instead of silently dropping them.
- FR-S9: Select only from valid candidates according to a declared objective.
- FR-S10: Emit a machine-readable report and human-readable explanation.
- FR-S11: Record compiler version, command, source hash, machine/OS, inputs and timing protocol.

## 5. Non-functional requirements
- Deterministic behavior for compiler stages given the same input.
- Reproducible experiment configuration.
- Clear separation between educational compiler and C++ benchmark runner.
- Small enough implementation for a student to explain line by line.
- No claim of correctness based only on a single output example.
- No claim of speedup based on a single timing run.
- No hidden network calls or external service dependency at runtime.

## 6. Constraints and assumptions
- C++17 is the implementation language.
- CMake is the build system.
- Terminal-only interface.
- GCC is the initial C++ benchmark compiler.
- First benchmark family is numerical kernels, beginning with matrix multiplication.
- The custom language is intentionally not C++.
- Initial research scope is CPU/single-process only.

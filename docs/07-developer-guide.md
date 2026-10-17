# 07 — Developer Guide and Working Rules

## 1. Development principles

- Build one small vertical slice at a time.
- Keep compiler stages independently inspectable.
- Keep SmartPass independent of the NOVA Script parser.
- Prefer readable C++ over clever abstractions.
- Use RAII and standard containers.
- Keep diagnostics actionable.
- Use tests as the specification for each feature.
- Make each research experiment replayable.
- Document a design decision when it changes the architecture or evaluation method.

## 2. Suggested C++ module boundaries

```text
include/nova/
  cli/                 command parsing and help
  frontend/            Token, Lexer, Parser, AST
  semantic/            SymbolTable, SemanticAnalyzer
  ir/                  TAC instruction and printer
  optimizer/           pass interface and individual passes
  smartpass/            benchmark, candidate, runner, evaluator, report
src/
  cli/
  frontend/
  semantic/
  ir/
  optimizer/
  smartpass/
tests/
```

Avoid putting all logic in `main.cpp`. `main` should parse the command, call a component and translate the result to an exit code.

## 3. Recommended implementation order

1. `--help` and version output.
2. Lexer and token-dump command.
3. Parser and AST printer.
4. Semantic analysis.
5. TAC generation.
6. One optimizer pass, then tests.
7. Remaining passes one at a time.
8. Reference interpreter/evaluator for differential tests.
9. Standalone benchmark executables.
10. Baseline runner and result serialization.
11. Candidate state machine and budget.
12. Random search.
13. Adaptive workload-aware policy.
14. Evaluation scripts and report generation.

## 4. Coding conventions

- C++17; use `std::optional` where appropriate.
- Use enum classes for token and instruction kinds.
- Use explicit error/result types rather than ambiguous sentinel values.
- Keep source locations on tokens and AST nodes where diagnostics need them.
- Make pass interfaces explicit, for example: `PassResult run(ProgramIR&)`.
- Ensure optimizer passes report whether they changed the IR.
- Avoid hidden global mutable state.
- Use stable formatting for golden tests.
- Keep external process invocation in one tested module.
- Pin tool/compiler versions in experimental metadata, not necessarily in the core executable.

## 5. CLI behavior

Implemented commands (V1.0; the authoritative list is `nova --help` and the README):

```bash
nova --help
nova --version
nova tokens examples/basic.nova
nova ast examples/basic.nova
nova tac examples/basic.nova
nova run examples/basic.nova
nova opt examples/optimize_demo.nova --show-all
nova bench list
nova bench run --kernels=stencil --reps=9
nova tune stencil --budget=6 --strategy=adaptive
nova explain benchmarks/results/<tuning-result-dir>
nova evaluate benchmarks/results/<tuning-result-dir>
nova table stencil --role=all
nova study --tables=... --out=...
nova history --tables=... --out=...
```

The original proposal used different names for some commands (for example `nova optimize`, `nova bench matrix`,
`nova explain latest`); those names do not exist. Help text must not advertise unimplemented functionality as working.

## 6. Example NOVA Script

`examples/basic.nova`:

```text
let x = 10;
let y = 20;
let z = x * 2 + y;
print z;
```

Expected conceptual TAC (exact temporary names may differ):

```text
t1 = x * 2
t2 = t1 + y
z = t2
print z
```

If constant propagation is enabled and the compiler can prove the values, the optimized representation may reduce to `z = 40` and `print z`, depending on the language's declaration/assignment model. The test must match the implemented semantics rather than this illustrative sketch.

## 7. Example GCC comparison workflow

```bash
mkdir -p build/bench
g++ --version

g++ -std=c++17 -O2 -DNOVA_N=512 examples/matmul.cpp -o build/bench/matmul_O2
g++ -std=c++17 -O3 -DNOVA_N=512 examples/matmul.cpp -o build/bench/matmul_O3

./build/bench/matmul_O2
./build/bench/matmul_O3
```

This validates that both binaries run; it is not sufficient as a benchmark protocol. Use the documented harness for repeated timings, correctness and metadata.

## 8. Git workflow

Suggested branches:
- `main`: tested and demoable.
- `feat/lexer`, `feat/parser`, etc.: one feature per branch.
- `exp/<experiment-id>`: only if experiments need code changes; raw results should be tied to a commit hash.

Each pull request/commit should include:
- what changed;
- tests added/run;
- limitations;
- whether benchmark behavior or compiler semantics changed.

## 9. Research notebook

For every experiment, record:
- question and hypothesis;
- exact benchmark/source hash;
- search strategy and budget;
- compiler version and environment;
- candidate-space definition;
- command used to run it;
- raw output/result file;
- observations and failures;
- next experiment decision.

Never discard a failed experiment just because it weakens the hypothesis.

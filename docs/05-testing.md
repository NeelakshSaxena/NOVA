# 05 — Testing Strategy

## 1. Testing layers

### Unit tests
Test each component in isolation:
- lexer tokenization and source locations;
- parser precedence and AST shape;
- semantic rules and diagnostics;
- TAC emission;
- each optimizer rewrite;
- candidate configuration validation;
- timing statistics and report serialization.

### Integration tests
Run full CLI commands against example files:
- valid source through tokens, AST and TAC;
- invalid source returns non-zero and readable diagnostics;
- optimizer output is deterministic;
- benchmark candidate builds, executes and produces a report;
- failed build/correctness candidates are excluded from ranking.

### Regression tests
Every bug fix adds a small input and expected behavior. Keep both the source and expected diagnostic/output.

### Differential tests
For NOVA Script, create a small reference evaluator/interpreter and compare its results with the optimized and unoptimized execution path. This is valuable but does not constitute formal proof.

For C++ benchmarks, compare candidate output with a trusted reference implementation or expected checksum/tolerance policy. Floating-point comparisons must use a documented tolerance and account for numerical stability.

### Property/fuzz tests (later)
Generate small expressions/programs and check:
- lexer/parser never crash;
- valid generated programs parse;
- optimized and unoptimized executions agree for the supported semantics;
- TAC optimizer reaches a fixed point or reports its iteration cap.

## 2. Minimum test matrix

| Area | Example | Expected result |
|---|---|---|
| Lexer | `let x = 12;` | Expected token sequence |
| Lexer error | `let x = @;` | Location-aware error |
| Parser | `print 2 + 3 * 4;` | Multiplication nested under addition |
| Parser error | Missing semicolon | Clear parse error, non-zero exit |
| Semantics | `print missing;` | Undeclared-variable error |
| Semantics | Duplicate `let x` | Duplicate declaration error |
| Constant folding | `t1 = 2 + 3` | `t1 = 5` |
| Propagation | `x = 4; y = x + 1` | Fold/use constant where safe |
| DCE | unused pure assignment | Assignment removed |
| Correctness gate | deliberately wrong benchmark candidate | Rejected before timing/ranking |
| Budget | budget = 3 | At most three counted candidate evaluations, per documented definition |
| Repeatability | same source/config/seed | Same candidate sequence and metadata, timing naturally may vary |

## 3. Correctness policy for numerical benchmarks

- Use deterministic input generation with recorded seed.
- Validate all output elements, or use a checksum plus additional spot checks where full validation is too costly.
- For floating-point output, use a documented absolute/relative tolerance:
  `abs(actual - expected) <= atol + rtol * abs(expected)`.
- Run correctness validation before timing.
- Do not compile correctness and timed binaries with different candidate flags.
- If a candidate changes floating-point semantics, isolate it as a separate experimental condition.
- For integer kernels, prevent undefined behavior from overflow by choosing safe input bounds or using wider types.

## 4. Exit codes (proposed contract)

- `0`: command completed and all required checks passed.
- `1`: runtime/build/benchmark failure.
- `2`: CLI usage or configuration error.
- `3`: source lexical/syntax/semantic error.
- `4`: correctness failure.
- `5`: incomplete experiment or missing required measurements.

The exact mapping can be simplified in the implementation, but it must be documented and tested.

## 5. Test commands

Once targets exist:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Run sanitizers in a separate build where supported:

```bash
cmake -S . -B build-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

Do not mix sanitizer builds into performance measurements.

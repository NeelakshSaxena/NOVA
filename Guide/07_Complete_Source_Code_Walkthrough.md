# 07 — Complete Source Code Walkthrough & File Guide

> **Target Platform:** Google NotebookLM Source Document  
> **Source Directory:** `src/`, `include/nova/`  

---

## 1. Top-Level Entry & CLI Architecture

### `src/main.cpp`
* **Responsibility:** Minimal, dependency-free entry point.
* **Mechanism:** Converts C `argc`/`argv` into a standard `std::vector<std::string>` and delegates execution directly to `nova::run_cli(args, std::cout, std::cerr)`.
* **Design Rationale:** Decouples CLI logic from standard streams, enabling comprehensive in-memory testing inside CTest without spawning child processes.

### `src/cli.cpp` (Header: `include/nova/cli.hpp`)
* **Responsibility:** Top-level command routing, argument parsing, error formatting, and front-end pipeline invocation.
* **Exit Codes:**
  * `0`: Successful execution.
  * `1`: Input or runtime error (unreadable file, syntax error, semantic error, runtime division by zero).
  * `2`: Usage error (unknown command, missing argument, unrecognized flag).
* **Key Function:** `int front_end(path, stage, err, lexed, program)` runs the pipeline up to the requested stage (`Stage::Lex`, `Stage::Parse`, or `Stage::Semantic`), emitting located diagnostics to `err`.

---

## 2. Track 1: Educational Compiler Subsystem

### `src/lexer.cpp` (Header: `include/nova/lexer.hpp`)
* **Data Structures:** `Token` struct holding `TokenKind`, `std::string lexeme`, `int64_t int_value`, `int line`, `int col`.
* **Key Functions:**
  * `LexResult lex(const std::string& source)`: Main scanning loop.
  * `std::string dump_tokens(const std::vector<Token>& tokens)`: Formats tokens as `line:col KIND 'lexeme'`.

### `src/parser.cpp` & `src/ast.cpp` (Headers: `include/nova/parser.hpp`, `include/nova/ast.hpp`)
* **Data Structures:** AST node hierarchy derived from `AstNode`: `Program`, `LetStatement`, `PrintStatement`, `BinaryExpr`, `UnaryExpr`, `VariableExpr`, `IntegerExpr`.
* **Key Functions:**
  * `ParseResult parse(const std::vector<Token>& tokens)`: Entry point.
  * `std::unique_ptr<Expr> parse_expression()`, `parse_term()`, `parse_factor()`, `parse_primary()`: Precedence climbing recursive descent methods.
  * `std::string dump_ast(const Program& program)`: Indented tree representation for testing.

### `src/semantic.cpp` (Header: `include/nova/semantic.hpp`)
* **Data Structures:** `SymbolTable` mapping variable names to their definition `SourceLocation`.
* **Key Functions:**
  * `SemanticResult analyze(const Program& program)`: Traverses the AST to enforce declare-before-use and reject duplicate variable declarations.

### `src/tac.cpp` (Header: `include/nova/tac.hpp`)
* **Data Structures:** `TacInstruction` struct with `Opcode` (`Assign`, `Copy`, `Add`, `Sub`, `Mul`, `Div`, `Neg`, `Print`), destination operand, left operand, right operand.
* **Key Functions:**
  * `TacProgram generate_tac(const Program& program)`: Lowers the hierarchical AST into linear TAC quadruples using a monotonically increasing temporary counter `$t1`, `$t2`, etc.
  * `std::string print_tac(const TacProgram& program)`: Serializes TAC into printable instructions.

### `src/optimizer.cpp` (Header: `include/nova/optimizer.hpp`)
* **Data Structures:** `OptimizeResult` holding transformed `TacProgram`, step-by-step audit logs, iteration count, and convergence flag.
* **Key Functions:**
  * `OptimizeResult optimize(tac, passes, show_all)`: Outer fixed-point loop running registered passes until convergence or 10,000 iterations.
  * `bool pass_fold(...)`: Evaluates constant expressions at compile-time.
  * `bool pass_propagate(...)`: Replaces variable references with known constant values.
  * `bool pass_simplify(...)`: Applies algebraic identity rewrites.
  * `bool pass_dce(...)`: Removes dead variable assignments whose results are never read.

### `src/interp.cpp` (Header: `include/nova/interp.hpp`)
* **Responsibility:** Reference runtime execution.
* **Key Functions:**
  * `RunResult run_ast(const Program& program)`: Direct tree-walking interpreter.
  * `RunResult run_tac(const TacProgram& program)`: Linear TAC virtual machine.
  * Both interpreters produce identical output on all valid inputs and report identical located runtime errors on division by zero.

---

## 3. Track 2: SmartPass Subsystem

### `src/bench/process.cpp` (Header: `include/nova/bench/process.hpp`)
* **Responsibility:** Safe, non-shell process spawning with timeouts and CPU core affinity.
* **Key Functions:**
  * `ProcessResult run_process(cmd, args, timeout_ms, affinity_mask, high_priority)`: Uses `CreateProcessW` and Windows Job Objects.

### `src/bench/harness.cpp` (Header: `include/nova/bench/harness.hpp`)
* **Responsibility:** Benchmark lifecycle execution, output capture, and correctness verification.
* **Key Functions:**
  * `ValidationResult validate_kernel(output, expected, tolerance)`: Computes the 5 statistical invariants and verifies relative deviation $\le 10^{-9}$.

### `src/tune/tuner.cpp` (Header: `include/nova/tune/tuner.hpp`)
* **Responsibility:** Main autotuning search loop.
* **Key Functions:**
  * `TuneResult run_tune(config, space, whitelist, ...)`: Coordinates candidate compilation, correctness gating, timing, and baseline comparisons within budget $B$.

### `src/tune/features.cpp` & `src/tune/adaptive.cpp`
* **Responsibility:** Workload descriptor extraction and adaptive candidate scoring.
* **Key Functions:**
  * `WorkloadFeatures extract_features(source)`: Parses loop nests and arithmetic intensity.
  * `std::vector<CandidateScore> score_candidates(features, history, pilot)`: Computes composite priority scores to guide the next evaluation in budget $B$.

### `src/tune/evaluate.cpp`
* **Responsibility:** Holdout input size re-measurement.
* **Key Functions:**
  * Measures the selected winner against `-O2`/`-O3` on reserved holdout sizes to verify generalization.

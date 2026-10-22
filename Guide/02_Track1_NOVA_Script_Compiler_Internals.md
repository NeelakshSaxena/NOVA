# 02 — Track 1: NOVA Script Compiler Internals

> **Target Platform:** Google NotebookLM Source Document  
> **Source Files Covered:** `src/lexer.cpp`, `src/parser.cpp`, `src/ast.cpp`, `src/semantic.cpp`, `src/tac.cpp`, `src/optimizer.cpp`, `src/interp.cpp`  

---

## 1. The NOVA Script Language Specification
NOVA Script is an educational imperative language for integer computations.

### Formal Grammar (EBNF):
```ebnf
Program    ::= Statement* EOF
Statement  ::= "let" IDENTIFIER "=" Expression ";"
             | "print" Expression ";"
Expression ::= Term (("+" | "-") Term)*
Term       ::= Factor (("*" | "/") Factor)*
Factor     ::= ["-"] Primary
Primary    ::= INTEGER | IDENTIFIER | "(" Expression ")"
```

### Language Rules:
* **Arithmetic Type:** 64-bit signed two's-complement integers (`int64_t`). Arithmetic wraps predictably on overflow.
* **Division Semantics:** Truncates towards zero. Division by zero is a **runtime error**, not a compile-time error.
* **Scope:** Single global scope. Variables must be declared before use.
* **Order of Evaluation:** In `let x = expr;`, `expr` is evaluated *before* `x` is added to the symbol table (preventing `let x = x + 1;` when `x` was not previously declared).

---

## 2. Compilation Stages

### Stage 1: Lexical Analysis (Lexer)
* **File:** `src/lexer.cpp` (Header: `include/nova/lexer.hpp`)
* **Tokens Produced:**
  * Keywords: `KW_LET`, `KW_PRINT`
  * Literals: `INTEGER` (64-bit parsed value)
  * Identifiers: `IDENTIFIER` (alphanumeric starting with letter or `_`)
  * Operators: `PLUS (+)`, `MINUS (-)`, `STAR (*)`, `SLASH (/)`, `ASSIGN (=)`
  * Delimiters: `SEMICOLON (;)`, `LPAREN (()`, `RPAREN ())`
  * Stream Terminator: `EOF_TOKEN`
* **Source Locations:** Every token tracks 1-indexed `line` and `column` for error diagnostics.
* **Error Recovery:** If an illegal character is encountered (e.g., `@` or `#`), the lexer records a diagnostic and skips the character, allowing subsequent tokens to be scanned.

### Stage 2: Recursive Descent Parser & AST
* **File:** `src/parser.cpp`, `src/ast.cpp` (Header: `include/nova/parser.hpp`, `include/nova/ast.hpp`)
* **Technique:** Precedence climbing:
  * `parse_expression()` handles level 1 precedence (`+`, `-`).
  * `parse_term()` handles level 2 precedence (`*`, `/`).
  * `parse_factor()` handles single unary minus (`-x`). Multiple unaries (`--x`) are rejected.
  * `parse_primary()` handles parenthesized sub-expressions, integers, and identifiers.
* **Recursion Guard:** A hard depth limit of 1,000 prevents call-stack overflow on maliciously nested parentheses.
* **Error Policy:** Stops at the first syntax error with a deterministic diagnostic and exit code 1.

### Stage 3: Semantic Analysis & Symbol Table
* **File:** `src/semantic.cpp` (Header: `include/nova/semantic.hpp`)
* **Data Structure:** `SymbolTable` tracking known variable names and their declaration locations.
* **Static Invariants Checked:**
  1. **Declare-before-use:** Expressions cannot reference unknown identifiers.
  2. **Duplicate detection:** Re-declaring an identifier in the global scope emits an error citing both original and attempted declaration sites.
  3. **RHS initialization check:** The right-hand side of a declaration is analyzed before adding the variable name to the table.

### Stage 4: Three-Address Code (TAC) Generation
* **File:** `src/tac.cpp` (Header: `include/nova/tac.hpp`)
* **Representation:** Linear quadruples with at most one operator and three operands.
* **Instruction Kinds:**
  * `Assign`: `x = 10`
  * `Copy`: `x = y`
  * `Binary`: `$t1 = x * 2`
  * `Unary`: `$t2 = -x`
  * `Print`: `print $t1`
* **Temporaries:** Named `$t1`, `$t2`, etc. User identifiers cannot begin with `$`, preventing naming collisions.

---

## 3. The 4-Pass Fixed-Point Optimizer
* **File:** `src/optimizer.cpp` (Header: `include/nova/optimizer.hpp`)
* Passes execute sequentially in an outer loop until no pass modifies the code (fixed point) or an iteration cap of 10,000 is reached.

### The 4 Passes:
1. **Constant Folding (`fold`):** Evaluates arithmetic on known literals at compile time:
   * `$t1 = 4 * 2` $\to$ `$t1 = 8`
   * `$t2 = 3 + 4` $\to$ `$t2 = 7`
2. **Constant Propagation (`propagate`):** Replaces variables with their known constant values downstream:
   * `a = 4; $t1 = a * 2` $\to$ `a = 4; $t1 = 4 * 2`
3. **Algebraic Simplification (`simplify`):** Applies mathematical identity rules:
   * $x + 0 \to x$, $0 + x \to x$
   * $x - 0 \to x$, $x - x \to 0$
   * $x \times 1 \to x$, $1 \times x \to x$
   * $x \times 0 \to 0$, $0 \times x \to 0$
   * $x / 1 \to x$
4. **Dead-Code Elimination (`dce`):** Deletes assignments to variables that are never read before reassignment or program termination. `print` statements are preserved.

### The Critical Safety Rule:
* **Division by zero causes a runtime halt.** If a division operation `$t1 = a / b` has an unknown divisor or zero divisor, it is **never** deleted by DCE and **never** folded by the constant folder. Deleting a division that crashes would silently alter program semantics.

---

## 4. End-to-End Optimization Example
Given source code (`examples/optimize_demo.nova`):
```nova
let a = 4;
let b = a * 2 + 0;
let unused = b - b;
let z = 1 - 1;
print b * 1;
print a * (3 + 4);
```

### Transformation Trace (`nova opt --show-all`):
1. **Original TAC:**
   ```text
   a = 4
   $t1 = a * 2
   b = $t1 + 0
   unused = b - b
   z = 1 - 1
   $t2 = b * 1
   print $t2
   $t3 = 3 + 4
   $t4 = a * $t3
   print $t4
   ```
2. **Iteration 1:**
   * `fold`: `z = 0`, `$t3 = 7`
   * `propagate`: `$t1 = 4 * 2`, `$t4 = 4 * $t3`
   * `simplify`: `b = $t1`, `unused = 0`, `$t2 = b`
   * `dce`: deletes `unused`, `z`, `a`
3. **Iteration 2:**
   * `fold`: `$t1 = 8`, `$t4 = 28`
   * `propagate`: `print 8`, `print 28`
   * `dce`: deletes `$t1`, `b`, `$t2`, `$t4`
4. **Fixed Point Reached (Iteration 3):**
   ```text
   print 8
   print 28
   ```
The entire computation is reduced to two constant print statements without altering program behavior.

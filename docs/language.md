# NOVA Script — semantics (implemented in V0.4)

Syntax is specified in [grammar.md](grammar.md). This document defines meaning. Everything here is
implemented and tested (`tests/test_semantic.cpp`, `tests/test_tac.cpp`).

## Values and types
- The only type is a signed 64-bit integer.
- Arithmetic is two's complement and **wraps** on overflow (`9223372036854775807 + 1` is
  `-9223372036854775808`). There is no undefined behavior.
- Unary minus and `+ - *` wrap. The literal `-9223372036854775808` cannot be written directly (literals
  must be <= 9223372036854775807); write `-9223372036854775807 - 1`.
- Division truncates toward zero (`-7 / 2` is `-3`). `INT64_MIN / -1` wraps to `INT64_MIN`.
- **Division by zero is a runtime error**, not a compile-time error, even when the divisor is a literal
  `0`. Execution stops at that point; output already printed is kept. The error is reported as
  `file:line:col: error: runtime error: division by zero`, located at the `/` operator, exit code 1.

## Scope and declarations
- There is a single global scope; no blocks, functions or shadowing.
- `let x = e;` declares `x`. `e` is checked **before** `x` is declared, so `let x = x;` is an error unless `x`
  was declared earlier (which would be a duplicate declaration).
- Declaring a name twice is an error: `variable 'x' is already declared (first declared at L:C)`. The first
  declaration is kept.
- A variable must be declared before any use: `use of undeclared variable 'x'`.
- `x = e;` requires `x` to be already declared: `assignment to undeclared variable 'x'` (reported at the
  start of the statement).
- A declaration always has an initializer, so there are no uninitialized variables.
- All semantic errors are reported (not only the first), in order of detection: within a statement, errors in
  the right-hand side come before the assignment-target error.
- Programs with any lexical, syntax or semantic error are never lowered to TAC or executed.

## Evaluation
- Statements run in order. Operands of a binary operator are evaluated left to right.
- `print e;` writes the decimal value and a newline. It is the only output.

## Three-address code (TAC)
Straight-line code (the language has no control flow). Instructions:

| Text | Meaning |
|---|---|
| `d = a` | copy |
| `d = neg a` | negation |
| `d = a + b`, `-`, `*`, `/` | arithmetic (`/` may fail at runtime) |
| `print a` | output |

Operands are integer constants or variables. Temporaries are named `$t1`, `$t2`, ... (`$` cannot occur in a
NOVA identifier). Lowering rules: operands are evaluated left to right, innermost first; each operation that
is not the top of an assignment gets the next temporary number, numbered in emission order across the whole
program; the top operation of `let`/assignment writes directly to the variable; a plain constant/variable
right-hand side becomes a copy.

Example (`examples/basic.nova`):

```
x = 10
y = 20
$t1 = x * 2
z = $t1 + y
print z
```

Commands: `nova tac <file>` prints this; `nova run <file>` executes it (reference interpreter). Two independent
evaluators exist (AST-based and TAC-based) and the tests require them to agree.

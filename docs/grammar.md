# NOVA Script — lexical rules and grammar (as implemented in V0.3)

This document describes exactly what `nova tokens` and `nova ast` accept. Semantics (declaration rules,
division, evaluation) are **not** implemented yet; the parser only checks syntax.

## Lexical rules

- Whitespace: space, tab, `\r`, `\n`. Newline starts a new line. No comments exist (`#`, `//` are errors).
- Locations are 1-based line:column. Columns count bytes; a tab counts as one column.
- Identifier: `[A-Za-z_][A-Za-z0-9_]*`. Keywords `let` and `print` are case-sensitive; `letx` is an identifier.
- Integer literal: decimal digits, must fit in a signed 64-bit integer (max 9223372036854775807). Negative
  numbers are unary minus applied to a literal. `12ab` is one invalid-number error; `3.5` is an error.
- Punctuation/operators: `= + - * / ( ) ;`
- Any other character is an error (`unexpected character`, one error per character; a non-ASCII UTF-8
  character counts as one error). Lexing continues after errors so all are reported.

## Grammar

```ebnf
program     = { statement } ;
statement   = "let" identifier "=" expression ";"
            | identifier "=" expression ";"
            | "print" expression ";" ;
expression  = term { ("+" | "-") term } ;
term        = unary { ("*" | "/") unary } ;
unary       = [ "-" ] primary ;
primary     = integer | identifier | "(" expression ")" ;
```

- `+ - * /` are left associative: `8 - 3 - 1` is `(8 - 3) - 1`; `*` and `/` bind tighter than `+` and `-`.
- Unary minus binds tighter than binary operators and is allowed **at most once** per operand:
  `-x`, `2 * -3` and `-(-3)` are valid; `--3` is a syntax error.
- Parentheses only group; they have no AST node.
- An empty file is a valid empty program.
- Limit: expression trees may be at most 1000 levels deep (nested parentheses or a chain of ~1000
  operators); deeper input is rejected with "expression is nested too deeply".

## Errors

The parser stops at the first syntax error and reports one diagnostic:
`<file>:<line>:<col>: error: expected <what>, found <token>` where `<token>` is `end of file`,
`identifier 'x'`, `integer '5'`, `keyword 'let'` or a quoted punctuation symbol. If the file has lexical
errors, they are reported and the parser is not run.

## AST dump (`nova ast <file>`)

Two-space indentation per level; node names `Program`, `Let <name>`, `Assign <name>`, `Print`,
`Binary <op>`, `Unary -`, `Integer <n>`, `Variable <name>`. Example for `let x = 2 + 3 * 4;`:

```
Program
  Let x
    Binary +
      Integer 2
      Binary *
        Integer 3
        Integer 4
```

Exit codes for `tokens` and `ast`: 0 success, 1 unreadable file or source errors, 2 usage error.

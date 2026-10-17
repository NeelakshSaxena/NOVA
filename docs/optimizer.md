# NOVA TAC optimizer (implemented in V0.5)

Four independent passes over straight-line TAC ([language.md](language.md)). Each is callable alone
(`nova opt <file> --passes=fold`) and returns whether it changed anything. "Observable behaviour" means: the
sequence of printed values, and whether and where execution stops with a runtime error. Every pass must
preserve it exactly. These passes are an *educational* optimizer; they say nothing about GCC or SmartPass
results.

All arithmetic uses the language semantics (64-bit wrapping, truncating division), implemented once in
`include/nova/arith.hpp` and shared with the interpreters.

## 1. Constant folding (`fold`)
- **Precondition:** an instruction whose operands are all constants (`neg c`, or `c1 op c2`).
- **Rewrite:** replace by `dst = <result>`.
- **Caveats:** `c / 0` is **never** folded (it must still fail at run time, at the same location).
  `INT64_MIN / -1` folds to `INT64_MIN` (defined wrap). Overflow folds to the wrapped value.

## 2. Constant propagation (`propagate`)
- **Precondition:** forward scan over straight-line code; a variable is "known" after `v = <constant>`.
- **Rewrite:** a use of a known variable becomes the constant. A definition of `v` by anything other than a
  constant copy makes `v` unknown; a new constant copy replaces the old known value (reassignment is handled).
- **Caveats:** operands are substituted before the instruction's own definition takes effect (`x = x + 1`
  uses the old `x`). Substituting a known zero into a divisor keeps the division (`1 / 0`), so the runtime
  error is unchanged. Only constants are propagated, not variable copies.

## 3. Algebraic simplification (`simplify`)
Rewrites, all exact under wrapping arithmetic:

| Pattern | Result |
|---|---|
| `x + 0`, `0 + x`, `x - 0`, `x * 1`, `1 * x`, `x / 1` | `x` |
| `x * 0`, `0 * x` | `0` |
| `x - x` (same variable) | `0` |
| `0 - x` (variable x) | `neg x` |

- **Not rewritten (on purpose):** `x / x` and `0 / x` (x may be zero, which must fail), `x * 2`, `1 - x`,
  anything needing knowledge of values. No strength reduction, reassociation or floating-point style tricks.
- **Caveat on `x * 0`:** safe because operands are already-evaluated variables/constants. A division that
  produces the operand is a separate instruction that stays (see DCE rule below), so its runtime error is kept.

## 4. Dead-code elimination (`dce`)
- **Precondition:** backward liveness scan. At the end of the program no variable is live (only `print`
  is observable).
- **Rewrite:** delete an assignment whose target is not live afterwards, **unless it can fail**. `print` is
  never deleted. A division is deletable only if its divisor is a non-zero constant; any other division is
  kept even when its result is unused, because deleting it could hide a division-by-zero error.
- **Caveats:** a read-modify-write (`x = x + 1`) keeps the earlier definition of `x` alive.

## Pipeline and termination
`optimize` runs the chosen passes in order (default `fold, propagate, simplify, dce`) repeatedly until a full
sweep changes nothing — a fixed point. Termination: every pass that reports a change strictly decreases the
measure (#binary instructions, #non-copy instructions, #variable operand uses, #instructions) in
lexicographic order, so a fixed point is reached after finitely many sweeps. As a safety net against a buggy
pass the loop stops after `kMaxOptimizeIterations = 10000` sweeps and reports `converged = false` (the CLI
prints a warning). Output is deterministic: no hashing/ordering that depends on addresses.

## CLI
```
nova opt <file>                         # optimized TAC (all passes, to a fixed point)
nova opt <file> --passes=fold,dce       # chosen passes, in the given order
nova opt <file> --show-all              # original TAC, TAC after every pass that changed it, then the result
```

## How it is tested (`tests/test_optimizer.cpp`)
- Positive and negative cases for every rewrite rule, reassignment, overflow, division by zero, `INT64_MIN / -1`.
- `print` preserved; unused pure assignments removed; failing divisions kept.
- Idempotence (re-optimizing changes nothing), a 500-statement dependency chain, and the iteration-cap path.
- **Differential corpus:** 4000 deterministic pseudo-random programs (fixed seed) run unoptimized and after
  (a) each pass alone, (b) the default pipeline, (c) the reversed pipeline. Printed output, runtime-error
  status, message and location must be identical, and the number of `print` instructions unchanged. About
  46% of the corpus ends in a runtime error, so error preservation is exercised heavily.

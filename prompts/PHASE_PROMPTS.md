# NOVA Agent Prompt Library

Copy the relevant prompt into your coding agent, alongside `AGENTS.md`, `CLAUDE.md`, and the phase specification. These prompts activate each phase and demand evidence-based completion; they do not replace repository instructions.

## Prompt 0 — Start or recover a session

```text
You are the engineering agent for NOVA. Read AGENTS.md, CLAUDE.md, all state files, and the active phase specification before changing anything. Inspect the actual repository, Git status, toolchain, code, tests, and prior evidence. Do not trust stale status claims.

Identify the earliest phase that is not truly complete. Extract its deliverables, mandatory tests, commands, and pass conditions. Do not implement later-phase features. Plan verification before editing. Implement one bounded task, run targeted tests, diagnose and fix failures, run regressions, inspect the diff, save actual evidence, update state, and report truthful status. Never claim a check passed unless it was run.
```

## Prompt 1 — V0.1 Foundation and CLI

```text
Implement V0.1 only. Read docs/phases/V0.1-foundation.md and repository instructions. Inspect the environment. Create a C++17/CMake scaffold, terminal executable, --help, --version, invalid-argument behavior, at least one real automated test integrated with CTest, and accurate build instructions. Create/update required state files. Do not advertise future commands as working.

Before coding, list acceptance criteria and verification commands. Run a clean configure/build, CTest, help/version smoke tests, and an invalid-command test. Diagnose failures without weakening tests. Save actual logs and exit codes under state/evidence/V0.1 where possible. Update verification, phase log, and roadmap status. Mark COMPLETE only if every mandatory condition passes; otherwise PARTIAL or BLOCKED. Do not begin V0.2.
```

## Prompt 2 — V0.2 Lexer

```text
Implement V0.2 only. Read docs/phases/V0.2-lexer.md. Verify V0.1 evidence and rerun relevant tests. Implement documented token kinds, source locations, lexer, diagnostics, and token-dump command. Test every token, keyword versus longer identifier, whitespace/newlines, EOF, invalid characters, and exact locations.

Run lexer tests, full regression tests, valid token-dump smoke test, and invalid-character diagnostic test. Inspect bounds and memory safety. Fix implementation defects, not tests merely to make them pass. Save evidence and update state. Mark COMPLETE only when every gate passes. Do not begin V0.3.
```

## Prompt 3 — V0.3 Parser and AST

```text
Implement V0.3 only. Read docs/phases/V0.3-parser-ast.md. Verify the lexer gate. Implement recursive-descent parsing and AST nodes for exactly the documented grammar, with explicit precedence/associativity, stable AST output, and source-located errors. Do not accept syntax without defined semantics.

Test every grammar production, precedence (2 + 3 * 4), associativity (8 - 3 - 1), parentheses, empty input, missing semicolon/operand, unmatched parentheses, unexpected EOF, and malformed input without crashes or hangs. Run targeted tests, full CTest, valid AST output, and invalid syntax smoke tests. Ensure grammar docs match behavior. Save evidence. Mark COMPLETE only if all gates pass; do not begin V0.4.
```

## Prompt 4 — V0.4 Semantic Analysis and TAC

```text
Implement V0.4 only. Read docs/phases/V0.4-semantics-tac.md. Verify the parser/AST gate. Implement symbol table, semantic checks, source-located diagnostics, deterministic TAC, and the documented CLI command. Define declaration, use-before-declaration, duplicate declaration, assignment, and division behavior explicitly.

Test valid programs, undeclared variables, duplicate declarations, unknown assignment targets, arithmetic TAC, deterministic temporary names, and division edge cases. Run targeted tests, full regressions, and valid/invalid CLI examples. Save evidence and update state. Mark COMPLETE only if all conditions pass; do not begin V0.5 otherwise.
```

## Prompt 5 — V0.5 Basic Optimizer

```text
Implement V0.5 only. Read docs/phases/V0.5-basic-optimizer.md. Verify V0.4. Implement constant folding, constant propagation, conservative algebraic simplification, and dead-code elimination for pure/unobservable assignments as independent passes. Document preconditions and semantic caveats.

Test positive and negative cases per pass, reassignment, division/overflow edge cases, preservation of print/observable effects, deterministic output, and termination/fixed-point behavior. Add differential tests comparing optimized and unoptimized execution on deterministic small programs. Run per-pass tests, differential tests, full CTest, and an end-to-end before/after example. Fix semantic bugs rather than weakening tests. Save evidence; mark COMPLETE only if every gate passes.
```

## Prompt 6 — V0.6 Benchmark Baselines

```text
Implement V0.6 only. Read docs/phases/V0.6-benchmark-baselines.md and the benchmark protocol. Verify prior gates. Add deterministic numerical C++ benchmarks, correctness oracles, GCC -O2/-O3 baselines, repeated timing, raw result storage, and environment metadata. Keep -Ofast separate with semantic caveats. Do not implement adaptive search yet.

Record OS, CPU, memory, compiler version, exact flags, source hash, input size, seed, repetitions, raw timings, median, spread, binary size, build status, and correctness status. Test serialization and deliberate incorrect-output detection. Validate correctness before timing, retain all samples, use multiple input sizes, and inspect variance. Never invent results or claim speedup from one run. Save manifests/logs; mark COMPLETE only if all mandatory conditions pass.
```

## Prompt 7 — V0.7 Bounded SmartPass

```text
Implement V0.7 only. Read docs/phases/V0.7-smartpass-bounded-search.md and benchmark protocol. Verify V0.6 evidence. Implement a whitelisted candidate space, baseline inclusion, safe process invocation using argument arrays, candidate lifecycle, build/correctness gates, repeated timing, ranking, result serialization, and CLI. Enforce the candidate budget. Do not concatenate untrusted input into shell commands.

Test valid candidates, invalid configurations, build failures, deliberately incorrect outputs, budget enforcement, deterministic sequence for a fixed seed, incomplete data, and no-valid-candidate behavior. Prove a correctness-failing candidate cannot win. Preserve failure and baseline records. Compare fixed and random search under equal budgets. Save evidence; mark COMPLETE only if every gate passes. Do not call the strategy adaptive yet.
```

## Prompt 8 — V0.8 Adaptive Search

```text
Implement V0.8 only. Read docs/phases/V0.8-adaptive-search.md. Verify bounded search and correctness gates. Implement a transparent adaptive policy using documented workload descriptors and prior outcomes to prioritize candidates under a fixed budget. Do not call it machine learning unless a learned model is actually implemented and evaluated.

Add deterministic feature extraction, seed handling, policy documentation, random-search baseline, ablation without the declared feature/history signal, and holdout input-size evaluation. Prevent holdout leakage. Compare methods under equal budgets and report tuning time. Evaluate at least two benchmark families, multiple sizes and multiple seeds where applicable. Include runtime, spread, binary size, correctness, and overhead. Preserve negative results. Mark COMPLETE only when required experiments and evidence exist.
```

## Prompt 9 — V0.9 Evaluation and Research Report

```text
Implement V0.9 only. Read docs/phases/V0.9-evaluation-report.md and docs/REFERENCES.md. Generate tables and plots only from saved raw data; every value must trace to a result file and manifest. Document research question, sourced problem framing, evidence versus inference, architecture, candidate space, budget accounting, environment, correctness, timing protocol, results, ablation, holdout evaluation, tuning overhead, failures, and threats to validity.

Verify citations support their claims. Do not claim universal speedups or that NOVA replaces GCC. Re-run report generation and validate missing-data handling. Provide clean-checkout reproduction instructions. Update state and mark COMPLETE only when values are traceable and reproduction gates pass.
```

## Prompt 10 — V1.0 Release Gate

```text
Perform the V1.0 release gate only. Read docs/phases/V1.0-release.md and repository instructions. Do not add features before the review. Start from a clean checkout/build where possible. Execute every checklist item and record PASS, FAIL, NOT_RUN, or BLOCKED with evidence. Run full tests, relevant sanitizer tests where supported, NOVA Script valid/invalid examples, optimizer differential tests, SmartPass budget/correctness tests, and the reproducible benchmark/report workflow.

Verify docs match implementation, planned commands are not presented as working, benchmark claims have raw data/metadata, references are traceable, and limitations are explicit. Resolve failures where possible; never waive mandatory checks silently. Mark RELEASED only if every mandatory gate passes; otherwise PARTIAL_RELEASE or BLOCKED with exact blockers.
```

## Prompt 11 — Resume after interruption

```text
Resume NOVA engineering from verified repository state. Read AGENTS.md, CLAUDE.md, all state files, and the active phase spec. Inspect actual files and Git status; do not trust memory over evidence. Identify the earliest phase that is not truly complete. Rerun checks if status/evidence is stale or inconsistent. Continue one bounded task at a time, follow retry caps, preserve user changes, never invent results, update state, and report exactly what was verified and what remains.
```

## Prompt 12 — Independent phase audit

```text
Act as an independent reviewer, not the implementer. Audit <PHASE_ID> against its phase specification, AGENTS.md, and CLAUDE.md. Inspect code, tests, docs, logs, and state. Run mandatory checks where possible. For every criterion, classify PASS, FAIL, NOT_RUN, or BLOCKED and cite the evidence path or command result. Look for missing/superficial/disabled/weakened tests, documented-but-unimplemented features, incorrect candidates that can win, missing raw benchmark samples, and unsupported research claims.

Do not modify code during the first audit. Return a severity-ranked gap list and verdict: COMPLETE supported, COMPLETE not supported, PARTIAL, or BLOCKED. If asked to repair gaps, fix one bounded gap at a time and rerun relevant gates.
```

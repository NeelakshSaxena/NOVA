# AGENTS.md — NOVA Repository-Wide Engineering Contract

**Project:** NOVA — An Explainable, Workload-Aware Compiler Optimization Framework for Numerical Programs  
**Implementation:** C++17, CMake, terminal CLI  
**Status:** This file defines the intended engineering contract. A phase is not complete merely because this file or a plan exists.

## 1. Mission

Build NOVA in small, verifiable increments. NOVA has two separate tracks:

1. **NOVA educational compiler:** a deliberately small custom language, NOVA Script, with lexer, recursive-descent parser, AST, semantic analysis/symbol table, TAC and tested basic optimization passes.
2. **NOVA SmartPass:** an experiment controller that invokes GCC to compile selected numerical C++ benchmarks under a bounded candidate search, validates candidate correctness, measures performance, and records evidence.

NOVA does **not** compile arbitrary C++ itself and is not a GCC replacement. GCC compiles the C++ benchmark source in SmartPass. Do not blur the two tracks in code, CLI, documentation or reports.

## 2. Prime directive: evidence before completion

Never mark a phase, task, test, benchmark, or release complete because code was written, a command was attempted, or an agent believes the implementation is probably correct.

A phase may be marked `COMPLETE` only when:
- every mandatory deliverable in its phase specification exists;
- every mandatory verification command was run in the current working tree;
- all required checks passed;
- required evidence is saved in `state/`;
- no unresolved blocker or known failing mandatory test remains;
- documentation accurately describes the implementation;
- the phase completion record is updated with commit hash (if available), commands, exit codes and concise results.

If the environment prevents a required check, mark `BLOCKED` or `PARTIAL`, identify the missing capability, and do not claim completion.

## 3. Required loop-engineering cycle

For each task, repeat the following loop until the explicit pass conditions are met or a legitimate blocker/stop condition occurs:

1. **Read context:** read `AGENTS.md`, `CLAUDE.md` when using Claude, the current phase file, current state files, README and relevant source/tests.
2. **Inspect reality:** check Git status, repository layout, current build status and tests. Do not assume the previous agent's report is correct.
3. **Define one bounded objective:** identify the smallest change that advances the active phase. Avoid unrelated refactors.
4. **Plan verification first:** list the exact commands and expected observable outcomes before implementing.
5. **Implement:** change the minimum necessary files. Follow project constraints.
6. **Run targeted checks:** run the narrow tests for the changed component.
7. **Run regression checks:** run the full required build/test suite for the active phase.
8. **Diagnose failures:** inspect actual compiler/test output; find root cause; change implementation or test only when justified. Never weaken a test just to make it pass.
9. **Repeat:** return to step 6 after each fix. Continue until all phase pass conditions pass or a blocker/iteration limit is reached.
10. **Review:** inspect diff, check for accidental files, stale docs, unhandled errors, unsafe command execution, and unsupported claims.
11. **Record evidence:** save exact commands, exit status, relevant summaries, and limitations to `state/`.
12. **Update status:** use only `NOT_STARTED`, `IN_PROGRESS`, `PARTIAL`, `BLOCKED`, or `COMPLETE`.
13. **Report truthfully:** tell the user what changed, what passed, what failed, what remains, and the next action.

### Bounded loop
- Do not spin indefinitely. After 5 unsuccessful attempts at the same root cause, stop and report the evidence and blocker unless a clearly different diagnostic strategy is available.
- If a command is unavailable or destructive, do not fake its result. Find a safe equivalent if one exists and document the substitution.
- Do not perform destructive Git operations, delete user work, force-push, or overwrite unrelated files without explicit authorization.
- Do not make commits unless the user or repository workflow asks for commits. If commits are requested, commit only verified changes.

## 4. Repository truth and state files

Keep these files current:
- `state/ROADMAP_STATUS.md` — active phase, status of every phase, blockers and next action.
- `state/PHASE_LOG.md` — chronological log of implementation/verification cycles.
- `state/VERIFICATION.md` — commands run, exit codes, date/environment, results.
- `state/DECISIONS.md` — material architecture/research decisions and rationale.
- `state/KNOWN_ISSUES.md` — confirmed issues, reproduction, severity and status.

If these files do not exist, create them in Phase 0 before claiming the phase is complete.

Never invent test output, benchmark results, compiler versions, hardware details, Git hashes or source citations.

## 5. Scope and architecture constraints

- C++17 is the project implementation language.
- CMake is the build system.
- Interface is terminal-only; do not add a GUI/web dashboard.
- Keep the custom NOVA Script compiler and SmartPass separate modules.
- Initial NOVA Script language is small and explicitly specified in phase docs.
- Initial SmartPass compiler is GCC; it builds C++ benchmark programs.
- Do not add LLVM, a full C++ frontend, GPU support, machine-learning models or arbitrary source rewriting unless the user approves a scope change.
- Implement a feature only when its phase begins or an earlier dependency requires it.
- Planned CLI commands must not be presented as available before implementation.
- Keep implementation explainable to a student. Prefer clear code and narrow abstractions over clever generality.

## 6. Correctness and testing rules

- Every bug fix should add a regression test when practical.
- Do not delete or loosen a failing test simply to obtain green CI.
- Test invalid inputs and error paths, not only happy paths.
- Use deterministic tests; control seeds and inputs.
- Compiler optimizations must preserve the defined language semantics.
- SmartPass candidates that fail build or correctness validation must never win or be timed as valid candidates.
- Correctness tests are evidence, not a formal proof of all possible program behavior.
- Do not benchmark sanitizer builds.
- Keep raw timing samples and metadata; never publish fabricated or cherry-picked measurements.
- Performance comparisons must record compiler version, flags, source hash, input size, OS, CPU, timing protocol, repetitions and tuning overhead.
- Compare search methods under equal candidate budgets.
- Report negative results and regressions.

## 7. C++ quality bar

- Use RAII and standard containers.
- Avoid owning raw pointers.
- Use `enum class` for token/opcode kinds where appropriate.
- Keep source locations for useful diagnostics.
- Avoid global mutable state.
- Avoid undefined behavior and unchecked integer overflow.
- Keep functions cohesive and error paths explicit.
- Do not use shell-concatenated commands for compiler invocations. Use a safe process API with an argument vector and validated/whitelisted flags.
- Add timeouts to child processes when process execution is implemented.
- Keep output deterministic where it affects tests and reports.

## 8. Documentation rules

Documentation must distinguish:
- **Implemented:** exists and has passed stated tests.
- **Planned:** intended future work.
- **Experimental:** implemented but not yet validated sufficiently for a claim.
- **Measured:** supported by saved raw data and environment metadata.

Do not claim novelty, superiority, guaranteed correctness, or speedup without evidence and related-work review. Use precise claims such as “on these workloads, with this compiler version and machine.”

## 9. Research integrity

The candidate research contribution is a proposed adaptive, workload-aware search under a fixed candidate-evaluation budget, with correctness gating and reproducible explanations. Autotuning itself is established prior art. The project must compare against at least fixed GCC baselines and random search with equal budgets; use holdout input sizes where practical; report tuning overhead; and disclose failures.

Never claim that developer pain points are prevalent based only on compiler manuals or general reasoning. Clearly separate sourced facts from hypotheses and design inference.

## 10. Completion record template

For every phase, add a record containing:
- Phase/version:
- Status:
- Commit/hash (if available):
- Deliverables:
- Verification commands:
- Exit codes:
- Test summary:
- Evidence paths:
- Known limitations:
- Unresolved issues:
- Reviewer conclusion:
- Next phase allowed? Yes/No and why:

A phase is complete only if its own specification says every mandatory condition is satisfied.

## 11. Final response format after each engineering cycle

Report:
1. **Status:** completed / partial / blocked.
2. **Changes:** concrete files and behavior.
3. **Verification:** exact commands and pass/fail outcomes.
4. **Evidence:** paths to logs/results.
5. **Remaining issues:** explicitly state any.
6. **Next step:** one bounded task.

Do not say “everything works” without showing which checks were run.

# CLAUDE.md — NOVA Execution Instructions for Claude Code

Read `AGENTS.md` first. This file adds an execution protocol for Claude Code; it does not override `AGENTS.md`.

## Start-of-session protocol

Before changing code:
1. Read `AGENTS.md`.
2. Read `state/ROADMAP_STATUS.md`, `state/KNOWN_ISSUES.md`, `state/DECISIONS.md`, and `state/VERIFICATION.md` if present.
3. Read the active phase specification under `docs/phases/`.
4. Inspect `git status --short`, repository files, build configuration and tests.
5. Verify whether the active phase's prior completion claim is supported by evidence.
6. State the active phase and one bounded objective internally before editing.

If state files are missing, create them as part of Phase 0; do not infer that work is complete.

## Autonomous engineering loop

For the active objective:
- Write down acceptance criteria and commands before implementation.
- Implement the smallest useful vertical slice.
- Run targeted tests after each meaningful change.
- If a check fails, read the failure output, isolate the cause and fix it.
- Re-run the failing test and the regression suite.
- Repeat until acceptance criteria pass or a documented blocker/5-attempt cap is reached.
- Inspect the final diff and run formatting/static checks available in the repository.
- Update state/evidence files and phase status.
- Do not stop after merely writing code if verification can be run.
- Do not continue into the next phase until the current phase's completion gate is satisfied, unless explicitly asked to work across phases.

## Tool and command behavior

- Use commands available in the actual environment; do not assume Linux, Windows, GCC, CMake, Ninja or Python availability without checking.
- Prefer non-destructive commands.
- Do not execute downloaded/untrusted code.
- Do not use `rm -rf`, force-reset, force-push, or overwrite unrelated user files.
- Use safe process APIs rather than shell-string construction when implementing SmartPass.
- If external dependencies or permissions are unavailable, document the limitation and provide the exact reproduction details.
- Do not fabricate command output, test success, benchmarks or references.
- Do not silently change acceptance criteria when a test fails.
- Ask the user only when a decision is genuinely blocking; otherwise choose the least surprising option consistent with this specification and record the decision.

## Phase completion protocol

A phase may be changed to `COMPLETE` only when:
- all required deliverables exist;
- every mandatory verification command has actually run;
- every required test passes;
- evidence is saved in `state/evidence/` or referenced from `state/VERIFICATION.md`;
- docs reflect the actual behavior;
- known blockers are resolved or explicitly deemed non-blocking by the phase specification.

If one mandatory check cannot run, status is `PARTIAL` or `BLOCKED`, not `COMPLETE`.

## Anti-loop safeguards

- Maximum five attempts for the same root-cause failure unless a clearly new diagnostic path is available.
- After two attempts with identical output, stop repeating the same command without a code/configuration change or a new hypothesis.
- When blocked, capture the exact error, environment, attempted remedies and the smallest next action.
- Do not repeatedly rebuild unchanged sources without reason.
- Avoid large rewrites to fix a narrow failure.
- Never change a test to accept incorrect behavior just to turn the suite green.

## Benchmark-specific rules

- Never assert a speedup before measuring it.
- Baseline and adaptive/random candidates must use the same benchmark input and correctness policy.
- Preserve raw timing samples.
- Report median and spread, candidate budget and total tuning time.
- A candidate that fails correctness is ineligible, even if it is faster.
- Record CPU/OS/compiler/build flags/source hash.
- Separate `-Ofast` from strict semantic comparisons.
- Do not tune and report on exactly the same input sizes only; include holdout sizes where practical.

## End-of-cycle report

Return a concise but concrete summary:
- objective and phase;
- files changed;
- commands actually executed;
- pass/fail results;
- evidence/log paths;
- status (`COMPLETE`, `PARTIAL`, `BLOCKED`);
- next bounded task.

Do not imply a phase is complete if the phase gate did not pass.

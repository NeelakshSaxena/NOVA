# Phase Execution Protocol — Autonomous Loop Until Verified

This protocol is used together with `AGENTS.md`, `CLAUDE.md`, and the selected phase specification.

## Before work
- Confirm active phase from `state/ROADMAP_STATUS.md`.
- Verify actual repository state; do not trust stale status.
- Read the entire active phase spec.
- Extract all deliverables, tests, commands and pass conditions into a checklist.
- Identify dependencies and check that earlier phase gates are complete.
- Write the intended verification commands before editing.

## During work
For each bounded implementation change:
1. Implement.
2. Run the narrowest relevant test.
3. If it fails, capture the full useful error and identify root cause.
4. Fix the implementation or correct a demonstrably incorrect test expectation.
5. Rerun the failed test.
6. Rerun all regression tests required by the active phase.
7. Inspect diff and check for unintended behavior.
8. Repeat until pass or blocked.

Do not proceed to unrelated features while the current objective is failing. Do not keep repeating an unchanged failing command.

## Completion decision
For every pass condition, assign exactly one:
- `PASS` — command/test was run and evidence supports it.
- `FAIL` — run evidence shows it does not pass.
- `NOT_RUN` — it has not been executed.
- `BLOCKED` — execution is prevented by an identified environment or dependency issue.

Only all mandatory conditions marked `PASS` permits `COMPLETE`.

`NOT_RUN` and `BLOCKED` cannot be treated as passing. If the phase is blocked, report the smallest next action needed to unblock it.

## Evidence record
Save under `state/evidence/<phase-id>/` where possible:
- `environment.txt`
- `configure.log`
- `build.log`
- `tests.log`
- `cli-smoke.log`
- `phase-checklist.md`
- benchmark raw data for benchmark phases

Evidence may contain only actual command output. Redact secrets, tokens and personal information.

## Phase checklist template
```markdown
# Phase Gate — <phase-id>
- [ ] Deliverable A — evidence:
- [ ] Deliverable B — evidence:
- [ ] Test A — command / exit code / evidence:
- [ ] Test B — command / exit code / evidence:
- [ ] Full regression suite — command / exit code / evidence:
- [ ] Documentation reflects implementation
- [ ] Diff reviewed
- [ ] Known issues reviewed

Decision: NOT_STARTED | IN_PROGRESS | PARTIAL | BLOCKED | COMPLETE
Reviewer notes:
```

## Retry and escalation
- After two identical failures with no change in hypothesis or inputs, do not repeat the same attempt.
- Try a different diagnostic path.
- Stop after five attempts on the same root cause unless a genuinely new path is available.
- On block, write a concise issue report with exact error, environment, attempted fixes and next action.
- Ask the user only for decisions that cannot be resolved from the repo/specification; make and record reasonable reversible choices otherwise.

## After phase completion
- Update `state/ROADMAP_STATUS.md`.
- Append to `state/PHASE_LOG.md`.
- Add actual commands/results to `state/VERIFICATION.md`.
- Update `state/KNOWN_ISSUES.md`.
- Record architecture/research changes in `state/DECISIONS.md`.
- Only then begin the next phase.

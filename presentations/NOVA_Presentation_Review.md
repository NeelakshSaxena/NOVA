# NOVA presentation: review report (revision 2)

Deck: `NOVA_Project_Presentation.pptx` (15 slides, 16:9, editable text, tables, shapes, one native chart; speaker notes with viva questions on every slide). Built by `NOVA_Presentation_Source.py`; text and notes are dumped to `NOVA_Slide_Content.md`; PDF preview: `NOVA_Project_Presentation.pdf`. Project source code was not modified.

## 1. What changed

Slides 5 to 9, 11, 12 and 15 are new or rewritten; slides 2 to 4 and 13 are kept (slide 3 gained an objectives line). To stay at 15 slides, the old Objectives, Existing-approach, SmartPass-workflow, Research-contribution and Testing slides were merged into the new ones (the evaluation chart now sits on "What NOVA Adds"; the workflow is covered by slides 5 and 7). The title slide keeps your edit (only "Course: Compiler Design"; no placeholders). Your root-level `NOVA_Project_Presentation.pptx` was not overwritten.

| Slide | Content |
|---|---|
| 5 | How NOVA SmartPass Chooses Compiler Flags: 6-step flow, five signals with real status, loop-depth example |
| 6 | Candidates, budget and accounting: illustrative A to D table, what uses the budget, selected vs compiled vs valid |
| 7 | Candidate lifecycle flowchart with failure branches and the anchor |
| 8 | Numerical checks: five checks, real tolerance rule, worked example |
| 9 | Faster code vs strict floating point (`-ffast-math`) |
| 10 | Running NOVA (A): compiler, real DEMO.md output |
| 11 | Running NOVA (B): SmartPass commands, saved real B = 4 comparison |
| 12 | What NOVA Adds (statement as worded in your brief; labelled "proposed contribution" next to "demonstrated so far") |
| 15 | Glossary |

## 2. Where the brief's premises differ from the code (please confirm)

1. **"Unaccounted anchor" is not defined anywhere** in the code or docs. Closest: "uncounted anchor baselines" (docs/NOVA_NotebookLM_Master_Source.md, line 139): the -O2 and -O3 baselines, always built and not counted against B. In the code, "anchor" means the plain -O2 baseline. The glossary says this and flags the term for you; no definition was invented.
2. **Reference**: it is an independent long-double calculation inside the harness, not a GCC-built reference executable.
3. **Weighted checksum** is `Σ ((i mod 7) + 1) · v[i]`, not `(i + 1) · x_i`.
4. **Tolerance** is mixed: `|actual − expected| ≤ 1e-9 · max(1, |expected|)` (absolute near zero, relative for large values), not purely absolute. The five checks are: sum, v[0], v[len/2], v[len−1], wsum.
5. **No whole-array comparison**, and no special empty-output rule (length must equal the expected length). NaN or infinity fails. Stated on slide 8.
6. **Signals**: source features are registered per kernel (not parsed from C++); loop depth and FLOPs/byte are recorded but NOT used in scoring; there is no bandwidth model (only working set above 4 MiB is used); history is implemented and optional. Slide 5 labels each one.
7. **Budget accounting** (matches docs/smartpass.md and tuner.cpp): B searched candidates are counted when built, failures included, no retries; the two baselines are extra (B = 4 gives 6 builds); invalid flags are refused before any build. B is configurable (0 to 100000); 4 is the demonstration setting.
8. **No saved adaptive B = 4 run exists.** The comparison table on slide 11 is the saved random-strategy run `V0.7-stencil-random-s1-B4` (labelled so). The adaptive command shown is the supported syntax from `nova tune` usage. The A to D table uses real flag IDs but is labelled illustrative.
9. `-ffast-math` is not in the whitelist (refused). `-Ofast` ran only in the separate V0.6 disclosure experiment and passed the five-check gate on all three kernels, which slides 8 and 9 use to show the gate is lightweight.
10. Per your instruction, the deck has no novelty discussion. Note that the project's own docs/03 says novelty is not established, so the deck uses "proposed contribution" and makes no novelty claim.
11. README / RELEASE still say "not a git repository"; the folder now has a `.git`.

## 3. Validation

- Commands checked against `tune_usage()` in `src/tune/cli.cpp` and the captured output in `docs/DEMO.md` (`nova tac`, `run`, `opt --show-all`; build commands from README).
- Flag whitelist, 22+2 space, 2.0x binary-size policy, anchor, lifecycle states, 4 MiB threshold and history weights checked in `src/tune/` and `src/bench/`.
- Numbers on slide 11 copied from the saved explain log; chart values parsed from `report/report.md`.
- Illustrative arithmetic recomputed: `(1e16 + −1e16) + 1.0 = 1.0` vs `1e16 + (−1e16 + 1.0) = 0.0`; worked-example differences and allowances (4.0e-10 vs 1.05e-6 pass; 2.1e-9 vs 1.0e-9 fail).
- Rendered with LibreOffice and inspected every slide; fixed title wrapping, box overflows, a nested-markup glitch and overlapping footers. `validate.py` passed; notes present on all 15 slides.

Not done: not opened in PowerPoint; the project's tests and benchmarks were not re-run (the device shell is Linux, the project binaries are Windows .exe). The saved baseline compile line on slide 11 is shown abbreviated ("…"), because the exact full command line was not reproduced from a log.

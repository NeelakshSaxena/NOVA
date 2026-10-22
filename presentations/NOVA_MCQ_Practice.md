# NOVA: MCQ Practice

**Q1.** In SmartPass, what compiles the C++ kernels?
a) NOVA's own backend  b) GCC (g++)  c) LLVM  d) The NOVA interpreter

**A1.** b. GCC compiles everything. SmartPass only picks flags, checks, times and records.

**Q2.** NOVA Script supports:
a) Loops and branches  b) Floats and arrays  c) 64-bit integers, `let`, `print`, `+ - * /`  d) Functions

**A2.** c. There are no loops or branches, and no machine-code backend.

**Q3.** In three-address code (TAC), each instruction has:
a) At most one operator  b) Exactly three operands  c) At most two variables  d) No temporaries

**A3.** a. For example `$t1 = x * 2`.

**Q4.** The optimizer's four passes are:
a) Inline, unroll, vectorize, dce  b) Fold, propagate, simplify, dce  c) Lex, parse, fold, emit  d) Fold, inline, simplify, schedule

**A4.** b. They repeat until a fixed point (3 iterations in the demo).

**Q5.** How does the optimizer treat `10 / 0`?
a) Folds it to 0  b) Removes it as dead code  c) Keeps it so the runtime error still happens  d) Rejects the program

**A5.** c. The optimizer never hides a runtime error.

**Q6.** The adaptive strategy is:
a) A trained neural network  b) A reinforcement-learning agent  c) A hand-written scoring heuristic  d) Exhaustive search

**A6.** c. Score = prior + history + pilot feedback. It is not machine learning.

**Q7.** With budget B = 4, how many builds happen in total?
a) 4  b) 5  c) 6  d) 22

**A7.** c. 4 searched candidates plus the 2 baselines (`-O2`, `-O3`), which don't use the budget.

**Q8.** A candidate whose build fails:
a) Is retried once  b) Does not count against the budget  c) Counts against the budget, with no retries  d) Aborts the whole run

**A8.** c. Failures and wrong answers still count, and nothing is retried.

**Q9.** What is the "anchor" in the code?
a) The fastest candidate  b) The plain `-O2` baseline  c) The `-O3` baseline  d) The first searched candidate

**A9.** b. It is the reference for pilot runs, history log-gains and the binary-size limit. Trap: "unaccounted anchor" is not defined anywhere in the project.

**Q10.** The correctness tolerance is:
a) `|a−e| ≤ 1e-9` (absolute only)  b) `|a−e| ≤ 1e-9·|e|` (relative only)  c) `|a−e| ≤ 1e-9·max(1,|e|)`  d) `|a−e| ≤ 1e-6`

**A10.** c. It is mixed: absolute near zero, relative for large values. Trap: it is not purely absolute.

**Q11.** The reference values come from:
a) A `-O0` GCC build  b) An independent long-double calculation in the harness  c) The previous run's output  d) The `-O2` binary

**A11.** b. No reference executable is built.

**Q12.** The weighted checksum `wsum` is:
a) Σ(i+1)·v[i]  b) Σ((i mod 7)+1)·v[i]  c) Σ i·v[i]  d) Σ v[i]²

**A12.** b. The weights cycle 1 to 7, not (i+1).

**Q13.** The five numerical checks are:
a) sum, min, max, mean, median  b) sum, first, middle, last, weighted checksum  c) Every element  d) Sum and variance

**A13.** b. They are sum, v[0], v[len/2], v[len−1] and wsum. There is no whole-array comparison.

**Q14.** A NaN in the output:
a) Passes if the sum matches  b) Is ignored  c) Fails the check  d) Is replaced by 0

**A14.** c. NaN and infinity fail.

**Q15.** Is `-ffast-math` in the SmartPass whitelist?
a) Yes  b) Only with `-O3`  c) No, it is refused  d) Only for matmul

**A15.** c. `-O2 -ffast-math` is refused with the flag named. The spelling is `-ffast-math`, not "-fastmath".

**Q16.** `-Ofast` passed the five-check gate in the V0.6 run. What does that show?
a) `-Ofast` is safe for aerospace  b) The gate is lightweight, not a proof of equivalence  c) The gate is broken  d) Fast-math never changes results

**A16.** b. Passing the gate does not make a result safe for high-stakes use.

**Q17.** The timing protocol is:
a) 1 run, best time  b) 2 warm-ups + 15 interleaved rounds, median and IQR  c) 100 runs, mean  d) 5 runs, minimum

**A17.** b. The CPU is pinned and raw samples are saved.

**Q18.** Which of these is used in the adaptive score today?
a) Loop depth  b) Arithmetic intensity  c) Whether the loop streams, has a reduction, and a working set above 4 MiB  d) Measured memory bandwidth

**A18.** c. Loop depth and arithmetic intensity are recorded but not scored. There is no bandwidth model.

**Q19.** A candidate whose binary is larger than 2.0x the `-O2` binary:
a) Wins automatically  b) Is marked REJECTED_BY_POLICY  c) Is retried  d) Is a build failure

**A19.** b. The policy limit is 2.0x the `-O2` binary.

**Q20.** The replay study found that adaptive vs random:
a) Adaptive is much faster at every budget  b) Random wins  c) Adaptive is slightly ahead at small budgets, and they tie at B = 22  d) No difference at any budget

**A20.** c. The gap is small and shrinks as B grows.

---

**Likely extras:** the search space is 22 candidates plus 2 baselines. Holdout sizes are never used for choosing, and tuning on one is refused. Fixed flags `-std=c++17 -msse2 -mfpmath=sse` exist because the 32-bit MinGW compiler would otherwise use x87. `nova tune` exits 0 only if an eligible winner exists. Tests pass 49/49 in the V1.0 log. The history file stores log(`-O2` median / candidate median); similarity weight is exp(−|Δlog2 n|/4) for the same benchmark and 0.25 for others.

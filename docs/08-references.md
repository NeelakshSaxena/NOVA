# 08 — References and Source Register

This source register separates official documentation, research papers and methodology references. Links were checked during preparation of this design pack; check current versions before final submission because online documentation can change.

## R1. GCC optimization options — official documentation
GNU Project, *Options That Control Optimization*.
https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html

**Used for:** what `-O2`, `-O3`, `-Os`, `-Ofast` and profile-guided options do; trade-offs and semantic caveats. The exact enabled passes depend on GCC version and target, so always record `g++ --version`.

## R2. OpenTuner — autotuning research
Jason Ansel, Shoaib Kamil, Kalyan Veeramachaneni, Jonathan Ragan-Kelley, Jeffrey Bosboom, Una-May O'Reilly, Saman Amarasinghe. “OpenTuner: An Extensible Framework for Program Autotuning.” PACT 2014.
DOI: https://doi.org/10.1145/2628071.2628092  
Project: https://opentuner.org/  
Paper: https://commit.csail.mit.edu/papers/2014/ansel-pact14-opentuner.pdf

**Used for:** evidence that program autotuning is established work, search-space representation matters, and no one search strategy works best across every problem. This is prior art; NOVA must not claim to invent autotuning.

## R3. Google Benchmark — measurement methodology
Google Benchmark, *User Guide*.
https://github.com/google/benchmark/blob/main/docs/user_guide.md  
Random interleaving guidance: https://github.com/google/benchmark/blob/main/docs/random_interleaving.md

**Used for:** repetition controls, timing practices and randomized interleaving to reduce run-to-run effects. The project may use its own simple harness initially; if so, implement and test equivalent measurement discipline rather than claiming to use Google Benchmark.

## R4. SPEC CPU 2026 run and reporting rules — benchmark methodology
Standard Performance Evaluation Corporation (SPEC), *SPEC CPU 2026 Run and Reporting Rules*.
https://www.spec.org/cpu2026/docs/runrules.html

**Used for:** principles of repeatable performance observation, output validation, multiple runs, and documented results. NOVA's custom benchmarks are not SPEC benchmarks and are not SPEC-certified.

## R5. Souper — synthesis-based compiler optimization
Raimondas Sasnauskas, Yang Chen, Peter Collingbourne, Jeroen Ketema, Gratian Lup, Taneja, John Regehr. “Souper: A Synthesizing Superoptimizer.” arXiv:1711.04422, 2017.
https://arxiv.org/abs/1711.04422

**Used for:** related work on discovering optimizations in LLVM IR using synthesis. NOVA does not implement Souper or synthesis in its initial design.

## R6. LLVM Kaleidoscope — compiler construction tutorial
LLVM Project, *My First Language Frontend with LLVM*.
https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/

**Used for:** staged compiler construction concepts: lexer, parser/AST, IR generation, optimizer and JIT. NOVA's initial implementation intentionally avoids LLVM to keep the project smaller and easier to explain.

## R7. egg — equality saturation
Max Willsey, Chandrakana Nandi, Yisu Remy Wang, Oliver Flatt, Zachary Tatlock, Pavel Panchekha. “egg: Fast and Extensible Equality Saturation.” arXiv:2004.03082, 2020.
https://arxiv.org/abs/2004.03082  
Journal publication (2026): https://doi.org/10.1145/3815481

**Used for:** related work on e-graphs and equality saturation for rewrite-driven optimization. This is future/related work only; NOVA's initial optimizer uses straightforward local TAC passes, not e-graphs.

## R8. Course materials supplied by the student
- Compiler Design theory syllabus file shared in the conversation (`turn0file0` in the original conversation context).
- Compiler Design Lab syllabus file shared in the conversation (`turn0file1` in the original conversation context).

**Used for:** aligning the educational compiler track with the course's compiler phases and lab topics (lexer, parser, AST/IR and code-generation concepts). These are course-provided materials, not public web references. Before final submission, add the official course title/version or instructor-provided citation format if required.

## 9. Reference-to-claim map

| Claim or design choice | Primary reference |
|---|---|
| GCC optimization levels have distinct behavior/trade-offs | R1 |
| Search spaces and domain-specific autotuning are established research problems | R2 |
| Repetitions and interleaving help performance measurement | R3 |
| Validate outputs and document repeatable benchmark runs | R4 |
| Synthesis-based optimization is existing prior art | R5 |
| Lexer/parser/AST/IR are standard compiler-construction stages | R6 |
| Equality saturation is established related work | R7 |
| Course coverage alignment | R8 |

## 10. Citation and academic integrity policy

- Cite sources for technical claims and related work.
- Do not present design hypotheses as published facts.
- Do not claim that a source proves a problem is common among all developers unless it contains evidence for that prevalence.
- Do not claim novelty until a broader literature review checks adjacent work.
- Do not copy paper abstracts or large text passages into the report; paraphrase and cite.
- Record source access dates if the final report format requires them.

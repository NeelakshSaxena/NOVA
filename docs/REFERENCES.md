# References and Evidence Boundaries

1. GCC, “Options That Control Optimization,” official manual: https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html
2. Ansel et al., “OpenTuner: An Extensible Framework for Program Autotuning,” PACT 2014. DOI: https://doi.org/10.1145/2628071.2628092 ; project: https://opentuner.org/
3. Google Benchmark user guide: https://github.com/google/benchmark/blob/main/docs/user_guide.md
4. Google Benchmark random interleaving: https://github.com/google/benchmark/blob/main/docs/random_interleaving.md
5. SPEC CPU 2026 run rules: https://www.spec.org/cpu2026/docs/runrules.html
6. Sasnauskas et al., “Souper: A Synthesizing Superoptimizer,” arXiv:1711.04422: https://arxiv.org/abs/1711.04422
7. LLVM, “My First Language Frontend with LLVM”: https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/
8. Willsey et al., “egg: Fast and Extensible Equality Saturation,” arXiv:2004.03082: https://arxiv.org/abs/2004.03082

## How to use these sources
- GCC supports claims about GCC's documented optimization flags and caveats.
- OpenTuner supports the statement that program autotuning is prior art and search-space design matters.
- Google Benchmark/SPEC support measurement protocol principles.
- Souper and egg are related work, not NOVA dependencies or implemented algorithms.
- LLVM Kaleidoscope supports the staged educational compiler design.
- These sources do not by themselves establish the prevalence of developer pain points. Use empirical studies or a properly conducted survey for prevalence claims.
- Recheck URLs and publication metadata before final academic submission.

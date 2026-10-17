# Reference check (performed 2026-10-15 with WebFetch; pages as served that day)

| # | Source | What was checked | Result |
|---|---|---|---|
| 1 | GCC manual, Optimize Options (current + GCC 6.3.0 version) | documents -O2/-O3/-Ofast, -ftree-vectorize, -fprefetch-loop-arrays, -funroll-loops; -Ofast "enables optimizations that are not valid for all standard-compliant programs"; the set enabled per -O level depends on target/configuration | SUPPORTED. GCC 6.3.0 manual: -ftree-vectorize "enables -ftree-loop-vectorize and -ftree-slp-vectorize"; -O3 turns on both of those, so `-ftree-vectorize` adds nothing at -O3 (docs/smartpass.md reworded accordingly). -funroll-loops: "makes code larger, and may or may not make it run faster". -fprefetch-loop-arrays: "improve the performance of loops that access large arrays". |
| 2 | OpenTuner (opentuner.org; paper DOI 10.1145/2628071.2628092) | framework for program autotuners, ensembles of search techniques, customizable configuration representations | PARTIALLY VERIFIED. opentuner.org confirms the framework claims quoted above. The ACM page returned HTTP 403 and the PDF could not be text-extracted here, so the PACT 2014 venue/year/authors rest on docs/08-references.md (not re-verified in this session). |
| 3 | Google Benchmark user guide | not fetched separately | NOT RE-VERIFIED (only cited for "repetitions are supported"; the report does not rely on it). |
| 4 | Google Benchmark random interleaving (raw.githubusercontent.com/.../random_interleaving.md) | describes randomly interleaving repetitions and says it lowers run-to-run variance (the page quotes 40% on average for their data) | SUPPORTED for "interleaving repetitions is a documented technique to reduce variance". NOVA does not reproduce their figure. |
| 5 | SPEC CPU 2026 run rules | repeatability, output validation, reporting median of three runs | SUPPORTED as a measurement-practice reference (validation of outputs; median of repeated runs). NOVA's benchmarks are not SPEC-conformant. |
| 6 | Souper (arXiv 1711.04422) | title/authors/summary | SUPPORTED: "Souper: A Synthesizing Superoptimizer", Sasnauskas et al.; related work only. |
| 7 | LLVM Kaleidoscope tutorial | scope | SUPPORTED: tutorial building a compiler (lexer, parser, AST, codegen) for a small language; cited only for the staged educational design. |
| 8 | egg (arXiv 2004.03082) | title/authors/summary | SUPPORTED: "egg: Fast and Extensible Equality Saturation", Willsey et al.; related work only. |

Not checked: publication metadata of #2 beyond the site; page content may change after the check date. docs/08-references.md also lists a 2026 journal DOI for egg, which was not opened.

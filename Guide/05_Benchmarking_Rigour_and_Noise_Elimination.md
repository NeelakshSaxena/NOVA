# 05 — Benchmarking Rigour, Timing Protocols & Noise Elimination

> **Target Platform:** Google NotebookLM Source Document  
> **Source Files Covered:** `src/bench/harness.cpp`, `src/bench/stats.cpp`, `benchmarks/kernels/common.hpp`  

---

## 1. The Challenge of Benchmarking Modern Hardware
Accurate compiler benchmarking is notoriously difficult due to:
* **OS Preemption:** Background services interrupt kernel execution.
* **Thermal Throttling:** Running Candidate A first when the CPU is cold and Candidate B second when the CPU is hot creates severe thermal bias.
* **Hybrid CPU Architectures:** On modern processors like the Intel Core Ultra 7 155H, threads jumping between Performance cores (P-cores) and Efficient cores (E-cores) cause timing variations up to 25% on identical binaries.

---

## 2. NOVA's 5-Pillar Timing Protocol

### Pillar 1: High-Resolution In-Process Timing
* Implemented in `benchmarks/kernels/common.hpp` and `src/bench/harness.cpp`.
* Uses native high-frequency hardware counters:
  * On Windows: `QueryPerformanceCounter` via `std::chrono::steady_clock`.
  * Timers achieve sub-microsecond resolution.

### Pillar 2: Region-of-Interest (ROI) Isolation
* Benchmarks **never** time process startup, dynamic linker loading, memory allocation (`malloc`/`new`), or buffer initialization.
* The timer starts immediately before the numerical loop and stops immediately after the loop completes:
  ```cpp
  auto t0 = std::chrono::steady_clock::now();
  kernel_compute(A, B, C, N);
  auto t1 = std::chrono::steady_clock::now();
  double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  ```

### Pillar 3: CPU Core Affinity Pinning & Process Priority
* **The Problem:** Windows thread schedulers randomly migrate threads across 22 logical cores, mixing P-cores and E-cores. Unpinned measurements had an Interquartile Range / Median ratio of ~25%.
* **The Solution:** SmartPass enforces core affinity via `--affinity=10` (hexadecimal mask `0x10` = bit 4 = logical core 4) and sets process priority class to `HIGH_PRIORITY_CLASS`.
* **The Result:** Pinned measurements achieve razor-thin IQR spreads ($\le 1\%$), yielding stable, reproducible measurements.

### Pillar 4: Interleaved Measurement Rounds
* Rather than running Candidate 1 fifteen times and then Candidate 2 fifteen times:
  * SmartPass executes in **rounds**.
  * In round 1, every candidate runs once in a randomized order.
  * In round 2, candidates run again in a freshly shuffled order using a seeded Fisher-Yates shuffle.
* This interleaving evenly distributes thermal and background noise across all candidates.

### Pillar 5: Median and IQR (Rejecting Mean and Standard Deviation)
* Execution time distributions have a hard physical floor (the fastest possible hardware execution) with an asymmetric tail of positive outliers caused by OS interruptions.
* **Why Mean and StdDev are wrong:** A single OS page fault or interrupt skews the arithmetic mean and inflates the standard deviation.
* **Why Median and IQR are correct:** The **Median** represents the true central tendency of the kernel. The **Interquartile Range (IQR = $Q_3 - Q_1$)** measures the spread of the middle 50% of runs, completely discarding outlier spikes.

---

## 3. Mathematical Formulas Used in NOVA

### Relative Oracle Tolerance:
$$\text{RelDiff} = \frac{|\text{actual} - \text{expected}|}{\max(1.0, |\text{expected}|)} \le 10^{-9}$$

### Speedup:
$$\text{Speedup} = \frac{\text{Baseline Median Time}}{\text{Candidate Median Time}}$$
* $\text{Speedup} > 1.0$ indicates faster execution.

### Percent Runtime Reduction:
$$\text{Runtime Reduction (\%)} = 100 \times \frac{\text{Baseline Median} - \text{Candidate Median}}{\text{Baseline Median}}$$

---

## 4. Rule: Never Benchmark Sanitizer Builds
AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan) inject shadow memory checks around every memory access, inflating runtime by $2\times$ to $5\times$. Sanitizers must only be used during correctness testing; they are **strictly forbidden** during performance benchmarking.

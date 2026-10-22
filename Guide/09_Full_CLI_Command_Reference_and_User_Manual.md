# 09 — Full CLI Command Reference & User Manual

> **Target Platform:** Google NotebookLM Source Document  
> **Executable:** `build/nova.exe` (or `nova` on PATH)  
> **Exit Codes:** `0` Success | `1` Input / Runtime Error | `2` CLI Usage Error  

---

## 1. Track 1: NOVA Script Commands

### 1.1 `nova tokens <file>`
* **Purpose:** Scans the `.nova` source file and emits the token stream with `line:column` coordinates.
* **Exit Codes:** `0` on success; `1` if the file has lexical errors (e.g. invalid character `@`) or cannot be read; `2` if arguments are missing.
* **Example:**
  ```powershell
  .\build\nova.exe tokens examples\basic.nova
  ```
* **Output:**
  ```text
  1:1 KW_LET 'let'
  1:5 IDENTIFIER 'x'
  1:7 ASSIGN '='
  1:9 INTEGER '10'
  1:11 SEMICOLON ';'
  ```

### 1.2 `nova ast <file>`
* **Purpose:** Parses the token stream and prints the hierarchical Abstract Syntax Tree.
* **Exit Codes:** `0` on success; `1` on syntax or lexical errors (stopping at the first error).
* **Example:**
  ```powershell
  .\build\nova.exe ast examples\basic.nova
  ```
* **Output:**
  ```text
  Program
    Let x
      Integer 10
    Let y
      Integer 20
    Let z
      Binary +
        Binary *
          Variable x
          Integer 2
        Variable y
    Print
      Variable z
  ```

### 1.3 `nova tac <file>`
* **Purpose:** Runs static semantic analysis (declare-before-use, duplicate variable detection), then generates and prints linear Three-Address Code (TAC).
* **Exit Codes:** `0` on success; `1` on semantic errors (e.g. use of undeclared variable).
* **Example:**
  ```powershell
  .\build\nova.exe tac examples\basic.nova
  ```
* **Output:**
  ```text
  x = 10
  y = 20
  $t1 = x * 2
  z = $t1 + y
  print z
  ```

### 1.4 `nova run <file>`
* **Purpose:** Runs semantic analysis and executes the program using the reference interpreter.
* **Exit Codes:** `0` on success; `1` on runtime errors (e.g. division by zero with exact source line:column).
* **Example:**
  ```powershell
  .\build\nova.exe run examples\basic.nova
  ```
* **Output:**
  ```text
  40
  ```

### 1.5 `nova opt <file> [--passes=fold,propagate,simplify,dce] [--show-all]`
* **Purpose:** Optimizes the TAC program using a 4-pass fixed-point optimizer.
* **Arguments:**
  * `--passes=a,b`: Selects a custom subset and ordering of passes.
  * `--show-all`: Prints the intermediate TAC after every mutating pass and iteration.
* **Example:**
  ```powershell
  .\build\nova.exe opt examples\optimize_demo.nova --show-all
  ```

---

## 2. Track 2: SmartPass & Benchmark Lab Commands

### 2.1 `nova bench list`
* **Purpose:** Displays registered numerical C++ benchmarks, descriptions, and designated tuning/holdout sizes.
* **Example Output:**
  ```text
  matmul      tuning sizes: 256, 384     holdout sizes: 320, 448
  stencil     tuning sizes: 800000       holdout sizes: 1200000
  vector_add  tuning sizes: 4000000      holdout sizes: 8000000
  ```

### 2.2 `nova bench run [options]`
* **Purpose:** Compiles default GCC `-O2` and `-O3` baselines, runs the correctness oracle, collects repeated timing samples, and writes raw data.
* **Key Flags:**
  * `--affinity=10`: Pins execution to logical CPU core 4 to eliminate scheduling noise.
  * `--high-priority`: Elevates process priority class.
  * `--reps=15`: Number of interleaved timing repetitions.
  * `--warmup=2`: Number of un-timed warm-up iterations.

### 2.3 `nova bench report <dir>`
* **Purpose:** Validates and summarizes an existing experiment directory, checking manifests and computing median and IQR statistics.

### 2.4 `nova tune <benchmark> --budget=B [options]`
* **Purpose:** Evaluates standard baselines plus at most $B$ searched candidate configurations.
* **Key Flags:**
  * `--strategy=fixed|random|adaptive`: Selects the search algorithm (default: `random`).
  * `--budget=N`: Hard limit on the number of non-baseline candidate evaluations.
  * `--ablation=none|no-features|no-history|no-feedback`: Tests policy components.
  * `--history=FILE`: Supplies cross-kernel historical prior database.
  * `--max-binary-growth=2.0`: Rejects candidates whose binary exceeds $2\times$ baseline `-O2`.
* **Example:**
  ```powershell
  .\build\nova.exe tune matmul --budget=4 --strategy=adaptive --affinity=10 --high-priority
  ```

### 2.5 `nova explain <results-dir>`
* **Purpose:** Explains the autotuning outcome: prints every candidate evaluated, its lifecycle state, rejection reason (if any), and the winning configuration.

### 2.6 `nova evaluate <tune-dir> [options]`
* **Purpose:** Takes a finished tuning directory and tests the winning configuration against `-O2`/`-O3` on reserved holdout sizes to verify generalization.

### 2.7 `nova table <benchmark> [options]`
* **Purpose:** Exhaustively measures all 24 configurations across sizes to create ground-truth lookup tables for offline study.

### 2.8 `nova study --tables=... --out=DIR`
* **Purpose:** Replays search strategies against saved ground-truth tables across hundreds of simulated seeds.

### 2.9 `nova history --tables=... --out=FILE`
* **Purpose:** Extracts a tuning-size historical performance database for the adaptive search policy.

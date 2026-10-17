#pragma once

#include <cstdint>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

#include "nova/bench/env.hpp"
#include "nova/bench/json.hpp"
#include "nova/bench/records.hpp"
#include "nova/bench/registry.hpp"

namespace nova {
namespace bench {

// ---- building blocks (also used by the SmartPass search) -------------------------------------------------

struct BuildOutcome {
    bool ok = false;
    std::string reason;                 // why the build failed (empty when ok)
    std::vector<std::string> command;   // the exact argv that was run
    double seconds = 0;
    std::int64_t binary_size = 0;
    std::string log;                    // compiler stdout+stderr
};

// Compiles `source` into `exe` with `compiler flags... source -o exe`. No shell is involved.
BuildOutcome build_kernel(const std::string& compiler, const std::vector<std::string>& flags,
                          const std::string& source, const std::string& exe, int timeout_ms);

struct KernelRun {
    bool ok = false;                       // process ran, exited 0 and its output parsed
    std::string reason;                    // failure description when !ok
    bool timed_out = false;
    KernelOutput output;                   // valid when ok
    double process_wall_ms = 0;
    std::vector<std::string> mismatches;   // oracle differences (only computed when a reference is given)
    bool correct() const { return ok && mismatches.empty(); }
};

// Runs `exe <n> <seed>` once, parses its output and (if `reference` is non-null) compares it to the oracle.
// `affinity_mask` / `high_priority` pin and prioritize the child process to reduce scheduler noise (0 / false
// leave the OS default).
KernelRun run_kernel(const std::string& exe, std::int64_t n, std::uint64_t seed, int timeout_ms,
                     const KernelOutput* reference, std::uint64_t affinity_mask = 0, bool high_priority = false);

// Lazily computed, memoized oracle results.
class ReferenceCache {
public:
    // Returns nullptr and sets `error` when the reference cannot be computed.
    const KernelOutput* get(const std::string& benchmark, std::int64_t n, std::uint64_t seed, std::string& error);

private:
    std::map<std::string, KernelOutput> cache_;
};

// SHA-256 of the kernel source file followed by common.hpp from the same directory (if present).
bool kernel_source_hash(const std::string& kernel_dir, const std::string& benchmark, std::string& hash,
                        std::string& error);

// ---- shared validate-and-time stage (baselines and SmartPass candidates) ----------------------------------

struct TimingSlot {
    RunRecord record;        // metadata filled by the caller; build_status must already be "OK" or "FAILED"
    std::string exe;
    bool active = false;     // still eligible for timing
};

struct TimingParams {
    std::string experiment_id;
    int repetitions = 9;
    int warmup_runs = 1;
    std::uint64_t seed = 1;
    std::uint64_t order_seed = 20261014;
    int run_timeout_ms = 120000;
    std::uint64_t affinity_mask = 0;
    bool high_priority = false;
};

// For one benchmark and input size: validates every slot against the oracle (discarded run), performs the warm-up
// runs, then times the surviving slots in `repetitions` interleaved rounds (seeded random order per round). Every
// run is re-validated; a slot that fails at any point becomes correctness FAIL and loses its samples. Statistics
// and `lifecycle` (BUILD_FAILED / CORRECTNESS_FAILED / BENCHMARKED) are filled in. Appends RawSample rows.
// `commands` (optional) receives the validation command per slot. Returns false only if the oracle cannot be built.
bool validate_and_time(std::vector<TimingSlot>& slots, const std::string& benchmark, std::int64_t n,
                       const TimingParams& params, std::uint64_t order_salt, ReferenceCache& refs,
                       std::vector<RawSample>& raw, Json* commands, std::ostream* log, std::string& error);

// ---- baseline experiment ----------------------------------------------------------------------------------

struct Variant {
    std::string opt_level;                // "O2", "O3", "Ofast"
    std::vector<std::string> flags;       // optimization flags added to the fixed flags
    std::string fp_semantics;             // "strict" or "fast-math"
};

struct BenchConfig {
    std::string compiler = "g++";
    std::string kernel_dir = "benchmarks/kernels";
    std::string out_dir;                              // required for write_experiment
    std::string experiment_id;                        // required
    std::vector<std::string> benchmarks;              // empty = all registered
    std::vector<std::int64_t> sizes;                  // empty = registry sizes selected by `role`
    std::string role = "tuning";                      // "tuning", "holdout" or "all" (used when sizes is empty)
    int repetitions = 9;
    int warmup_runs = 1;                              // discarded runs per variant after the validation run
    std::uint64_t seed = 1;                           // input seed
    std::uint64_t order_seed = 20261014;              // seed of the interleaving order
    bool include_ofast = false;                       // adds -Ofast as a separate, fast-math condition
    int build_timeout_ms = 180000;
    int run_timeout_ms = 120000;
    std::uint64_t affinity_mask = 0;                  // CPU mask for benchmark runs (0 = OS default)
    bool high_priority = false;                       // run benchmark processes at high priority (Windows)
    std::vector<std::string> fixed_flags = {"-std=c++17", "-msse2", "-mfpmath=sse"};
};

std::vector<Variant> baseline_variants(bool include_ofast);

struct ExperimentResult {
    Json manifest;
    std::vector<RunRecord> records;
    std::vector<RawSample> raw;
    std::map<std::string, std::string> build_logs;  // candidate id -> compiler output
};

// Builds every variant of every selected benchmark, validates each against the oracle, then times them with
// interleaved, seeded-random order across `repetitions` rounds per size. `log` (optional) receives progress.
// Returns false with `error` only for configuration/infrastructure problems; a variant that fails to build or
// is incorrect is recorded in the results, not treated as an error.
bool run_experiment(const BenchConfig& cfg, ExperimentResult& result, std::string& error, std::ostream* log);

// Writes manifest.json, results.json, results.csv, raw_timings.csv and logs/ into cfg.out_dir.
bool write_experiment(const BenchConfig& cfg, const ExperimentResult& result, std::string& error);

// Reads manifest.json and results.json back. Fails if a record is incomplete.
bool load_experiment(const std::string& dir, Json& manifest, std::vector<RunRecord>& records, std::string& error);

// Plain-text table grouped by benchmark and size, with spread, and a cautious O2-vs-O3 comparison.
std::string format_report(const std::vector<RunRecord>& records);

}  // namespace bench
}  // namespace nova

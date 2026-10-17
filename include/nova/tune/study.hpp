#pragma once

#include <cstdint>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

#include "nova/bench/harness.hpp"
#include "nova/bench/json.hpp"
#include "nova/tune/adaptive.hpp"
#include "nova/tune/space.hpp"

namespace nova {
namespace tune {

// ---- exhaustive measurement tables ----------------------------------------------------------------------------------
//
// `nova table` measures EVERY configuration of the space (both baselines and all searchable candidates) on given sizes
// with the standard protocol (validation against the oracle, interleaved timing). No candidate is chosen or ranked. The
// resulting tables are the ground truth the replay study (below) draws its measurements from.

struct TableConfig {
    std::string benchmark;
    std::vector<std::int64_t> sizes;  // empty = registry sizes selected by `role`
    std::string role = "tuning";      // "tuning", "holdout" or "all" (used when sizes is empty)
    std::string compiler = "g++";
    std::string kernel_dir = "benchmarks/kernels";
    std::string out_dir;
    std::string experiment_id;
    int repetitions = 15;
    int warmup_runs = 2;
    std::uint64_t seed = 1;
    std::uint64_t order_seed = 20261014;
    int build_timeout_ms = 180000;
    int run_timeout_ms = 120000;
    std::uint64_t affinity_mask = 0;
    bool high_priority = false;
    std::vector<std::string> fixed_flags = {"-std=c++17", "-msse2", "-mfpmath=sse"};
};

bool run_table(const TableConfig& cfg, const SearchSpace& space, const Whitelist& whitelist, bench::ExperimentResult& result,
               std::string& error, std::ostream* log);

// One measured configuration on one workload.
struct TableEntry {
    std::string id;
    bool eligible = false;       // built, correct, fully timed and within the binary-size policy
    std::string lifecycle;
    std::string failure_reason;
    double median_ms = 0;
    std::vector<double> samples_ms;
    std::int64_t binary_size = 0;
};

struct WorkloadTable {
    std::string benchmark;
    std::int64_t size = 0;
    std::string role;            // "tuning" or "holdout"
    std::string source;          // experiment id
    std::string source_hash;
    std::map<std::string, TableEntry> entries;  // by candidate id
};

// Loads every (benchmark, size) table of a `nova table` result directory. Fails if a table lacks any configuration of
// `space`, or a record is invalid.
bool load_tables(const std::string& dir, const SearchSpace& space, double max_binary_growth, std::vector<WorkloadTable>& out,
                 std::string& error);

// ---- replay study ----------------------------------------------------------------------------------------------------
//
// Replays search strategies against the saved tables instead of re-measuring: a strategy proposes candidates, the
// "measurement" it gets back is drawn from the saved raw samples of that candidate on the tuning workload (the pilot
// gain of the adaptive policy re-samples `pilot_reps` of the saved samples with a seeded generator, so pilot noise is
// realistic). The final choice among the evaluated candidates and the baselines uses the saved full-protocol medians
// of the tuning workload. The chosen configuration is then looked up in the HOLDOUT tables of the same benchmark; that
// lookup is only reported and never feeds back into any choice.
struct StudyConfig {
    std::vector<std::string> methods = {"fixed", "random", "adaptive", "adaptive-no-features", "adaptive-no-history", "adaptive-no-feedback"};
    std::vector<int> budgets = {1, 2, 3, 4, 6, 8, 12, 22};
    int seeds = 30;           // seeds 1..seeds for stochastic methods (fixed and baselines are deterministic: seed 0)
    int pilot_reps = 5;
    bool history_across_workloads = true;  // adaptive methods see the tuning results of the OTHER workloads
};

struct StudyRow {
    std::string method;
    int budget = 0;
    int seed = 0;
    std::string benchmark;
    std::int64_t tuning_size = 0;
    std::string selected;
    int evaluations = 0;
    int failed_evaluations = 0;
    double tuning_speedup = 1;      // best-baseline median / selected median on the tuning workload (>= 1 by construction)
    double oracle_speedup = 1;      // best-baseline median / best eligible median in the whole space
    double regret = 1;              // selected median / oracle median on the tuning workload (1 = optimal)
    std::vector<std::pair<std::int64_t, double>> holdout;  // (holdout size, best-baseline median / selected median)
    double holdout_mean = 0;        // mean of the holdout speedups (0 if there is no holdout table)
    std::vector<std::string> sequence;
};

struct StudyResult {
    std::vector<StudyRow> rows;
    bench::Json manifest;
};

bool run_study(const std::vector<WorkloadTable>& tables, const SearchSpace& space, const StudyConfig& cfg, StudyResult& out,
               std::string& error);

std::string study_runs_csv(const std::vector<StudyRow>& rows);
// Summary per (method, budget, benchmark): count, mean, median, min, max, stdev of the main metrics.
std::string study_summary_csv(const std::vector<StudyRow>& rows);
bool write_study(const std::string& dir, const StudyResult& result, std::string& error);

// `nova table ...` and `nova study ...`
int run_table_command(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);
// `nova history --tables=DIRS --out=FILE`: converts the TUNING tables of `nova table` results into a history file.
int run_history_command(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);
int run_study_command(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);
const char* study_usage();

}  // namespace tune
}  // namespace nova

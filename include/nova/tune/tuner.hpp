#pragma once

#include <cstdint>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

#include "nova/bench/harness.hpp"
#include "nova/bench/json.hpp"
#include "nova/bench/records.hpp"
#include "nova/tune/adaptive.hpp"
#include "nova/tune/space.hpp"

namespace nova {
namespace tune {

// Bounded SmartPass search (V0.7): evaluates the baselines plus at most `budget` search candidates chosen by a
// fixed or random strategy, on ONE tuning input size, and ranks only candidates that are correct.
//
// Budget accounting (documented in docs/smartpass.md): one *evaluation* = one search candidate that is compiled
// and put through the correctness check. Baselines (plain -O2 and -O3) are ALWAYS evaluated in addition and do
// not count against the budget; the same convention holds for every strategy. There are no automatic retries.
// All builds and timing repetitions count toward the wall-clock `tuning_wall_time_s`.
struct TuneConfig {
    std::string benchmark;
    int budget = 8;
    Strategy strategy = Strategy::Random;
    std::uint64_t search_seed = 1;     // drives the Random strategy
    std::int64_t size = 0;             // 0 = the first registry tuning size; holdout sizes are refused
    std::string compiler = "g++";
    std::string kernel_dir = "benchmarks/kernels";
    std::string out_dir;               // required
    std::string experiment_id;         // required
    int repetitions = 9;
    int warmup_runs = 1;
    std::uint64_t seed = 1;            // kernel input seed
    std::uint64_t order_seed = 20261014;
    int build_timeout_ms = 180000;
    int run_timeout_ms = 120000;
    std::uint64_t affinity_mask = 0;
    bool high_priority = false;
    std::vector<std::string> fixed_flags = {"-std=c++17", "-msse2", "-mfpmath=sse"};
    // Eligibility policy: a correct candidate whose binary is larger than max_binary_growth x the baseline -O2
    // binary is REJECTED_BY_POLICY (code-size blow-up guard). <= 0 disables the check.
    double max_binary_growth = 2.0;

    // ---- adaptive strategy only (V0.8) ----
    Ablation ablation;                  // which signal to remove (default: none, i.e. the full policy)
    std::string history_path;           // persisted prior experiments; empty = no history (session-local only)
    bool history_exclude_same = false;  // drop history entries from the very same (benchmark, size)
    bool record_history = false;        // append this run's tuning-size outcomes to history_path afterwards
    int pilot_reps = 5;                 // interleaved anchor/candidate rounds per candidate during the adaptive search
};

struct TuneResult {
    bench::Json manifest;
    std::vector<bench::RunRecord> records;  // baselines first, then candidates in evaluation order
    std::vector<bench::RawSample> raw;
    std::map<std::string, std::string> build_logs;
    std::vector<std::string> sequence;      // ids of the search candidates, in the order they were evaluated
    int evaluations = 0;                    // search candidates built and validated (always <= budget)
    int builds_attempted = 0;               // baselines + search candidates
    bool has_winner = false;                // false when no candidate is eligible
    std::string winner_id;
    double tuning_wall_time_s = 0;
    std::vector<HistoryEntry> history_used;   // entries (after filtering) that were visible to the adaptive policy
    std::vector<HistoryEntry> new_history;    // tuning-size outcomes of this run, ready to append to a history file
};

// Runs the pipeline: validate configuration and every flag (before any process is started) -> build ->
// correctness -> interleaved timing -> eligibility/ranking. Returns false (with `error`) for configuration or
// infrastructure problems; individual candidate failures are recorded in the result, never dropped.
bool run_tune(const TuneConfig& cfg, const SearchSpace& space, const Whitelist& whitelist, TuneResult& result,
              std::string& error, std::ostream* log);

// Appends this run's tuning-size outcomes (TuneResult::new_history) to cfg.history_path. Only tuning-size
// results are ever stored; holdout and ad-hoc sizes produce no entries.
bool record_tune_history(const TuneConfig& cfg, const TuneResult& result, std::string& error);

// Writes manifest.json, results.json, results.csv, raw_timings.csv and logs/ into cfg.out_dir.
bool write_tune(const TuneConfig& cfg, const TuneResult& result, std::string& error);

// Human-readable account of a saved or fresh tuning result: every candidate with its lifecycle state and failure
// reason, the ranking among eligible candidates, the winner, and how it compares with the baselines.
std::string explain_tune(const bench::Json& manifest, const std::vector<bench::RunRecord>& records);

}  // namespace tune
}  // namespace nova

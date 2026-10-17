#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "nova/bench/json.hpp"
#include "nova/tune/features.hpp"
#include "nova/tune/space.hpp"

namespace nova {
namespace tune {

// ---- Adaptive, workload-aware candidate prioritization (V0.8) --------------------------------------------------
//
// This is a *transparent heuristic*, not machine learning: no model is trained or fitted. Each unevaluated
// candidate c gets a score in "log-gain" units (log of the expected speedup over the plain -O2 baseline):
//
//   score(c) = prior(c | workload)  +  history(c | workload)  +  feedback(c)
//
//   prior     (signal 1: workload descriptors) hand-written expectations of which flags help which kind of
//             workload; see docs/adaptive.md for every constant and its rationale. Zero when the workload is unknown.
//   history   (signal 2: prior experiments) similarity-weighted, shrunk average of the log-gains this same flag
//             configuration achieved in earlier experiments (persisted history). Zero when there is no history.
//   feedback  (signal 3: outcomes of this run) neighbour-smoothed average of the log-gains of the candidates
//             already evaluated in this run (neighbours = Hamming distance on the flag features). Zero before the
//             first outcome.
//
// The candidate with the highest score is evaluated next; exact ties are broken by a fixed per-candidate random
// key derived from the search seed, so the whole sequence is a deterministic function of
// (space, workload, history, ablation, seed, observed outcomes).
//
// Ablations remove exactly one declared signal by forcing its term to zero:
//   no-features   removes the workload-descriptor prior   (the policy then ignores which workload it is tuning)
//   no-history    removes persisted prior experiments
//   no-feedback   removes within-run feedback             (the sequence becomes independent of measurements)

struct Ablation {
    bool use_features = true;
    bool use_history = true;
    bool use_feedback = true;
};
bool parse_ablation(const std::string& text, Ablation& out);  // "none", "no-features", "no-history", "no-feedback"
std::string ablation_name(const Ablation& a);                  // inverse of parse_ablation for single-signal ablations

// One prior observation: how a flag configuration performed on a workload.
struct HistoryEntry {
    std::string benchmark;
    std::int64_t size = 0;
    std::string size_role;     // "tuning" only is ever stored or used; see filter_history
    std::string candidate;     // candidate id, e.g. "O3+unroll"
    double log_gain = 0;       // log(median(-O2) / median(candidate)) on that workload; > 0 = faster than -O2
    bool failed = false;       // the candidate failed to build or was incorrect on that workload
    std::string source;        // experiment id the entry came from
};

// Which history entries may influence a selection. Holdout (and ad-hoc) sizes are never admitted: only entries whose
// size_role is "tuning" survive. If exclude_same_workload is set, entries measured on the very same
// (benchmark, size) are dropped too (used by experiments that measure transfer between workloads).
std::vector<HistoryEntry> filter_history(const std::vector<HistoryEntry>& entries, const std::string& benchmark,
                                         std::int64_t size, bool exclude_same_workload);

bench::Json history_to_json(const std::vector<HistoryEntry>& entries);
bool history_from_json(const bench::Json& j, std::vector<HistoryEntry>& out, std::string& error);
// File helpers: a missing file is an empty history (load) / created (append); a malformed file is an error.
bool load_history_file(const std::string& path, std::vector<HistoryEntry>& out, std::string& error);
// Appends `entries` after refusing any non-"tuning" role (so holdout measurements can never enter the history).
bool append_history_file(const std::string& path, const std::vector<HistoryEntry>& entries, std::string& error);

struct FlagFeatures {
    bool o3 = false, unroll = false, vec = false /*effective: explicit or implied by -O3*/, prefetch = false, native = false;
};
FlagFeatures flag_features(const Candidate& c);

class AdaptivePolicy {
public:
    // `space.candidates` are the candidates the policy chooses among (indices refer to that vector).
    AdaptivePolicy(const SearchSpace& space, const WorkloadFeatures& workload, const std::vector<HistoryEntry>& history,
                   const Ablation& ablation, std::uint64_t seed);

    // Index of the next candidate to evaluate, or -1 if all have been evaluated. The returned candidate counts as
    // evaluated from now on (call observe() with its outcome).
    int next();
    // Records the outcome of candidate `index`: ok = built and correct, log_gain = log(median(anchor)/median(candidate)).
    // A failed candidate (ok == false) is recorded with the fixed penalty kFailureLogGain.
    void observe(int index, bool ok, double log_gain);

    // Score components, exposed for tests and for the report (all in log-gain units).
    double prior(int index) const;
    double history(int index) const;
    double feedback(int index) const;
    double score(int index) const;
    const std::vector<int>& order() const { return order_; }

    static constexpr double kFailureLogGain = -0.3;  // outcome assigned to a failed candidate
    static constexpr double kShrinkage = 1.0;        // pseudo-observations of "no gain" in the smoothed averages

private:
    const SearchSpace& space_;
    WorkloadFeatures workload_;
    std::vector<HistoryEntry> history_;
    Ablation ablation_;
    std::vector<FlagFeatures> flags_;
    std::vector<double> tie_key_;
    std::vector<bool> evaluated_;
    std::vector<double> outcome_;  // log-gain per evaluated candidate
    std::vector<int> order_;
};

}  // namespace tune
}  // namespace nova

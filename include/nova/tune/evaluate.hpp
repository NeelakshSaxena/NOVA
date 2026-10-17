#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "nova/bench/harness.hpp"
#include "nova/tune/space.hpp"

namespace nova {
namespace tune {

// Holdout evaluation (V0.8): takes a FINISHED tuning result, rebuilds its selected configuration and the plain
// -O2/-O3 baselines, and measures all of them on input sizes the search never saw. This is a one-way flow: the
// function only reads the tuning result; it never alters it, never writes the history, and nothing it measures can
// feed back into a candidate selection. Records are written with kind "holdout-eval".
struct EvalConfig {
    std::string tune_dir;             // finished tuning result (manifest.json + results.json)
    std::string out_dir;              // required
    std::string experiment_id;        // required
    std::vector<std::int64_t> sizes;  // empty = the benchmark's registered holdout sizes
    std::string compiler = "g++";
    std::string kernel_dir;           // empty = the kernel_dir recorded in the tuning manifest
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

bool run_evaluate(const EvalConfig& cfg, const Whitelist& whitelist, bench::ExperimentResult& result, std::string& error,
                  std::ostream* log);

// Per-size table: winner vs plain -O2/-O3, ratio of medians against the best baseline and whether the interquartile
// ranges overlap (then the difference is called inconclusive).
std::string format_holdout_report(const std::vector<bench::RunRecord>& records);

// `nova evaluate <tune-dir> [options]`
int run_evaluate_command(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);
const char* evaluate_usage();

}  // namespace tune
}  // namespace nova

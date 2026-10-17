#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "nova/bench/env.hpp"
#include "nova/bench/json.hpp"

namespace nova {
namespace bench {

// One build/run configuration of one benchmark at one input size. This is the unit stored in results.json
// and as one row of results.csv. Every field is required (see validate_record).
struct RunRecord {
    std::string experiment_id;
    std::string benchmark;
    std::string source_hash;        // SHA-256 (hex) of the kernel source + common.hpp
    std::string compiler_version;
    std::string flags;              // exact compiler flags, space separated (fixed flags + optimization flags)
    std::string opt_level;          // "O2", "O3", "Ofast", ...
    std::string fp_semantics;       // "strict" (IEEE-conforming flags) or "fast-math" (-Ofast / -ffast-math)
    std::int64_t input_size = 0;
    std::string size_role;          // "tuning", "holdout" or "adhoc"
    std::uint64_t seed = 0;
    std::string candidate_id;
    std::string build_status;       // "OK" | "FAILED"
    std::string correctness_status; // "PASS" | "FAIL" | "NOT_RUN"
    std::string failure_reason;     // empty iff nothing failed
    int warmup_runs = 0;
    int repetitions = 0;            // number of timed runs requested
    std::vector<double> timing_samples_ms;  // every raw sample, in run order
    double median_ms = 0, q1_ms = 0, q3_ms = 0, iqr_ms = 0, mad_ms = 0, min_ms = 0, max_ms = 0;
    std::int64_t binary_size_bytes = 0;
    double build_time_s = 0;
    int candidate_budget = 0;       // 0 for plain baseline runs
    std::string search_strategy;    // "baseline" for fixed GCC levels
    double tuning_wall_time_s = 0;
    bool selected = false;
    // Candidate lifecycle: GENERATED -> BUILD_FAILED | BUILT -> CORRECTNESS_FAILED | CORRECT -> BENCHMARKED ->
    // ELIGIBLE | REJECTED_BY_POLICY. Only terminal/persistent states are stored (BUILT/CORRECT are transient).
    std::string lifecycle;
    int rank = 0;                   // 1 = best eligible candidate, 0 = not ranked
};

// Fills the statistic fields from timing_samples_ms (no-op fields stay zero for an empty sample set).
void compute_statistics(RunRecord& r);

// Problems that make the record unusable as evidence (missing/empty metadata, inconsistent counts, statistics
// that do not match the samples, a PASS status without samples, ...). Empty means the record is complete.
std::vector<std::string> validate_record(const RunRecord& r);

// Lifecycle implied by build/correctness status (used for files written before candidates had a lifecycle).
std::string derive_lifecycle(const RunRecord& r);

Json record_to_json(const RunRecord& r);
// Fails (with a message naming the field) if any field is missing or has the wrong type.
bool record_from_json(const Json& j, RunRecord& r, std::string& error);

// results.csv (schema of docs/benchmarks.md): header line + one row per record; samples are a JSON list.
std::string csv_header();
std::string record_to_csv_row(const RunRecord& r);
std::string csv_escape(const std::string& field);

// One timed execution; written to raw_timings.csv.
struct RawSample {
    std::string experiment_id;
    std::string candidate_id;
    std::string benchmark;
    std::int64_t input_size = 0;
    int repetition = 0;       // 1-based
    int order_position = 0;   // position of this run within its (interleaved) repetition round
    double time_ms = 0;       // time reported by the kernel for the timed region
    double process_wall_ms = 0;
};
std::string raw_csv_header();
std::string raw_to_csv_row(const RawSample& s);

}  // namespace bench
}  // namespace nova

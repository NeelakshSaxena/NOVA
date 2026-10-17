#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nova {
namespace bench {

// Benchmark registry (the "manifest" of available workloads). The kernel sources live in
// benchmarks/kernels/<name>.cpp; see docs/benchmarks.md for the exact definition of each workload.
struct BenchmarkDef {
    std::string name;
    std::string description;
    std::vector<std::int64_t> tuning_sizes;   // sizes a search/tuning run may use
    std::vector<std::int64_t> holdout_sizes;  // reserved for evaluation; never used to choose candidates
};

const std::vector<BenchmarkDef>& registry();
const BenchmarkDef* find_benchmark(const std::string& name);

// What a kernel prints (see benchmarks/kernels/common.hpp).
struct KernelOutput {
    std::string kernel;
    std::int64_t n = 0;
    std::uint64_t seed = 0;
    std::uint64_t len = 0;
    double sum = 0, s0 = 0, s1 = 0, s2 = 0, wsum = 0;
    double time_ms = 0;
};

// Parses kernel stdout. Every field is required; returns false and sets `error` otherwise.
bool parse_kernel_output(const std::string& text, KernelOutput& out, std::string& error);

// Independent reference result for (benchmark, n, seed) computed by the harness itself (long double
// arithmetic, different code from the kernels). Returns false for an unknown benchmark / bad size.
bool reference_output(const std::string& benchmark, std::int64_t n, std::uint64_t seed, KernelOutput& out,
                      std::string& error);

// Documented tolerance: |actual - expected| <= kRelativeTolerance * max(1, |expected|) for every statistic.
constexpr double kRelativeTolerance = 1e-9;

// Compares a kernel's output to the reference. Returns the list of mismatches (empty = correct).
std::vector<std::string> compare_to_reference(const KernelOutput& expected, const KernelOutput& actual,
                                              double tolerance = kRelativeTolerance);

}  // namespace bench
}  // namespace nova

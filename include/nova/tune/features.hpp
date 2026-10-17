#pragma once

#include <cstdint>
#include <string>

namespace nova {
namespace tune {

// Workload descriptors used by the adaptive policy (V0.8). They describe the *kernel and input size*, are computed
// from the benchmark's documented definition (docs/benchmarks.md) and never from measurements, so extraction is
// deterministic and cannot leak timing information.
//
//   working_set_bytes    bytes of data the kernel touches (all arrays, 8-byte doubles)
//   flops_per_byte       nominal floating-point operations per working-set byte (arithmetic intensity)
//   loop_nest_depth      depth of the hot loop nest as written in the kernel source
//   innermost_reduction  the innermost loop accumulates into a scalar (strict FP forbids reassociating it)
//   unit_stride_streaming  the hot loops walk arrays with unit stride and no loop-carried reduction
//   size_log2            log2 of the size parameter n (used to compare input sizes)
struct WorkloadFeatures {
    bool known = false;  // false for an unregistered benchmark: all descriptors are then zero/false and the policy
                         // treats the workload as "no information" (see docs/adaptive.md)
    std::string benchmark;
    std::int64_t n = 0;
    double working_set_bytes = 0;
    double flops_per_byte = 0;
    int loop_nest_depth = 0;
    bool innermost_reduction = false;
    bool unit_stride_streaming = false;
    double size_log2 = 0;
};

// Deterministic: the same (benchmark, n) always gives the same descriptors. Unknown benchmark or n <= 0 gives
// known == false.
WorkloadFeatures extract_features(const std::string& benchmark, std::int64_t n);

}  // namespace tune
}  // namespace nova

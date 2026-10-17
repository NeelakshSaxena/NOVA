#include "nova/tune/features.hpp"

#include <cmath>

namespace nova {
namespace tune {

WorkloadFeatures extract_features(const std::string& benchmark, std::int64_t n) {
    WorkloadFeatures f;
    f.benchmark = benchmark;
    f.n = n;
    if (n <= 0) return f;
    const double dn = static_cast<double>(n);
    if (benchmark == "matmul") {
        // A, B, C: three n x n double matrices; 2*n^3 flops; i-j-k nest with a scalar reduction over k and a
        // column-strided access to B.
        f.working_set_bytes = 3.0 * dn * dn * 8.0;
        f.flops_per_byte = (2.0 * dn * dn * dn) / f.working_set_bytes;
        f.loop_nest_depth = 3;
        f.innermost_reduction = true;
        f.unit_stride_streaming = false;
    } else if (benchmark == "vector_add") {
        // a and b: n doubles each; 16 passes of one add per element.
        f.working_set_bytes = 2.0 * dn * 8.0;
        f.flops_per_byte = (16.0 * dn) / f.working_set_bytes;
        f.loop_nest_depth = 2;
        f.innermost_reduction = false;
        f.unit_stride_streaming = true;
    } else if (benchmark == "stencil") {
        // two buffers of n doubles; 100 sweeps of 5 flops per interior cell.
        f.working_set_bytes = 2.0 * dn * 8.0;
        f.flops_per_byte = (100.0 * 5.0 * dn) / f.working_set_bytes;
        f.loop_nest_depth = 2;
        f.innermost_reduction = false;
        f.unit_stride_streaming = true;
    } else {
        return f;  // unknown: known stays false
    }
    f.size_log2 = std::log(dn) / std::log(2.0);
    f.known = true;
    return f;
}

}  // namespace tune
}  // namespace nova

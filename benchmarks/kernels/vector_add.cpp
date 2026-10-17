// Vector addition kernel: a[i] = a[i] + b[i], applied kPasses times (in place accumulation) so the timed
// region is long enough to measure. Input: a then b from Rng(seed).uniform(). Output array: a.
// Result after the passes: a[i] = (...((a0[i] + b[i]) + b[i]) ...) with kPasses additions (strict FP order).
#include "common.hpp"

namespace {

constexpr int kPasses = 16;

#if defined(__GNUC__)
__attribute__((noinline))
#endif
void vector_add(double* a, const double* b, std::size_t n) {
    for (int pass = 0; pass < kPasses; ++pass) {
        for (std::size_t i = 0; i < n; ++i) a[i] = a[i] + b[i];
    }
}

}  // namespace

int main(int argc, char** argv) {
    nova_kernel::Args args;
    if (!nova_kernel::parse_args(argc, argv, args)) return 2;
    const std::size_t n = static_cast<std::size_t>(args.n);
    std::vector<double> a(n), b(n);
    nova_kernel::Rng rng(args.seed);
    for (std::size_t i = 0; i < n; ++i) a[i] = rng.uniform();
    for (std::size_t i = 0; i < n; ++i) b[i] = rng.uniform();

    nova_kernel::escape(a.data());
    nova_kernel::escape(b.data());
    double t0 = nova_kernel::now_ms();
    vector_add(a.data(), b.data(), n);
    nova_kernel::escape(a.data());
    double t1 = nova_kernel::now_ms();

    nova_kernel::print_report("vector_add", args, a, t1 - t0);
    return 0;
}

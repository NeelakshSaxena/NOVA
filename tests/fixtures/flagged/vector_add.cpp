// TEST FIXTURE for the SmartPass lifecycle tests: a correct vector_add kernel whose behaviour can be broken by
// preprocessor flags that only the tests put on a test-only whitelist. Never used outside tests.
//   -DNOVA_FIXTURE_BREAK  fails to compile                (BUILD_FAILED)
//   -DNOVA_FIXTURE_WRONG  skips the computation: very fast but wrong output   (CORRECTNESS_FAILED)
//   -DNOVA_FIXTURE_HANG   never terminates                (timeout -> CORRECTNESS_FAILED)
//   -DNOVA_FIXTURE_BIG    links a 400 KB constant blob    (REJECTED_BY_POLICY via the binary-size limit)
#include "../../../benchmarks/kernels/common.hpp"

#ifdef NOVA_FIXTURE_BREAK
#error "fixture: deliberate build failure"
#endif

#ifdef NOVA_FIXTURE_BIG
static const unsigned char kBlob[400000] = {1, 2, 3};
#endif

namespace {

#if defined(__GNUC__)
__attribute__((noinline))
#endif
void vector_add(double* a, const double* b, std::size_t n) {
    for (int pass = 0; pass < 16; ++pass)
        for (std::size_t i = 0; i < n; ++i) a[i] = a[i] + b[i];
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
#ifdef NOVA_FIXTURE_BIG
    nova_kernel::escape(kBlob);
#endif
#ifdef NOVA_FIXTURE_HANG
    volatile int spin = 1;
    while (spin) {
    }
#endif
    nova_kernel::escape(a.data());
    nova_kernel::escape(b.data());
    double t0 = nova_kernel::now_ms();
#ifndef NOVA_FIXTURE_WRONG
    vector_add(a.data(), b.data(), n);
#endif
    nova_kernel::escape(a.data());
    double t1 = nova_kernel::now_ms();
    nova_kernel::print_report("vector_add", args, a, t1 - t0);
    return 0;
}

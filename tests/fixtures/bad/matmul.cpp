// TEST FIXTURE: a deliberately incorrect matmul kernel (the k loop stops one element early). The benchmark
// harness must reject it with a correctness failure. Never used outside tests.
#include "../../../benchmarks/kernels/common.hpp"

int main(int argc, char** argv) {
    nova_kernel::Args args;
    if (!nova_kernel::parse_args(argc, argv, args)) return 2;
    const int n = static_cast<int>(args.n);
    const std::size_t count = static_cast<std::size_t>(n) * n;
    std::vector<double> a(count), b(count), c(count, 0.0);
    nova_kernel::Rng rng(args.seed);
    for (std::size_t i = 0; i < count; ++i) a[i] = rng.uniform();
    for (std::size_t i = 0; i < count; ++i) b[i] = rng.uniform();
    double t0 = nova_kernel::now_ms();
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            double sum = 0.0;
            for (int k = 0; k < n - 1; ++k) sum += a[static_cast<std::size_t>(i) * n + k] * b[static_cast<std::size_t>(k) * n + j];
            c[static_cast<std::size_t>(i) * n + j] = sum;
        }
    double t1 = nova_kernel::now_ms();
    nova_kernel::print_report("matmul", args, c, t1 - t0);
    return 0;
}

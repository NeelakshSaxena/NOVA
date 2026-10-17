// Matrix multiplication reference kernel: C = A * B for n x n row-major double matrices.
// Plain i-j-k loop order with a sequential sum over k (the baseline kernel; not claimed optimal).
// Input: A then B drawn row-major from Rng(seed).uniform(). Output array: C (n*n values).
#include "common.hpp"

namespace {

#if defined(__GNUC__)
__attribute__((noinline))
#endif
void matmul(const double* a, const double* b, double* c, int n) {
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            double sum = 0.0;
            for (int k = 0; k < n; ++k) sum += a[static_cast<std::size_t>(i) * n + k] * b[static_cast<std::size_t>(k) * n + j];
            c[static_cast<std::size_t>(i) * n + j] = sum;
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    nova_kernel::Args args;
    if (!nova_kernel::parse_args(argc, argv, args)) return 2;
    const int n = static_cast<int>(args.n);
    const std::size_t count = static_cast<std::size_t>(n) * n;
    std::vector<double> a(count), b(count), c(count, 0.0);
    nova_kernel::Rng rng(args.seed);
    for (std::size_t i = 0; i < count; ++i) a[i] = rng.uniform();
    for (std::size_t i = 0; i < count; ++i) b[i] = rng.uniform();

    nova_kernel::escape(a.data());
    nova_kernel::escape(b.data());
    double t0 = nova_kernel::now_ms();
    matmul(a.data(), b.data(), c.data(), n);
    nova_kernel::escape(c.data());
    double t1 = nova_kernel::now_ms();

    nova_kernel::print_report("matmul", args, c, t1 - t0);
    return 0;
}

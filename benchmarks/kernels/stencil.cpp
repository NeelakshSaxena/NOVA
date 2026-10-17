// Iterative numerical kernel: 1-D Jacobi smoothing (heat diffusion) on n cells for kSteps sweeps.
//   next[i] = 0.25*cur[i-1] + 0.5*cur[i] + 0.25*cur[i+1]  for 0 < i < n-1; end cells are fixed.
// Input: cur[i] from Rng(seed).uniform(). Output array: the final state. Each sweep depends on the previous
// one, so the sweeps cannot be hoisted or removed.
#include <utility>

#include "common.hpp"

namespace {

constexpr int kSteps = 100;

#if defined(__GNUC__)
__attribute__((noinline))
#endif
void smooth(double* cur, double* next, std::size_t n) {
    for (int step = 0; step < kSteps; ++step) {
        next[0] = cur[0];
        next[n - 1] = cur[n - 1];
        for (std::size_t i = 1; i + 1 < n; ++i) next[i] = 0.25 * cur[i - 1] + 0.5 * cur[i] + 0.25 * cur[i + 1];
        std::swap(cur, next);
    }
}

}  // namespace

int main(int argc, char** argv) {
    nova_kernel::Args args;
    if (!nova_kernel::parse_args(argc, argv, args)) return 2;
    if (args.n < 3) {
        std::fprintf(stderr, "n must be >= 3\n");
        return 2;
    }
    const std::size_t n = static_cast<std::size_t>(args.n);
    std::vector<double> x(n), y(n, 0.0);
    nova_kernel::Rng rng(args.seed);
    for (std::size_t i = 0; i < n; ++i) x[i] = rng.uniform();

    nova_kernel::escape(x.data());
    double t0 = nova_kernel::now_ms();
    smooth(x.data(), y.data(), n);
    nova_kernel::escape(x.data());
    nova_kernel::escape(y.data());
    double t1 = nova_kernel::now_ms();

    // kSteps is even, so the final state is back in x (cur/next swap kSteps times).
    static_assert(kSteps % 2 == 0, "final state location assumes an even number of sweeps");
    nova_kernel::print_report("stencil", args, x, t1 - t0);
    return 0;
}

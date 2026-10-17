#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <vector>

#ifndef NOVA_N
#define NOVA_N 512
#endif

int main() {
    constexpr int n = NOVA_N;
    std::vector<double> a(static_cast<std::size_t>(n) * n);
    std::vector<double> b(static_cast<std::size_t>(n) * n);
    std::vector<double> c(static_cast<std::size_t>(n) * n, 0.0);

    // Deterministic initialization; no random seed is needed.
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            a[static_cast<std::size_t>(i) * n + j] =
                static_cast<double>((i * 3 + j * 7) % 101) / 101.0;
            b[static_cast<std::size_t>(i) * n + j] =
                static_cast<double>((i * 11 + j * 5) % 103) / 103.0;
        }
    }

    // Reference i-j-k loop order. This is a baseline kernel, not a claim
    // that this source is optimal or representative of all matrix products.
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            double sum = 0.0;
            for (int k = 0; k < n; ++k) {
                sum += a[static_cast<std::size_t>(i) * n + k] *
                       b[static_cast<std::size_t>(k) * n + j];
            }
            c[static_cast<std::size_t>(i) * n + j] = sum;
        }
    }

    // A simple checksum helps confirm that the program ran. For rigorous
    // validation, compare all output values with a trusted reference and
    // a documented tolerance; a checksum alone is not a correctness proof.
    long double checksum = 0.0L;
    for (double value : c) {
        checksum += value;
    }

    std::cout << std::setprecision(17)
              << "N=" << n << " checksum=" << checksum << '\n';
    return 0;
}

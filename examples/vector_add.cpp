#include <cstddef>
#include <iomanip>
#include <iostream>
#include <vector>

#ifndef NOVA_N
#define NOVA_N 10000000
#endif

int main() {
    const std::size_t n = NOVA_N;
    std::vector<double> a(n), b(n), out(n);

    for (std::size_t i = 0; i < n; ++i) {
        a[i] = static_cast<double>(i % 1000) * 0.25;
        b[i] = static_cast<double>(i % 997) * 0.5;
    }

    for (std::size_t i = 0; i < n; ++i) {
        out[i] = a[i] + b[i];
    }

    long double checksum = 0.0L;
    for (double value : out) checksum += value;

    std::cout << std::setprecision(17)
              << "N=" << n << " checksum=" << checksum << '\n';
    return 0;
}

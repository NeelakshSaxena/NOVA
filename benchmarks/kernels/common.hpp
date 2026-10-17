// Shared support for NOVA benchmark kernels. Part of every kernel's source hash.
//
// Kernel contract (see docs/benchmarks.md):
//   kernel <n> <seed>
// prints "key=value" lines on stdout:
//   nova-kernel=1
//   kernel=<name>  n=<n>  seed=<seed>
//   out.sum / out.s0 / out.s1 / out.s2 / out.wsum   (statistics of the output array, %.17g)
//   time_ms=<milliseconds spent in the timed region only>
// Inputs are generated deterministically from <seed> with splitmix64 (spec in docs/benchmarks.md).
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <chrono>
#endif

namespace nova_kernel {

// splitmix64; uniform() is in [0,1) with 53 random bits.
struct Rng {
    std::uint64_t state;
    explicit Rng(std::uint64_t seed) : state(seed) {}
    std::uint64_t next() {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    double uniform() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }
};

// High-resolution monotonic clock in milliseconds.
inline double now_ms() {
#ifdef _WIN32
    LARGE_INTEGER freq, t;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t);
    return 1000.0 * static_cast<double>(t.QuadPart) / static_cast<double>(freq.QuadPart);
#else
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double, std::milli>(clock::now().time_since_epoch()).count();
#endif
}

struct Args {
    long long n;
    std::uint64_t seed;
};

inline bool parse_args(int argc, char** argv, Args& a) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <n> <seed>\n", argv[0]);
        return false;
    }
    char* end = nullptr;
    a.n = std::strtoll(argv[1], &end, 10);
    if (*end != '\0' || a.n <= 0) {
        std::fprintf(stderr, "bad n: %s\n", argv[1]);
        return false;
    }
    a.seed = std::strtoull(argv[2], &end, 10);
    if (*end != '\0') {
        std::fprintf(stderr, "bad seed: %s\n", argv[2]);
        return false;
    }
    return true;
}

// Decimal text of an unsigned 64-bit value (the MinGW C runtime's printf lacks portable %llu support).
inline std::string u64_to_string(std::uint64_t v) {
    char buf[24];
    int pos = 23;
    buf[pos] = '\0';
    do {
        buf[--pos] = static_cast<char>('0' + v % 10);
        v /= 10;
    } while (v != 0);
    return std::string(buf + pos);
}

// Prevents the compiler from discarding or hoisting work whose result is only read later.
inline void escape(const void* p) {
#if defined(__GNUC__)
    asm volatile("" : : "g"(p) : "memory");
#else
    (void)p;
#endif
}

// Statistics of the output array (accumulated in long double, printed as double):
//   sum = sum v[i];  s0 = v[0], s1 = v[len/2], s2 = v[len-1];  wsum = sum ((i % 7) + 1) * v[i]
inline void print_report(const char* kernel, const Args& a, const std::vector<double>& v, double time_ms) {
    long double sum = 0.0L, wsum = 0.0L;
    for (std::size_t i = 0; i < v.size(); ++i) {
        sum += v[i];
        wsum += static_cast<long double>((i % 7) + 1) * v[i];
    }
    std::printf("nova-kernel=1\n");
    std::printf("kernel=%s\n", kernel);
    std::printf("n=%s\n", u64_to_string(static_cast<std::uint64_t>(a.n)).c_str());
    std::printf("seed=%s\n", u64_to_string(a.seed).c_str());
    std::printf("out.len=%s\n", u64_to_string(v.size()).c_str());
    std::printf("out.sum=%.17g\n", static_cast<double>(sum));
    std::printf("out.s0=%.17g\n", v[0]);
    std::printf("out.s1=%.17g\n", v[v.size() / 2]);
    std::printf("out.s2=%.17g\n", v[v.size() - 1]);
    std::printf("out.wsum=%.17g\n", static_cast<double>(wsum));
    std::printf("time_ms=%.9g\n", time_ms);
}

}  // namespace nova_kernel

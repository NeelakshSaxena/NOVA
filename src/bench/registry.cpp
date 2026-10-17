#include "nova/bench/registry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <sstream>

#include "nova/bench/util.hpp"

namespace nova {
namespace bench {

const std::vector<BenchmarkDef>& registry() {
    static const std::vector<BenchmarkDef> defs = {
        {"matmul", "C = A*B, n x n doubles, i-j-k loops (n = matrix dimension)", {256, 384}, {320, 448}},
        {"vector_add", "a[i] += b[i], 16 in-place passes (n = vector length)", {1000000, 4000000}, {2000000, 6000000}},
        {"stencil", "1-D Jacobi smoothing, 100 sweeps (n = number of cells)", {200000, 800000}, {400000, 1600000}},
    };
    return defs;
}

const BenchmarkDef* find_benchmark(const std::string& name) {
    for (const BenchmarkDef& d : registry())
        if (d.name == name) return &d;
    return nullptr;
}

bool parse_kernel_output(const std::string& text, KernelOutput& out, std::string& error) {
    std::map<std::string, std::string> kv;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    auto need = [&](const char* key, std::string& v) {
        auto it = kv.find(key);
        if (it == kv.end()) {
            error = std::string("kernel output is missing '") + key + "'";
            return false;
        }
        v = it->second;
        return true;
    };
    auto num = [&](const char* key, double& v) {
        std::string s;
        if (!need(key, s)) return false;
        char* end = nullptr;
        v = std::strtod(s.c_str(), &end);
        if (s.empty() || *end != '\0' || !std::isfinite(v)) {
            error = std::string("kernel output field '") + key + "' is not a finite number: '" + s + "'";
            return false;
        }
        return true;
    };
    auto u64 = [&](const char* key, std::uint64_t& v) {
        std::string s;
        if (!need(key, s)) return false;
        char* end = nullptr;
        v = std::strtoull(s.c_str(), &end, 10);
        if (s.empty() || *end != '\0') {
            error = std::string("kernel output field '") + key + "' is not an integer: '" + s + "'";
            return false;
        }
        return true;
    };
    std::string marker;
    if (!need("nova-kernel", marker) || marker != "1") {
        if (error.empty()) error = "unsupported kernel protocol version '" + marker + "'";
        return false;
    }
    KernelOutput k;
    std::uint64_t n = 0;
    if (!need("kernel", k.kernel) || !u64("n", n) || !u64("seed", k.seed) || !u64("out.len", k.len) ||
        !num("out.sum", k.sum) || !num("out.s0", k.s0) || !num("out.s1", k.s1) || !num("out.s2", k.s2) ||
        !num("out.wsum", k.wsum) || !num("time_ms", k.time_ms))
        return false;
    k.n = static_cast<std::int64_t>(n);
    if (k.time_ms < 0) {
        error = "kernel reported a negative time";
        return false;
    }
    out = k;
    return true;
}

namespace {

// splitmix64, the input generator specified in docs/benchmarks.md.
struct SplitMix {
    std::uint64_t state;
    double uniform() {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        z ^= z >> 31;
        return static_cast<double>(z >> 11) * (1.0 / 9007199254740992.0);
    }
};

void fill_stats(KernelOutput& r, const std::vector<long double>& v) {
    long double sum = 0, wsum = 0;
    for (size_t i = 0; i < v.size(); ++i) {
        sum += v[i];
        wsum += static_cast<long double>((i % 7) + 1) * v[i];
    }
    r.len = v.size();
    r.sum = static_cast<double>(sum);
    r.wsum = static_cast<double>(wsum);
    r.s0 = static_cast<double>(v[0]);
    r.s1 = static_cast<double>(v[v.size() / 2]);
    r.s2 = static_cast<double>(v[v.size() - 1]);
}

void ref_matmul(std::int64_t n, std::uint64_t seed, std::vector<long double>& out) {
    SplitMix rng{seed};
    size_t count = static_cast<size_t>(n) * static_cast<size_t>(n);
    std::vector<double> a(count), b(count);
    for (double& x : a) x = rng.uniform();
    for (double& x : b) x = rng.uniform();
    out.assign(count, 0.0L);
    size_t N = static_cast<size_t>(n);
    // Accumulate each C[i][j] in long double with k innermost (independent of the kernel's double sum).
    for (size_t i = 0; i < N; ++i)
        for (size_t j = 0; j < N; ++j) {
            long double acc = 0;
            for (size_t k = 0; k < N; ++k) acc += static_cast<long double>(a[i * N + k]) * static_cast<long double>(b[k * N + j]);
            out[i * N + j] = acc;
        }
}

void ref_vector_add(std::int64_t n, std::uint64_t seed, std::vector<long double>& out) {
    SplitMix rng{seed};
    size_t N = static_cast<size_t>(n);
    std::vector<double> a(N), b(N);
    for (double& x : a) x = rng.uniform();
    for (double& x : b) x = rng.uniform();
    out.resize(N);
    // 16 passes of a += b. The kernel adds in double each pass; the reference keeps long double precision.
    for (size_t i = 0; i < N; ++i) {
        long double v = a[i];
        for (int pass = 0; pass < 16; ++pass) v += static_cast<long double>(b[i]);
        out[i] = v;
    }
}

void ref_stencil(std::int64_t n, std::uint64_t seed, std::vector<long double>& out) {
    SplitMix rng{seed};
    size_t N = static_cast<size_t>(n);
    std::vector<long double> cur(N), next(N);
    for (size_t i = 0; i < N; ++i) cur[i] = rng.uniform();
    for (int step = 0; step < 100; ++step) {
        next[0] = cur[0];
        next[N - 1] = cur[N - 1];
        for (size_t i = 1; i + 1 < N; ++i) next[i] = 0.25L * cur[i - 1] + 0.5L * cur[i] + 0.25L * cur[i + 1];
        cur.swap(next);
    }
    out = cur;
}

}  // namespace

bool reference_output(const std::string& benchmark, std::int64_t n, std::uint64_t seed, KernelOutput& out,
                      std::string& error) {
    if (n <= 0 || (benchmark == "stencil" && n < 3)) {
        error = "invalid size " + i64_to_string(n) + " for " + benchmark;
        return false;
    }
    std::vector<long double> v;
    if (benchmark == "matmul") ref_matmul(n, seed, v);
    else if (benchmark == "vector_add") ref_vector_add(n, seed, v);
    else if (benchmark == "stencil") ref_stencil(n, seed, v);
    else {
        error = "unknown benchmark '" + benchmark + "'";
        return false;
    }
    KernelOutput r;
    r.kernel = benchmark;
    r.n = n;
    r.seed = seed;
    fill_stats(r, v);
    out = r;
    return true;
}

std::vector<std::string> compare_to_reference(const KernelOutput& e, const KernelOutput& a, double tol) {
    std::vector<std::string> bad;
    if (a.kernel != e.kernel) bad.push_back("kernel name '" + a.kernel + "' != '" + e.kernel + "'");
    if (a.n != e.n) bad.push_back("n " + i64_to_string(a.n) + " != " + i64_to_string(e.n));
    if (a.seed != e.seed) bad.push_back("seed differs");
    if (a.len != e.len) bad.push_back("out.len " + u64_to_string(a.len) + " != " + u64_to_string(e.len));
    auto cmp = [&](const char* name, double expect, double actual) {
        double limit = tol * std::max(1.0, std::fabs(expect));
        if (!(std::fabs(actual - expect) <= limit)) {
            std::ostringstream os;
            os.precision(17);
            os << name << " = " << actual << ", expected " << expect << " (tolerance " << limit << ")";
            bad.push_back(os.str());
        }
    };
    cmp("out.sum", e.sum, a.sum);
    cmp("out.s0", e.s0, a.s0);
    cmp("out.s1", e.s1, a.s1);
    cmp("out.s2", e.s2, a.s2);
    cmp("out.wsum", e.wsum, a.wsum);
    return bad;
}

}  // namespace bench
}  // namespace nova

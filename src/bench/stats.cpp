#include "nova/bench/stats.hpp"

#include <algorithm>
#include <cmath>

namespace nova {
namespace bench {

double quantile_sorted(const std::vector<double>& sorted, double q) {
    if (sorted.size() == 1) return sorted[0];
    double pos = q * static_cast<double>(sorted.size() - 1);
    size_t lo = static_cast<size_t>(std::floor(pos));
    size_t hi = std::min(lo + 1, sorted.size() - 1);
    double frac = pos - static_cast<double>(lo);
    return sorted[lo] + (sorted[hi] - sorted[lo]) * frac;
}

Summary summarize(const std::vector<double>& samples) {
    Summary s;
    if (samples.empty()) return s;
    std::vector<double> v = samples;
    std::sort(v.begin(), v.end());
    s.count = v.size();
    s.min = v.front();
    s.max = v.back();
    s.median = quantile_sorted(v, 0.5);
    s.q1 = quantile_sorted(v, 0.25);
    s.q3 = quantile_sorted(v, 0.75);
    s.iqr = s.q3 - s.q1;
    double total = 0;
    for (double x : v) total += x;
    s.mean = total / static_cast<double>(v.size());
    std::vector<double> dev;
    for (double x : v) dev.push_back(std::fabs(x - s.median));
    std::sort(dev.begin(), dev.end());
    s.mad = quantile_sorted(dev, 0.5);
    return s;
}

}  // namespace bench
}  // namespace nova

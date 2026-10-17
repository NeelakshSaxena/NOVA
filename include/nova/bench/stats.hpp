#pragma once

#include <cstddef>
#include <vector>

namespace nova {
namespace bench {

struct Summary {
    size_t count = 0;
    double median = 0, q1 = 0, q3 = 0, iqr = 0;  // quartiles use linear interpolation (R type 7)
    double mad = 0;                               // median absolute deviation from the median
    double min = 0, max = 0, mean = 0;
};

// Summary of `samples`. Returns a zeroed Summary (count 0) for an empty input. Never modifies the input order.
Summary summarize(const std::vector<double>& samples);

// Quantile with linear interpolation of an already-sorted, non-empty vector; q in [0, 1].
double quantile_sorted(const std::vector<double>& sorted, double q);

}  // namespace bench
}  // namespace nova

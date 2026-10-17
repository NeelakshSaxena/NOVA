#pragma once

#include <cstdint>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace nova {
namespace bench {

// Decimal conversions that are correct for the full 64-bit range even where `unsigned long` is 32 bits and
// std::to_string(long long) may be unavailable (the MinGW.org toolchain this project is developed on).
inline std::string u64_to_string(std::uint64_t v) {
    if (v == 0) return "0";
    std::string s;
    while (v != 0) {
        s.insert(s.begin(), static_cast<char>('0' + v % 10));
        v /= 10;
    }
    return s;
}

inline std::string i64_to_string(std::int64_t v) {
    if (v >= 0) return u64_to_string(static_cast<std::uint64_t>(v));
    return "-" + u64_to_string(0 - static_cast<std::uint64_t>(v));
}

// Deterministic splitmix64 generator for reproducible orderings and candidate sequences. Its output is fully
// specified (unlike std::shuffle / distributions), so the same seed gives the same sequence everywhere.
class Rng {
public:
    explicit Rng(std::uint64_t seed) : state_(seed) {}
    std::uint64_t next() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    // Uniform integer in [0, n) (n > 0), rejection-free modulo is adequate for the tiny n used here.
    std::uint64_t below(std::uint64_t n) { return next() % n; }
    double uniform() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }

private:
    std::uint64_t state_;
};

// Fisher-Yates shuffle driven by Rng.
template <typename T>
void shuffle(std::vector<T>& v, Rng& rng) {
    for (size_t i = v.size(); i > 1; --i) {
        size_t j = static_cast<size_t>(rng.below(i));
        if (j != i - 1) std::swap(v[i - 1], v[j]);  // swapping an element with itself is a self-move (caught by checked containers)
    }
}

// Whole-file helpers (binary-safe). read_file returns false if the file cannot be opened.
bool read_file(const std::string& path, std::string& contents);
bool write_file(const std::string& path, const std::string& contents);
// Creates `path` and any missing parents. Returns true if the directory exists afterwards.
bool make_dirs(const std::string& path);
bool file_exists(const std::string& path);
// Size in bytes, or -1 if the file cannot be read.
std::int64_t file_size(const std::string& path);
// Current UTC time as "YYYY-MM-DDTHH:MM:SSZ".
std::string utc_timestamp();
// Joins with single spaces.
std::string join(const std::vector<std::string>& parts, const std::string& sep = " ");

}  // namespace bench
}  // namespace nova

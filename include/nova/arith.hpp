#pragma once

#include <cstdint>

namespace nova {

// NOVA Script integer arithmetic (see docs/language.md): 64-bit two's complement, wrapping on overflow,
// division truncates toward zero. These helpers are the single definition of the semantics, shared by
// the interpreters and the optimizer's constant folding.

inline std::int64_t wrap_add(std::int64_t a, std::int64_t b) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) + static_cast<std::uint64_t>(b));
}
inline std::int64_t wrap_sub(std::int64_t a, std::int64_t b) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) - static_cast<std::uint64_t>(b));
}
inline std::int64_t wrap_mul(std::int64_t a, std::int64_t b) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b));
}
inline std::int64_t wrap_neg(std::int64_t a) {
    return static_cast<std::int64_t>(0 - static_cast<std::uint64_t>(a));
}

// Returns false (and leaves `out` untouched) when b == 0. INT64_MIN / -1 wraps to INT64_MIN.
inline bool checked_div(std::int64_t a, std::int64_t b, std::int64_t& out) {
    if (b == 0) return false;
    if (b == -1) {
        out = wrap_neg(a);
        return true;
    }
    out = a / b;
    return true;
}

}  // namespace nova

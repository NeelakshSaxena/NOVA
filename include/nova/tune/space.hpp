#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace nova {
namespace tune {

// SmartPass search space: GCC optimization flags for the benchmark kernels. NOVA does not compile the kernels;
// GCC does. Candidates are built only from a closed whitelist of flags, so nothing a user types can reach the
// compiler command line unchecked (and arguments are passed as an array, never through a shell).

class Whitelist {
public:
    explicit Whitelist(std::set<std::string> flags) : flags_(std::move(flags)) {}
    // The production whitelist (see docs/smartpass.md for why each flag is included).
    static const Whitelist& production();
    bool allows(const std::string& flag) const { return flags_.count(flag) != 0; }
    const std::set<std::string>& flags() const { return flags_; }

private:
    std::set<std::string> flags_;
};

struct Candidate {
    std::string id;                  // stable, human readable: "O2", "O3+unroll+native", ...
    std::vector<std::string> flags;  // canonical order: one -O level first, then the other flags sorted
    bool baseline = false;           // a plain GCC level with no extra flags
};

// Checks `flags`: every flag whitelisted, no duplicates, exactly one -O level. Returns false and sets `error`.
bool validate_flags(const std::vector<std::string>& flags, const Whitelist& whitelist, std::string& error);

// Canonical form: the -O flag first, redundant flags removed (-ftree-vectorize is implied by -O3), the rest sorted.
std::vector<std::string> canonicalize(const std::vector<std::string>& flags);

struct SearchSpace {
    std::vector<Candidate> baselines;   // always evaluated, never counted against the budget
    std::vector<Candidate> candidates;  // the searchable configurations (canonical, unique, excluding baselines)
};

// The default space: opt level {O2,O3} x unroll x vectorize x prefetch x arch(native), canonicalized and
// de-duplicated, minus the two bare baselines. In enumeration order (opt, then unroll, vectorize, prefetch, native).
SearchSpace default_space();

// Builds a space from explicit flag lists (used by tests and by `nova tune` extensions). Every candidate is
// validated against `whitelist` first; returns false and sets `error` on the first invalid one (no process
// has been run at that point).
bool make_space(const std::vector<std::vector<std::string>>& baseline_flags,
                const std::vector<std::vector<std::string>>& candidate_flags, const Whitelist& whitelist,
                SearchSpace& out, std::string& error);

enum class Strategy { Fixed, Random, Adaptive };
const char* to_string(Strategy s);
bool parse_strategy(const std::string& text, Strategy& out);

// The ordered list of candidates a strategy evaluates under budget `budget` (the number of search
// candidates, excluding baselines). Deterministic: the same space, strategy, seed and budget always give the
// same sequence. Fixed = the first `budget` candidates in enumeration order; Random = the first `budget` of a
// seeded Fisher-Yates permutation (sampling without replacement). Never returns more than min(budget, size).
// Adaptive sequences depend on measured outcomes and are produced online by AdaptivePolicy (adaptive.hpp); for
// Strategy::Adaptive this function returns an empty list.
std::vector<Candidate> select_sequence(const SearchSpace& space, Strategy strategy, std::uint64_t seed, int budget);

}  // namespace tune
}  // namespace nova

#include "nova/tune/space.hpp"

#include <algorithm>

#include "nova/bench/util.hpp"

namespace nova {
namespace tune {

const Whitelist& Whitelist::production() {
    static const Whitelist w({"-O2", "-O3", "-funroll-loops", "-ftree-vectorize", "-fprefetch-loop-arrays",
                              "-march=native"});
    return w;
}

namespace {

bool is_opt_level(const std::string& f) { return f.size() == 3 && f[0] == '-' && f[1] == 'O'; }

std::string short_name(const std::string& flag) {
    if (flag == "-funroll-loops") return "unroll";
    if (flag == "-ftree-vectorize") return "vec";
    if (flag == "-fprefetch-loop-arrays") return "prefetch";
    if (flag == "-march=native") return "native";
    if (is_opt_level(flag)) return flag.substr(1);
    return flag;
}

Candidate make_candidate(const std::vector<std::string>& flags) {
    Candidate c;
    c.flags = canonicalize(flags);
    for (size_t i = 0; i < c.flags.size(); ++i) {
        if (i) c.id += "+";
        c.id += short_name(c.flags[i]);
    }
    c.baseline = c.flags.size() == 1;  // a bare optimization level
    return c;
}

}  // namespace

bool validate_flags(const std::vector<std::string>& flags, const Whitelist& whitelist, std::string& error) {
    std::set<std::string> seen;
    int levels = 0;
    for (const std::string& f : flags) {
        if (!whitelist.allows(f)) {
            error = "flag '" + f + "' is not in the whitelist";
            return false;
        }
        if (!seen.insert(f).second) {
            error = "duplicate flag '" + f + "'";
            return false;
        }
        if (is_opt_level(f)) ++levels;
    }
    if (levels != 1) {
        error = "exactly one -O level is required (found " + std::to_string(levels) + ")";
        return false;
    }
    return true;
}

std::vector<std::string> canonicalize(const std::vector<std::string>& flags) {
    std::string level;
    std::vector<std::string> rest;
    for (const std::string& f : flags) {
        if (is_opt_level(f)) level = f;
        else rest.push_back(f);
    }
    if (level == "-O3") rest.erase(std::remove(rest.begin(), rest.end(), "-ftree-vectorize"), rest.end());  // implied
    std::sort(rest.begin(), rest.end());
    rest.erase(std::unique(rest.begin(), rest.end()), rest.end());
    std::vector<std::string> out;
    if (!level.empty()) out.push_back(level);
    out.insert(out.end(), rest.begin(), rest.end());
    return out;
}

SearchSpace default_space() {
    SearchSpace s;
    s.baselines.push_back(make_candidate({"-O2"}));
    s.baselines.push_back(make_candidate({"-O3"}));
    std::set<std::string> seen;
    for (const char* opt : {"-O2", "-O3"})
        for (int unroll = 0; unroll < 2; ++unroll)
            for (int vec = 0; vec < 2; ++vec)
                for (int prefetch = 0; prefetch < 2; ++prefetch)
                    for (int native = 0; native < 2; ++native) {
                        std::vector<std::string> f = {opt};
                        if (unroll) f.push_back("-funroll-loops");
                        if (vec) f.push_back("-ftree-vectorize");
                        if (prefetch) f.push_back("-fprefetch-loop-arrays");
                        if (native) f.push_back("-march=native");
                        Candidate c = make_candidate(f);
                        if (c.baseline) continue;
                        if (seen.insert(c.id).second) s.candidates.push_back(c);
                    }
    return s;
}

bool make_space(const std::vector<std::vector<std::string>>& baseline_flags,
                const std::vector<std::vector<std::string>>& candidate_flags, const Whitelist& whitelist,
                SearchSpace& out, std::string& error) {
    SearchSpace s;
    std::set<std::string> ids;
    auto add = [&](const std::vector<std::string>& flags, bool baseline) {
        std::string why;
        if (!validate_flags(flags, whitelist, why)) {
            error = "invalid candidate '" + nova::bench::join(flags) + "': " + why;
            return false;
        }
        Candidate c = make_candidate(flags);
        c.baseline = baseline;
        if (!ids.insert(c.id).second) {
            error = "duplicate candidate '" + c.id + "'";
            return false;
        }
        (baseline ? s.baselines : s.candidates).push_back(c);
        return true;
    };
    for (const auto& f : baseline_flags)
        if (!add(f, true)) return false;
    for (const auto& f : candidate_flags)
        if (!add(f, false)) return false;
    out = s;
    return true;
}

const char* to_string(Strategy s) {
    switch (s) {
        case Strategy::Fixed: return "fixed";
        case Strategy::Random: return "random";
        case Strategy::Adaptive: return "adaptive";
    }
    return "?";
}

bool parse_strategy(const std::string& text, Strategy& out) {
    if (text == "fixed") out = Strategy::Fixed;
    else if (text == "random") out = Strategy::Random;
    else if (text == "adaptive") out = Strategy::Adaptive;
    else return false;
    return true;
}

std::vector<Candidate> select_sequence(const SearchSpace& space, Strategy strategy, std::uint64_t seed, int budget) {
    std::vector<Candidate> pool = space.candidates;
    if (budget <= 0 || strategy == Strategy::Adaptive) return {};
    if (strategy == Strategy::Random) {
        nova::bench::Rng rng(seed);
        nova::bench::shuffle(pool, rng);
    }
    if (static_cast<size_t>(budget) < pool.size()) pool.resize(static_cast<size_t>(budget));
    return pool;
}

}  // namespace tune
}  // namespace nova

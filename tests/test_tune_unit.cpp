#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "check.hpp"
#include "nova/tune/space.hpp"
#include "nova/tune/tuner.hpp"

using namespace nova::tune;
using nova::bench::Json;
using nova::bench::RunRecord;

namespace {

std::vector<std::string> ids(const std::vector<Candidate>& v) {
    std::vector<std::string> r;
    for (const Candidate& c : v) r.push_back(c.id);
    return r;
}

void test_whitelist_and_validation() {
    const Whitelist& w = Whitelist::production();
    for (const char* ok : {"-O2", "-O3", "-funroll-loops", "-ftree-vectorize", "-fprefetch-loop-arrays", "-march=native"})
        CHECK_MSG(w.allows(ok), std::string(ok) + " is whitelisted");
    // Semantically risky or unlisted flags are NOT whitelisted.
    for (const char* bad : {"-Ofast", "-ffast-math", "-O0", "-O1", "-Os", "-fno-such-flag", "-DX=1", "-o", "-x", "-include",
                            "-Wl,--wrap=main", "-fplugin=evil.so", "-O2 -O3", "-O2;rm", "-O2&&echo", "$(id)", "", "--version", "-fopenmp"})
        CHECK_MSG(!w.allows(bad), std::string("'") + bad + "' is not whitelisted");

    std::string err;
    CHECK(validate_flags({"-O2"}, w, err));
    CHECK(validate_flags({"-O3", "-funroll-loops", "-march=native"}, w, err));
    CHECK(!validate_flags({"-O2", "-ffast-math"}, w, err));
    CHECK(err.find("-ffast-math") != std::string::npos);
    CHECK(!validate_flags({"-O2", "-O2"}, w, err));      // duplicate
    CHECK(!validate_flags({"-O2", "-O3"}, w, err));      // two levels
    CHECK(!validate_flags({"-funroll-loops"}, w, err));  // no level
    CHECK(!validate_flags({}, w, err));
    CHECK(!validate_flags({"-O2", "-funroll-loops; echo pwned"}, w, err));
    CHECK(!validate_flags({"-O2", "-Wl,-Map=x"}, w, err));
}

void test_canonicalization() {
    CHECK((canonicalize({"-march=native", "-O2", "-funroll-loops"}) == std::vector<std::string>{"-O2", "-funroll-loops", "-march=native"}));
    // -ftree-vectorize is implied by -O3 and removed there, but kept at -O2.
    CHECK((canonicalize({"-O3", "-ftree-vectorize"}) == std::vector<std::string>{"-O3"}));
    CHECK((canonicalize({"-O2", "-ftree-vectorize"}) == std::vector<std::string>{"-O2", "-ftree-vectorize"}));
    CHECK((canonicalize({"-O2", "-funroll-loops", "-funroll-loops"}) == std::vector<std::string>{"-O2", "-funroll-loops"}));
}

void test_default_space() {
    SearchSpace s = default_space();
    CHECK_EQ(s.baselines.size(), size_t(2));
    CHECK_EQ(s.baselines[0].id, std::string("O2"));
    CHECK_EQ(s.baselines[1].id, std::string("O3"));
    CHECK(s.baselines[0].baseline && s.baselines[1].baseline);
    // 15 distinct non-baseline -O2 configs + 7 distinct -O3 configs
    CHECK_EQ(s.candidates.size(), size_t(22));
    std::set<std::string> seen;
    const Whitelist& w = Whitelist::production();
    for (const Candidate& c : s.candidates) {
        CHECK_MSG(seen.insert(c.id).second, "unique id " + c.id);
        CHECK_MSG(!c.baseline, c.id + " is not a baseline");
        std::string err;
        CHECK_MSG(validate_flags(c.flags, w, err), c.id + " passes validation: " + err);
        CHECK_MSG(c.flags.size() >= 2, c.id + " differs from a bare level");
        CHECK_MSG(canonicalize(c.flags) == c.flags, c.id + " is canonical");
    }
    // No candidate repeats a baseline's flags.
    for (const Candidate& c : s.candidates) CHECK(c.id != "O2" && c.id != "O3");
    // Deterministic enumeration.
    CHECK(ids(default_space().candidates) == ids(s.candidates));
    // No unsafe semantics anywhere in the space.
    for (const Candidate& c : s.candidates)
        for (const std::string& f : c.flags) CHECK_MSG(f.find("fast-math") == std::string::npos && f != "-Ofast", "no fast-math in " + c.id);
}

void test_make_space_rejects_invalid() {
    SearchSpace out;
    std::string err;
    const Whitelist& w = Whitelist::production();
    CHECK(make_space({{"-O2"}, {"-O3"}}, {{"-O2", "-funroll-loops"}}, w, out, err));
    CHECK_EQ(out.baselines.size(), size_t(2));
    CHECK_EQ(out.candidates.size(), size_t(1));
    struct Case { std::vector<std::string> flags; const char* what; };
    const Case bad[] = {
        {{"-O2", "-Ofast"}, "unlisted -Ofast"}, {{"-O2", "-fno-such-flag"}, "unknown flag"},
        {{"-O2", "-funroll-loops; rm -rf /"}, "shell metacharacters"}, {{"-O2", "-O2"}, "duplicate"},
        {{"-funroll-loops"}, "no -O level"}, {{"-O2", "-O3"}, "two -O levels"}, {{"-O2", "-DX"}, "macro definition"},
    };
    for (const Case& c : bad) {
        SearchSpace s;
        std::string e;
        CHECK_MSG(!make_space({{"-O2"}}, {c.flags}, w, s, e), std::string("rejects ") + c.what);
        CHECK_MSG(e.find("invalid candidate") != std::string::npos, std::string("error explains: ") + e);
    }
    SearchSpace dup;
    CHECK(!make_space({{"-O2"}}, {{"-O3"}, {"-O3"}}, w, dup, err));  // duplicate ids
    CHECK(!make_space({{"-O2"}, {"-O2"}}, {}, w, dup, err));
}

void test_sequences() {
    SearchSpace s = default_space();
    const size_t n = s.candidates.size();
    // Fixed: a prefix of the enumeration order; independent of the seed.
    for (int b : {0, 1, 5, 22, 100}) {
        std::vector<Candidate> f1 = select_sequence(s, Strategy::Fixed, 1, b);
        std::vector<Candidate> f2 = select_sequence(s, Strategy::Fixed, 999, b);
        CHECK_EQ(f1.size(), static_cast<size_t>(std::min<size_t>(b, n)));
        CHECK(ids(f1) == ids(f2));
        for (size_t i = 0; i < f1.size(); ++i) CHECK_EQ(f1[i].id, s.candidates[i].id);
    }
    // Random: deterministic per seed, sampling without replacement, never above the budget.
    for (int b : {0, 1, 3, 8, 22, 50}) {
        std::vector<Candidate> r1 = select_sequence(s, Strategy::Random, 42, b);
        std::vector<Candidate> r2 = select_sequence(s, Strategy::Random, 42, b);
        CHECK_MSG(ids(r1) == ids(r2), "same seed, same sequence (budget " + std::to_string(b) + ")");
        CHECK_MSG(r1.size() == std::min<size_t>(static_cast<size_t>(b), n), "never exceeds the budget");
        std::vector<std::string> id_list = ids(r1);
        std::set<std::string> unique(id_list.begin(), id_list.end());
        CHECK_MSG(unique.size() == r1.size(), "no candidate evaluated twice");
        for (const Candidate& c : r1) CHECK(!c.baseline);
    }
    // Prefix property: a bigger budget extends a smaller one for the same seed (so budgets are comparable).
    std::vector<std::string> small = ids(select_sequence(s, Strategy::Random, 7, 5));
    std::vector<std::string> big = ids(select_sequence(s, Strategy::Random, 7, 12));
    CHECK(std::equal(small.begin(), small.end(), big.begin()));
    // Different seeds give different orders (checked on a few seeds; a collision of all would be astronomically unlikely).
    int differing = 0;
    for (std::uint64_t seed = 1; seed <= 5; ++seed)
        if (ids(select_sequence(s, Strategy::Random, seed, 22)) != ids(select_sequence(s, Strategy::Random, seed + 100, 22))) ++differing;
    CHECK_EQ(differing, 5);
    CHECK(ids(select_sequence(s, Strategy::Random, 3, 22)) != ids(select_sequence(s, Strategy::Fixed, 3, 22)));
    // Both strategies contain exactly the same set when the budget covers the space.
    std::vector<std::string> a = ids(select_sequence(s, Strategy::Random, 3, 22)), b = ids(select_sequence(s, Strategy::Fixed, 3, 22));
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    CHECK(a == b);
    Strategy st;
    CHECK(parse_strategy("fixed", st) && st == Strategy::Fixed);
    CHECK(parse_strategy("random", st) && st == Strategy::Random);
    CHECK(parse_strategy("adaptive", st) && st == Strategy::Adaptive);
    CHECK(!parse_strategy("bayesian", st));  // no such strategy
    CHECK(!parse_strategy("", st));
    // adaptive sequences are produced online by AdaptivePolicy, never by select_sequence
    CHECK(select_sequence(s, Strategy::Adaptive, 1, 5).empty());
}

RunRecord rec(const std::string& id, const std::string& life, double median, int rank = 0, bool selected = false,
              const std::string& strategy = "random") {
    RunRecord r;
    r.experiment_id = "e";
    r.benchmark = "stencil";
    r.source_hash = std::string(64, 'a');
    r.compiler_version = "g++ test";
    r.flags = "-O2";
    r.opt_level = "O2";
    r.fp_semantics = "strict";
    r.input_size = 100;
    r.size_role = "adhoc";
    r.seed = 1;
    r.candidate_id = id;
    r.build_status = "OK";
    r.correctness_status = "PASS";
    r.warmup_runs = 1;
    r.repetitions = 3;
    r.timing_samples_ms = {median, median, median};
    r.binary_size_bytes = 1000;
    r.search_strategy = strategy;
    r.lifecycle = life;
    r.rank = rank;
    r.selected = selected;
    nova::bench::compute_statistics(r);
    return r;
}

void test_explain_text() {
    Json manifest = Json::object();
    manifest.set("benchmark", Json::string("stencil"));
    manifest.set("experiment_id", Json::string("e"));
    Json search = Json::object();
    search.set("strategy", Json::string("random"));
    search.set("search_seed", Json::string("5"));
    search.set("budget", Json::integer(3));
    search.set("evaluations", Json::integer(3));
    search.set("builds_attempted", Json::integer(5));
    search.set("tuning_size", Json::integer(100));
    manifest.set("search", search);
    std::vector<RunRecord> rs;
    rs.push_back(rec("O2", "ELIGIBLE", 10.0, 3, false, "baseline"));
    rs.push_back(rec("O3", "ELIGIBLE", 8.0, 2, false, "baseline"));
    rs.push_back(rec("O3+unroll", "ELIGIBLE", 5.0, 1, true));
    RunRecord bad = rec("O2+x", "CORRECTNESS_FAILED", 1.0);
    bad.correctness_status = "FAIL";
    bad.failure_reason = "output differs from the reference";
    bad.timing_samples_ms.clear();
    nova::bench::compute_statistics(bad);
    rs.push_back(bad);
    std::string text = explain_tune(manifest, rs);
    CHECK(text.find("WINNER") != std::string::npos);
    CHECK(text.find("CORRECTNESS_FAILED") != std::string::npos);
    CHECK(text.find("output differs from the reference") != std::string::npos);
    CHECK(text.find("winner: O3+unroll") != std::string::npos);
    CHECK(text.find("best baseline: O3") != std::string::npos);
    CHECK(text.find("ratio of medians 1.600") != std::string::npos);
    // The failing candidate is listed and not ranked.
    CHECK(text.find("O2+x") != std::string::npos);
    // No eligible candidate -> an explicit statement.
    std::vector<RunRecord> none = {bad};
    std::string t2 = explain_tune(manifest, none);
    CHECK(t2.find("NO ELIGIBLE CANDIDATE") != std::string::npos);
}

}  // namespace

int main() {
    test_whitelist_and_validation();
    test_canonicalization();
    test_default_space();
    test_make_space_rejects_invalid();
    test_sequences();
    test_explain_text();
    return nova_test::finish("tune-unit");
}

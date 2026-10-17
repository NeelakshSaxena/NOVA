#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "check.hpp"
#include "nova/bench/util.hpp"
#include "nova/tune/adaptive.hpp"
#include "nova/tune/features.hpp"

using namespace nova::tune;

namespace {

int index_of(const SearchSpace& s, const std::string& id) {
    for (size_t i = 0; i < s.candidates.size(); ++i)
        if (s.candidates[i].id == id) return static_cast<int>(i);
    return -1;
}

void test_features() {
    WorkloadFeatures m = extract_features("matmul", 256);
    CHECK(m.known);
    CHECK_EQ(m.working_set_bytes, 3.0 * 256 * 256 * 8);
    CHECK(std::fabs(m.flops_per_byte - 256.0 / 12.0) < 1e-9);
    CHECK(m.innermost_reduction && !m.unit_stride_streaming);
    CHECK_EQ(m.loop_nest_depth, 3);
    CHECK(std::fabs(m.size_log2 - 8.0) < 1e-9);
    WorkloadFeatures v = extract_features("vector_add", 1000000);
    CHECK(v.known && v.unit_stride_streaming && !v.innermost_reduction);
    CHECK_EQ(v.working_set_bytes, 16000000.0);
    CHECK(std::fabs(v.flops_per_byte - 1.0) < 1e-12);
    WorkloadFeatures s = extract_features("stencil", 200000);
    CHECK(s.known && s.unit_stride_streaming);
    CHECK(std::fabs(s.flops_per_byte - 31.25) < 1e-12);
    // deterministic
    WorkloadFeatures m2 = extract_features("matmul", 256);
    CHECK(m2.working_set_bytes == m.working_set_bytes && m2.flops_per_byte == m.flops_per_byte && m2.size_log2 == m.size_log2);
    // unknown inputs are explicit, not guessed
    for (const WorkloadFeatures& u : {extract_features("nope", 100), extract_features("matmul", 0), extract_features("matmul", -5), extract_features("", 10)}) {
        CHECK(!u.known);
        CHECK(u.working_set_bytes == 0 && u.flops_per_byte == 0 && !u.innermost_reduction && !u.unit_stride_streaming);
    }
}

void test_flag_features() {
    SearchSpace s = default_space();
    FlagFeatures a = flag_features(s.candidates[static_cast<size_t>(index_of(s, "O2+unroll"))]);
    CHECK(a.unroll && !a.vec && !a.o3 && !a.prefetch && !a.native);
    FlagFeatures b = flag_features(s.candidates[static_cast<size_t>(index_of(s, "O3+native"))]);
    CHECK(b.o3 && b.vec && b.native);  // vectorization implied by -O3
    FlagFeatures c = flag_features(s.candidates[static_cast<size_t>(index_of(s, "O2+vec"))]);
    CHECK(c.vec && !c.o3);
}

void test_prior_depends_on_workload() {
    SearchSpace s = default_space();
    Ablation full;
    AdaptivePolicy stencil(s, extract_features("stencil", 800000), {}, full, 1);
    AdaptivePolicy matmul(s, extract_features("matmul", 256), {}, full, 1);
    AdaptivePolicy unknown(s, extract_features("nope", 10), {}, full, 1);
    int vec = index_of(s, "O2+vec");
    int unroll = index_of(s, "O2+unroll");
    CHECK(std::fabs(stencil.prior(vec) - 0.20) < 1e-12);       // streaming + no reduction -> full vectorization prior
    CHECK(std::fabs(matmul.prior(vec) - 0.05) < 1e-12);        // reduction: 0.25 x 0.20
    CHECK(std::fabs(stencil.prior(unroll) - 0.10) < 1e-12);
    CHECK(std::fabs(matmul.prior(unroll) - 0.05) < 1e-12);
    CHECK(stencil.prior(index_of(s, "O2+prefetch")) > matmul.prior(index_of(s, "O2+prefetch")));  // 0.1 vs 0.03 (matmul 256 fits in cache terms)
    for (size_t i = 0; i < s.candidates.size(); ++i) CHECK(unknown.prior(static_cast<int>(i)) == 0.0);
    // priors are in a sane range and monotone in the number of helpful flags
    CHECK(stencil.prior(index_of(s, "O3+prefetch+unroll+native")) > stencil.prior(index_of(s, "O3+unroll")));
    // history and feedback start at zero with no data
    for (size_t i = 0; i < s.candidates.size(); ++i) {
        CHECK(stencil.history(static_cast<int>(i)) == 0.0);
        CHECK(stencil.feedback(static_cast<int>(i)) == 0.0);
    }
}

HistoryEntry hist(const std::string& bench, std::int64_t size, const std::string& cand, double g, bool failed = false, const char* role = "tuning") {
    HistoryEntry h;
    h.benchmark = bench;
    h.size = size;
    h.size_role = role;
    h.candidate = cand;
    h.log_gain = g;
    h.failed = failed;
    h.source = "t";
    return h;
}

void test_history_term() {
    SearchSpace s = default_space();
    Ablation full;
    WorkloadFeatures w = extract_features("stencil", 200000);
    std::vector<HistoryEntry> h = {hist("stencil", 200000, "O3+unroll", 0.5)};
    AdaptivePolicy p(s, w, h, full, 1);
    int i = index_of(s, "O3+unroll");
    CHECK(std::fabs(p.history(i) - 0.5 * 1.0 / (1.0 + 1.0)) < 1e-12);  // w = 1 (same benchmark and size), shrunk by +1
    CHECK(p.history(index_of(s, "O2+unroll")) == 0.0);                  // other candidates: no information
    // another family transfers weakly
    AdaptivePolicy q(s, w, {hist("matmul", 256, "O3+unroll", 0.5)}, full, 1);
    CHECK(std::fabs(q.history(i) - 0.5 * 0.25 / (0.25 + 1.0)) < 1e-12);
    // same family, farther size: smaller weight
    AdaptivePolicy r(s, w, {hist("stencil", 800000, "O3+unroll", 0.5)}, full, 1);
    double wt = std::exp(-2.0 / 4.0);
    CHECK(std::fabs(r.history(i) - 0.5 * wt / (wt + 1.0)) < 1e-9);
    // a failure in the history is penalized
    AdaptivePolicy f(s, w, {hist("stencil", 200000, "O3+unroll", 0.0, true)}, full, 1);
    CHECK(f.history(i) < 0);
    // history that is not tuning-role is ignored by the policy itself, not only by the filter
    AdaptivePolicy hold(s, w, {hist("stencil", 400000, "O3+unroll", 9.0, false, "holdout")}, full, 1);
    CHECK(hold.history(i) == 0.0);
    // ablation removes it entirely
    Ablation no_hist;
    no_hist.use_history = false;
    AdaptivePolicy z(s, w, h, no_hist, 1);
    CHECK(z.history(i) == 0.0);
}

void test_feedback_term() {
    SearchSpace s = default_space();
    Ablation full;
    AdaptivePolicy p(s, extract_features("nope", 1), {}, full, 1);  // unknown workload: only feedback + ties
    int a = p.next();
    p.observe(a, true, 0.4);
    const Candidate& ca = s.candidates[static_cast<size_t>(a)];
    // The candidate at Hamming distance 1 scores higher than one at the maximum distance.
    int near = -1, far = -1, far_d = -1;
    for (size_t i = 0; i < s.candidates.size(); ++i) {
        if (static_cast<int>(i) == a) continue;
        FlagFeatures x = flag_features(ca), y = flag_features(s.candidates[i]);
        int d = (x.o3 != y.o3) + (x.unroll != y.unroll) + (x.vec != y.vec) + (x.prefetch != y.prefetch) + (x.native != y.native);
        if (d == 1 && near < 0) near = static_cast<int>(i);
        if (d > far_d) {
            far_d = d;
            far = static_cast<int>(i);
        }
    }
    CHECK(near >= 0 && far >= 0 && far_d >= 3);
    CHECK(p.feedback(near) > p.feedback(far));
    CHECK(p.feedback(near) > 0 && p.feedback(far) > 0);  // a positive outcome lifts every neighbour a little
    CHECK(std::fabs(p.feedback(near) - 0.4 * std::exp(-1.0) / (std::exp(-1.0) + 1.0)) < 1e-12);
    // A failed outcome drags neighbours down.
    AdaptivePolicy q(s, extract_features("nope", 1), {}, full, 1);
    int b = q.next();
    q.observe(b, false, 123.0);  // log_gain is ignored for failures
    for (size_t i = 0; i < s.candidates.size(); ++i)
        if (static_cast<int>(i) != b) CHECK(q.feedback(static_cast<int>(i)) < 0);
    // ablation
    Ablation nof;
    nof.use_feedback = false;
    AdaptivePolicy z(s, extract_features("nope", 1), {}, nof, 1);
    int c = z.next();
    z.observe(c, true, 5.0);
    for (size_t i = 0; i < s.candidates.size(); ++i) CHECK(z.feedback(static_cast<int>(i)) == 0.0);
}

// Drives a policy for `steps` picks with outcome function `gain(candidate_id)` and returns the picked ids.
std::vector<std::string> drive(const SearchSpace& s, const WorkloadFeatures& w, const std::vector<HistoryEntry>& h, const Ablation& a,
                               std::uint64_t seed, int steps, double (*gain)(const std::string&)) {
    AdaptivePolicy p(s, w, h, a, seed);
    std::vector<std::string> picks;
    for (int i = 0; i < steps; ++i) {
        int idx = p.next();
        if (idx < 0) break;
        picks.push_back(s.candidates[static_cast<size_t>(idx)].id);
        p.observe(idx, true, gain(s.candidates[static_cast<size_t>(idx)].id));
    }
    return picks;
}

double gain_unroll_good(const std::string& id) { return id.find("unroll") != std::string::npos ? 0.4 : -0.1; }
double gain_prefetch_good(const std::string& id) { return id.find("prefetch") != std::string::npos ? 0.4 : -0.1; }

void test_sequences_and_ablations() {
    SearchSpace s = default_space();
    Ablation full, no_f, no_h, no_fb;
    CHECK(parse_ablation("none", full));
    CHECK(parse_ablation("no-features", no_f));
    CHECK(parse_ablation("no-history", no_h));
    CHECK(parse_ablation("no-feedback", no_fb));
    Ablation bad;
    CHECK(!parse_ablation("no-everything", bad));
    CHECK_EQ(ablation_name(no_f), std::string("no-features"));
    CHECK_EQ(ablation_name(full), std::string("none"));
    WorkloadFeatures wm = extract_features("matmul", 256), ws = extract_features("stencil", 800000);

    // Determinism and no repeats.
    std::vector<std::string> a1 = drive(s, ws, {}, full, 5, 22, gain_unroll_good), a2 = drive(s, ws, {}, full, 5, 22, gain_unroll_good);
    CHECK(a1 == a2);
    CHECK_EQ(a1.size(), size_t(22));
    std::set<std::string> uniq(a1.begin(), a1.end());
    CHECK_EQ(uniq.size(), size_t(22));  // covers the whole space exactly once, then next() would return -1
    AdaptivePolicy done(s, ws, {}, full, 5);
    for (int i = 0; i < 22; ++i) CHECK(done.next() >= 0);
    CHECK_EQ(done.next(), -1);
    CHECK_EQ(done.order().size(), size_t(22));

    // no-features: the workload does not matter (same seed, same outcomes) ...
    CHECK(drive(s, wm, {}, no_f, 3, 10, gain_unroll_good) == drive(s, ws, {}, no_f, 3, 10, gain_unroll_good));
    // ... while with features the first picks do depend on the workload.
    CHECK(drive(s, wm, {}, full, 3, 5, gain_unroll_good) != drive(s, ws, {}, full, 3, 5, gain_unroll_good));
    // The declared signal is the only difference: removing it from "full" on a workload with no useful prior (unknown)
    // changes nothing.
    WorkloadFeatures wu = extract_features("nope", 1);
    CHECK(drive(s, wu, {}, full, 3, 10, gain_unroll_good) == drive(s, wu, {}, no_f, 3, 10, gain_unroll_good));

    // no-feedback: outcomes do not matter.
    CHECK(drive(s, ws, {}, no_fb, 3, 10, gain_unroll_good) == drive(s, ws, {}, no_fb, 3, 10, gain_prefetch_good));
    // with feedback they do.
    CHECK(drive(s, wu, {}, full, 3, 10, gain_unroll_good) != drive(s, wu, {}, full, 3, 10, gain_prefetch_good));
    // ... and feedback steers towards the good flag: after the first pick, "unroll" candidates dominate the next picks.
    std::vector<std::string> steer = drive(s, wu, {}, full, 3, 8, gain_unroll_good);
    int with_unroll = 0;
    for (size_t i = 1; i < steer.size(); ++i) with_unroll += steer[i].find("unroll") != std::string::npos;
    CHECK_MSG(with_unroll >= 5, "feedback concentrates the search near the good flag (" + std::to_string(with_unroll) + " of 7)");

    // history: changes the sequence when used, is ignored by no-history.
    std::vector<HistoryEntry> h = {hist("matmul", 384, "O2+native", 0.8), hist("matmul", 384, "O3+prefetch", 0.7)};
    CHECK(drive(s, wm, h, full, 3, 4, gain_unroll_good) != drive(s, wm, {}, full, 3, 4, gain_unroll_good));
    CHECK(drive(s, wm, h, no_h, 3, 4, gain_unroll_good) == drive(s, wm, {}, no_h, 3, 4, gain_unroll_good));
    // with a strong history entry and nothing else, that candidate is picked first
    Ablation hist_only = full;
    hist_only.use_features = false;
    hist_only.use_feedback = false;
    CHECK_EQ(drive(s, wm, {hist("matmul", 256, "O3+unroll+native", 0.9)}, hist_only, 1, 1, gain_unroll_good)[0], std::string("O3+unroll+native"));

    // Different seeds break ties differently (first pick with no information is the random key's minimum).
    std::set<std::string> firsts;
    for (std::uint64_t seed = 1; seed <= 12; ++seed) firsts.insert(drive(s, wu, {}, no_f, seed, 1, gain_unroll_good)[0]);
    CHECK_MSG(firsts.size() >= 4, "seeds spread the first pick over several candidates");
    // Holdout-role history cannot change a sequence (leakage check at the policy level).
    std::vector<HistoryEntry> leak = {hist("matmul", 320, "O3+unroll+native", 5.0, false, "holdout")};
    CHECK(drive(s, wm, leak, full, 2, 6, gain_unroll_good) == drive(s, wm, {}, full, 2, 6, gain_unroll_good));
}

void test_history_store() {
    std::vector<HistoryEntry> e = {hist("matmul", 256, "O2+unroll", 0.1), hist("matmul", 320, "O2+unroll", 0.9, false, "holdout"),
                                   hist("stencil", 200000, "O3", 0.2), hist("matmul", 500, "O2", 0.0, false, "adhoc")};
    // filter: tuning only; optionally drop the same workload
    std::vector<HistoryEntry> f = filter_history(e, "matmul", 256, false);
    CHECK_EQ(f.size(), size_t(2));
    for (const HistoryEntry& x : f) CHECK(x.size_role == "tuning");
    std::vector<HistoryEntry> g = filter_history(e, "matmul", 256, true);
    CHECK_EQ(g.size(), size_t(1));
    CHECK_EQ(g[0].benchmark, std::string("stencil"));
    // JSON round trip
    nova::bench::Json j = history_to_json(e);
    nova::bench::Json back;
    std::string err;
    CHECK(nova::bench::Json::parse(j.dump(2), back, err));
    std::vector<HistoryEntry> e2;
    CHECK_MSG(history_from_json(back, e2, err), err);
    CHECK_EQ(e2.size(), e.size());
    CHECK(e2[1].size_role == "holdout" && e2[1].log_gain == 0.9 && e2[2].candidate == "O3");
    // malformed
    nova::bench::Json bad = back;
    bad.set("format", nova::bench::Json::string("other"));
    CHECK(!history_from_json(bad, e2, err));
    nova::bench::Json noentries = nova::bench::Json::object();
    noentries.set("format", nova::bench::Json::string("nova-history-1"));
    CHECK(!history_from_json(noentries, e2, err));
    // file store: missing file = empty; append refuses holdout/adhoc; append accumulates
    const std::string path = std::string(NOVA_TEST_TMP_DIR) + "/hist_unit.json";
    nova::bench::make_dirs(NOVA_TEST_TMP_DIR);
    std::remove(path.c_str());
    std::vector<HistoryEntry> loaded;
    CHECK(load_history_file(path, loaded, err));
    CHECK(loaded.empty());
    CHECK(append_history_file(path, {hist("matmul", 256, "O2+unroll", 0.1)}, err));
    CHECK(append_history_file(path, {hist("stencil", 200000, "O3", 0.2)}, err));
    CHECK(load_history_file(path, loaded, err));
    CHECK_EQ(loaded.size(), size_t(2));
    std::string before;
    CHECK(nova::bench::read_file(path, before));
    CHECK(!append_history_file(path, {hist("matmul", 320, "O2+unroll", 0.9, false, "holdout")}, err));
    CHECK_MSG(err.find("holdout") != std::string::npos, err);
    CHECK(!append_history_file(path, {hist("matmul", 500, "O2", 0.0, false, "adhoc")}, err));
    // a rejected append does not touch the file, even when mixed with valid entries
    CHECK(!append_history_file(path, {hist("matmul", 256, "O3", 0.3), hist("matmul", 320, "O3", 0.9, false, "holdout")}, err));
    std::string after;
    CHECK(nova::bench::read_file(path, after));
    CHECK_EQ(before, after);
    // a corrupt file is an error, not an empty history
    CHECK(nova::bench::write_file(path, "{not json"));
    CHECK(!load_history_file(path, loaded, err));
    CHECK(!append_history_file(path, {hist("matmul", 256, "O3", 0.3)}, err));
    std::remove(path.c_str());
}

}  // namespace

int main() {
    test_features();
    test_flag_features();
    test_prior_depends_on_workload();
    test_history_term();
    test_feedback_term();
    test_sequences_and_ablations();
    test_history_store();
    return nova_test::finish("adaptive-unit");
}

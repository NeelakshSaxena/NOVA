#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "check.hpp"
#include "nova/bench/stats.hpp"
#include "nova/tune/study.hpp"

using namespace nova::tune;

namespace {

// Builds a complete synthetic table. `speedup(id)` is the speedup of a configuration over -O2 (1.0 = same as -O2);
// samples jitter deterministically around the median so resampling is exercised.
WorkloadTable make_table(const std::string& bench, std::int64_t size, const std::string& role, double base_ms,
                         const std::function<double(const std::string&)>& speedup, const std::set<std::string>& failing = {}) {
    WorkloadTable t;
    t.benchmark = bench;
    t.size = size;
    t.role = role;
    t.source = "synthetic";
    t.source_hash = std::string(64, 'a');
    SearchSpace s = default_space();
    std::vector<Candidate> all = s.baselines;
    all.insert(all.end(), s.candidates.begin(), s.candidates.end());
    for (const Candidate& c : all) {
        TableEntry e;
        e.id = c.id;
        e.binary_size = 80000;
        double med = base_ms / speedup(c.id);
        for (int i = 0; i < 15; ++i) e.samples_ms.push_back(med * (1.0 + 0.002 * ((i * 7) % 11 - 5)));
        e.median_ms = nova::bench::summarize(e.samples_ms).median;
        e.eligible = !failing.count(c.id);
        e.lifecycle = e.eligible ? "BENCHMARKED" : "CORRECTNESS_FAILED";
        if (!e.eligible) e.failure_reason = "synthetic failure";
        t.entries[c.id] = e;
    }
    return t;
}

double speedup_unroll(const std::string& id) {
    if (id == "O2") return 1.0;
    if (id == "O3") return 1.05;
    double g = 1.0;
    if (id.find("unroll") != std::string::npos) g *= 1.30;
    if (id.find("native") != std::string::npos) g *= 1.10;
    if (id.find("O3") != std::string::npos) g *= 1.03;
    return g;
}

double speedup_prefetch(const std::string& id) {
    if (id == "O2") return 1.0;
    if (id == "O3") return 1.02;
    return id.find("prefetch") != std::string::npos ? 1.20 : 1.0;
}

std::vector<WorkloadTable> tables_two_families() {
    std::vector<WorkloadTable> t;
    t.push_back(make_table("stencil", 200000, "tuning", 10, speedup_unroll));
    t.push_back(make_table("stencil", 800000, "tuning", 40, speedup_unroll));
    t.push_back(make_table("stencil", 400000, "holdout", 20, speedup_unroll));
    t.push_back(make_table("stencil", 1600000, "holdout", 80, speedup_unroll));
    t.push_back(make_table("matmul", 256, "tuning", 12, speedup_prefetch));
    t.push_back(make_table("matmul", 384, "tuning", 40, speedup_prefetch));
    t.push_back(make_table("matmul", 320, "holdout", 25, speedup_prefetch));
    t.push_back(make_table("matmul", 448, "holdout", 70, speedup_prefetch));
    return t;
}

StudyConfig small_config() {
    StudyConfig c;
    c.budgets = {1, 3, 22};
    c.seeds = 5;
    return c;
}

void test_basic_invariants() {
    SearchSpace space = default_space();
    std::vector<WorkloadTable> tables = tables_two_families();
    StudyResult res;
    std::string err;
    StudyConfig cfg = small_config();
    CHECK_MSG(run_study(tables, space, cfg, res, err), err);
    // 4 tuning tables x (fixed: 3 budgets + random/adaptive x4: 5 methods... ) -> count exactly
    const size_t per_table = 3 * 1 /*fixed*/ + 5 * 3 * 5 /*random + 4 adaptive variants*/;
    CHECK_EQ(res.rows.size(), 4 * per_table);
    for (const StudyRow& r : res.rows) {
        std::string who = r.method + " B=" + std::to_string(r.budget) + " seed=" + std::to_string(r.seed) + " " + r.benchmark;
        CHECK_MSG(r.evaluations <= r.budget, who + ": evaluations <= budget");
        CHECK_MSG(r.sequence.size() == static_cast<size_t>(r.evaluations), who + ": sequence length");
        std::set<std::string> u(r.sequence.begin(), r.sequence.end());
        CHECK_MSG(u.size() == r.sequence.size(), who + ": no repeated candidate");
        CHECK_MSG(r.tuning_speedup >= 1.0 - 1e-9, who + ": tuning speedup >= 1 (baselines always included)");
        CHECK_MSG(r.regret >= 1.0 - 1e-9, who + ": regret >= 1");
        CHECK_MSG(r.oracle_speedup >= r.tuning_speedup - 1e-9, who + ": oracle is the upper bound");
        CHECK_MSG(r.holdout.size() == 2, who + ": two holdout sizes scored");
        // The reported quality must be the table's median of exactly the selected configuration.
        for (const WorkloadTable& T : tables) {
            if (T.role != "tuning" || T.benchmark != r.benchmark || T.size != r.tuning_size) continue;
            double best_base = std::min(T.entries.at("O2").median_ms, T.entries.at("O3").median_ms);
            CHECK_MSG(std::fabs(r.tuning_speedup - best_base / T.entries.at(r.selected).median_ms) < 1e-9, who + ": tuning speedup is that of the selected configuration");
        }
        bool selectable = r.selected == "O2" || r.selected == "O3" || std::find(r.sequence.begin(), r.sequence.end(), r.selected) != r.sequence.end();
        CHECK_MSG(selectable || r.method.rfind("baseline-", 0) == 0, who + ": the choice is a baseline or an evaluated candidate (no peeking at unevaluated ones)");
        if (r.budget == 22) CHECK_MSG(std::fabs(r.regret - 1.0) < 1e-9, who + ": the full budget is exhaustive");
        if (r.budget == 22) CHECK_EQ(r.evaluations, 22);
    }
    // Fixed uses the enumeration order and the same pick for every seed (it has only seed 0).
    for (const StudyRow& r : res.rows)
        if (r.method == "fixed") {
            CHECK_EQ(r.seed, 0);
            for (size_t i = 0; i < r.sequence.size(); ++i) CHECK_EQ(r.sequence[i], space.candidates[i].id);
        }
    // Deterministic: identical CSV on a second run.
    StudyResult res2;
    CHECK(run_study(tables, space, cfg, res2, err));
    CHECK_EQ(study_runs_csv(res.rows), study_runs_csv(res2.rows));
    CHECK_EQ(study_summary_csv(res.rows), study_summary_csv(res2.rows));
}

void test_baseline_methods() {
    SearchSpace space = default_space();
    std::vector<WorkloadTable> tables = tables_two_families();
    StudyConfig cfg;
    cfg.methods = {"baseline-O2", "baseline-O3"};
    cfg.budgets = {5};  // ignored for baselines
    cfg.seeds = 3;
    StudyResult res;
    std::string err;
    CHECK_MSG(run_study(tables, space, cfg, res, err), err);
    CHECK_EQ(res.rows.size(), size_t(8));  // 4 tuning tables x 2 methods, no seeds/budgets
    for (const StudyRow& r : res.rows) {
        CHECK_EQ(r.budget, 0);
        CHECK_EQ(r.evaluations, 0);
        CHECK(r.selected == (r.method == "baseline-O2" ? "O2" : "O3"));
        if (r.method == "baseline-O2") CHECK(r.tuning_speedup <= 1.0 + 1e-9);   // -O3 is slightly faster in the synthetic data
        if (r.method == "baseline-O3") CHECK(std::fabs(r.tuning_speedup - 1.0) < 1e-9);
    }
}

void test_no_holdout_leakage_into_selection() {
    SearchSpace space = default_space();
    StudyConfig cfg = small_config();
    std::vector<WorkloadTable> a = tables_two_families();
    // Same tuning data, completely different holdout data (inverted preferences, and a failing configuration).
    std::vector<WorkloadTable> b;
    for (const WorkloadTable& t : a) {
        if (t.role == "tuning") b.push_back(t);
        else b.push_back(make_table(t.benchmark, t.size, "holdout", 33, [](const std::string& id) { return id == "O2+prefetch" ? 50.0 : (id == "O3+unroll+native" ? 0.1 : 1.0); }, {"O2+unroll"}));
    }
    StudyResult ra, rb;
    std::string err;
    CHECK(run_study(a, space, cfg, ra, err));
    CHECK(run_study(b, space, cfg, rb, err));
    CHECK_EQ(ra.rows.size(), rb.rows.size());
    bool holdout_differs = false;
    for (size_t i = 0; i < ra.rows.size(); ++i) {
        const StudyRow &x = ra.rows[i], &y = rb.rows[i];
        CHECK_MSG(x.selected == y.selected && x.sequence == y.sequence && x.tuning_speedup == y.tuning_speedup && x.regret == y.regret,
                  "holdout data cannot change a selection (" + x.method + ")");
        if (x.holdout_mean != y.holdout_mean) holdout_differs = true;
    }
    CHECK_MSG(holdout_differs, "...but the holdout scores themselves do depend on the holdout tables");
    // Removing the holdout tables altogether also changes nothing about selections.
    std::vector<WorkloadTable> c;
    for (const WorkloadTable& t : a)
        if (t.role == "tuning") c.push_back(t);
    StudyResult rc;
    CHECK(run_study(c, space, cfg, rc, err));
    CHECK_EQ(rc.rows.size(), ra.rows.size());
    for (size_t i = 0; i < ra.rows.size(); ++i) {
        CHECK(ra.rows[i].selected == rc.rows[i].selected && ra.rows[i].sequence == rc.rows[i].sequence);
        CHECK(rc.rows[i].holdout.empty() && rc.rows[i].holdout_mean == 0.0);
    }
}

void test_holdout_scoring() {
    SearchSpace space = default_space();
    std::vector<WorkloadTable> t;
    t.push_back(make_table("stencil", 200000, "tuning", 10, speedup_unroll));
    // On the holdout size the best baseline is O3 (1.05x faster than O2); the all-unroll candidates are 1.3x faster than O2.
    t.push_back(make_table("stencil", 400000, "holdout", 20, speedup_unroll));
    StudyConfig cfg;
    cfg.methods = {"fixed"};
    cfg.budgets = {22};
    StudyResult res;
    std::string err;
    CHECK(run_study(t, space, cfg, res, err));
    CHECK_EQ(res.rows.size(), size_t(1));
    const StudyRow& r = res.rows[0];
    CHECK_EQ(r.selected, std::string("O3+unroll+native"));  // 1.3 * 1.1 * 1.03 is the best configuration
    // speedup_unroll: selected is 1.30*1.10*1.03 times faster than O2, the best baseline (O3) is 1.05 times faster.
    const double expected_ratio = (1.30 * 1.10 * 1.03) / 1.05;
    CHECK_EQ(r.holdout.size(), size_t(1));
    CHECK_MSG(std::fabs(r.holdout[0].second - expected_ratio) < 5e-3, "holdout speedup = best baseline median / selected median: " + std::to_string(r.holdout[0].second));
    CHECK(std::fabs(r.holdout_mean - r.holdout[0].second) < 1e-12);
    CHECK_EQ(r.holdout[0].first, std::int64_t(400000));
}

void test_failures_are_never_selected() {
    SearchSpace space = default_space();
    std::vector<WorkloadTable> t;
    // The best configuration fails on the tuning workload.
    t.push_back(make_table("stencil", 200000, "tuning", 10, speedup_unroll, {"O3+unroll+native"}));
    t.push_back(make_table("stencil", 800000, "tuning", 40, speedup_unroll));
    StudyConfig cfg;
    cfg.methods = {"fixed", "random", "adaptive"};
    cfg.budgets = {22};
    cfg.seeds = 3;
    StudyResult res;
    std::string err;
    CHECK(run_study(t, space, cfg, res, err));
    int checked = 0;
    for (const StudyRow& r : res.rows) {
        if (r.tuning_size != 200000) continue;
        ++checked;
        CHECK_MSG(r.selected != "O3+unroll+native", r.method + " never selects a failed configuration");
        CHECK_EQ(r.failed_evaluations, 1);
        CHECK(r.regret >= 1.0);  // oracle is the best *eligible* configuration, so a full-budget search reaches it
        CHECK(std::fabs(r.regret - 1.0) < 1e-9);
    }
    CHECK(checked > 0);
    // A failing selected configuration on a holdout size counts as no usable result (speedup 0), never as a win.
    std::vector<WorkloadTable> u;
    u.push_back(make_table("stencil", 200000, "tuning", 10, speedup_unroll));
    u.push_back(make_table("stencil", 400000, "holdout", 20, speedup_unroll, {"O3+unroll+native"}));
    StudyConfig c2;
    c2.methods = {"fixed"};
    c2.budgets = {22};
    StudyResult r2;
    CHECK(run_study(u, space, c2, r2, err));
    CHECK_EQ(r2.rows[0].selected, std::string("O3+unroll+native"));
    CHECK_EQ(r2.rows[0].holdout[0].second, 0.0);
}

void test_adaptive_uses_its_signals() {
    SearchSpace space = default_space();
    // The matmul tuning tables say: unrolling and -march=native HURT (0.5x), everything else is neutral. A stencil workload's
    // prior, on the other hand, loves unrolling and native. With a budget of one evaluation the adaptive policy WITH history
    // therefore avoids those flags, while the same policy WITHOUT history follows its prior to O3+prefetch+unroll+native.
    auto hurt = [](const std::string& id) {
        return (id.find("unroll") != std::string::npos || id.find("native") != std::string::npos) ? 0.5 : 1.0;
    };
    std::vector<WorkloadTable> t;
    t.push_back(make_table("stencil", 200000, "tuning", 10, speedup_unroll));
    t.push_back(make_table("matmul", 256, "tuning", 12, hurt));
    t.push_back(make_table("matmul", 384, "tuning", 40, hurt));
    StudyConfig cfg;
    cfg.methods = {"adaptive", "adaptive-no-history"};
    cfg.budgets = {1};
    cfg.seeds = 8;
    StudyResult res;
    std::string err;
    CHECK(run_study(t, space, cfg, res, err));
    int with_hist = 0, without_hist = 0;
    for (const StudyRow& r : res.rows) {
        if (r.benchmark != "stencil") continue;
        CHECK_EQ(r.sequence.size(), size_t(1));
        if (r.method == "adaptive") {
            ++with_hist;
            CHECK_MSG(r.sequence[0].find("unroll") == std::string::npos && r.sequence[0].find("native") == std::string::npos,
                      "history steers away from flags that hurt elsewhere: " + r.sequence[0]);
        } else {
            ++without_hist;
            CHECK_EQ(r.sequence[0], std::string("O3+prefetch+unroll+native"));
        }
    }
    CHECK_EQ(with_hist, 8);
    CHECK_EQ(without_hist, 8);
    // Only ONE tuning workload: nothing to learn from, so adaptive and adaptive-no-history coincide exactly.
    std::vector<WorkloadTable> solo;
    solo.push_back(t[0]);
    StudyResult rs;
    CHECK(run_study(solo, space, cfg, rs, err));
    int compared = 0;
    for (const StudyRow& x : rs.rows)
        for (const StudyRow& y : rs.rows)
            if (x.method == "adaptive" && y.method == "adaptive-no-history" && x.seed == y.seed && x.budget == y.budget) {
                CHECK(x.sequence == y.sequence);
                ++compared;
            }
    CHECK_EQ(compared, 8);
}

void test_config_errors() {
    SearchSpace space = default_space();
    std::vector<WorkloadTable> tables = tables_two_families();
    StudyResult res;
    std::string err;
    StudyConfig c = small_config();
    c.methods = {"nonsense"};
    CHECK(!run_study(tables, space, c, res, err));
    c = small_config();
    c.seeds = 0;
    CHECK(!run_study(tables, space, c, res, err));
    c = small_config();
    c.budgets.clear();
    CHECK(!run_study(tables, space, c, res, err));
    c = small_config();
    std::vector<WorkloadTable> holdout_only;
    for (const WorkloadTable& t : tables)
        if (t.role == "holdout") holdout_only.push_back(t);
    CHECK(!run_study(holdout_only, space, c, res, err));
    // an ineligible baseline on a tuning table is an error, not a silent skip
    std::vector<WorkloadTable> broken;
    broken.push_back(make_table("stencil", 200000, "tuning", 10, speedup_unroll, {"O2"}));
    CHECK(!run_study(broken, space, small_config(), res, err));
}

void test_csv_and_summary() {
    SearchSpace space = default_space();
    StudyResult res;
    std::string err;
    StudyConfig cfg;
    cfg.methods = {"fixed", "random"};
    cfg.budgets = {3};
    cfg.seeds = 4;
    CHECK(run_study(tables_two_families(), space, cfg, res, err));
    std::string runs = study_runs_csv(res.rows);
    int lines = static_cast<int>(std::count(runs.begin(), runs.end(), '\n'));
    CHECK_EQ(lines, 1 + static_cast<int>(res.rows.size()));
    CHECK(runs.find("method,budget,seed,benchmark") == 0);
    std::string summary = study_summary_csv(res.rows);
    CHECK(summary.find(",ALL,") != std::string::npos);
    CHECK(summary.find("random,3,matmul,8,") != std::string::npos);  // 2 matmul tuning sizes x 4 seeds = 8 runs
    CHECK(summary.find("fixed,3,stencil,2,") != std::string::npos);  // deterministic method: one run per tuning size
    // manifest documents the leakage rule and lists every table used
    CHECK(res.manifest.find("leakage_rules") != nullptr);
    CHECK_EQ(res.manifest.find("tables")->items().size(), size_t(8));
}

}  // namespace

int main() {
    test_basic_invariants();
    test_baseline_methods();
    test_no_holdout_leakage_into_selection();
    test_holdout_scoring();
    test_failures_are_never_selected();
    test_adaptive_uses_its_signals();
    test_config_errors();
    test_csv_and_summary();
    return nova_test::finish("study-unit");
}

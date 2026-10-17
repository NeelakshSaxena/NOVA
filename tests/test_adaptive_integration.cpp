// End-to-end tests of the V0.8 pipeline with a real compiler: adaptive tuning, history, holdout evaluation, tables, study.
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "check.hpp"
#include "nova/bench/harness.hpp"
#include "nova/bench/sha256.hpp"
#include "nova/bench/util.hpp"
#include "nova/tune/cli.hpp"
#include "nova/tune/evaluate.hpp"
#include "nova/tune/study.hpp"
#include "nova/tune/tuner.hpp"

using namespace nova::tune;
using namespace nova::bench;

namespace {

const std::string kSrc = NOVA_SOURCE_DIR;
const std::string kCompiler = NOVA_TEST_COMPILER;
const std::string kTmp = NOVA_TEST_TMP_DIR;

TuneConfig base(const std::string& id, int budget) {
    TuneConfig cfg;
    cfg.benchmark = "vector_add";
    cfg.budget = budget;
    cfg.compiler = kCompiler;
    cfg.kernel_dir = kSrc + "/benchmarks/kernels";
    cfg.experiment_id = id;
    cfg.out_dir = kTmp + "/" + id;
    cfg.size = 20000;  // ad-hoc size: cheap
    cfg.repetitions = 2;
    cfg.warmup_runs = 0;
    cfg.pilot_reps = 2;
    cfg.strategy = Strategy::Adaptive;
    return cfg;
}

std::string dir_fingerprint(const std::string& dir) {
    std::string all;
    for (const char* f : {"manifest.json", "results.json", "results.csv", "raw_timings.csv"}) {
        std::string t;
        read_file(dir + "/" + f, t);
        all += f + sha256_hex(t);
    }
    return all;
}

void test_adaptive_tune_records_its_search() {
    TuneConfig cfg = base("ad-basic", 3);
    TuneResult res;
    std::string err;
    CHECK_MSG(run_tune(cfg, default_space(), Whitelist::production(), res, err, nullptr), err);
    CHECK_EQ(res.evaluations, 3);
    CHECK_EQ(res.sequence.size(), size_t(3));
    CHECK_EQ(res.builds_attempted, 5);
    CHECK(res.has_winner);
    std::set<std::string> u(res.sequence.begin(), res.sequence.end());
    CHECK_EQ(u.size(), size_t(3));
    for (const RunRecord& r : res.records) {
        CHECK_MSG(validate_record(r).empty(), r.candidate_id + ": " + join(validate_record(r), "; "));
        if (r.search_strategy != "baseline") CHECK_EQ(r.search_strategy, std::string("adaptive"));
    }
    const Json* ad = res.manifest.find("search")->find("adaptive");
    CHECK(ad != nullptr);
    CHECK_EQ(ad->find("ablation")->as_string(), std::string("none"));
    CHECK(ad->find("workload_features")->find("known")->as_bool());
    const Json* pilot = ad->find("pilot");
    CHECK_EQ(pilot->items().size(), size_t(3));
    for (size_t i = 0; i < 3; ++i) {
        const Json& p = pilot->items()[i];
        CHECK_EQ(p.find("candidate")->as_string(), res.sequence[i]);          // pilot order == evaluation order
        CHECK_EQ(p.find("candidate_samples_ms")->items().size(), size_t(2));  // pilot samples are preserved
        CHECK_EQ(p.find("anchor_samples_ms")->items().size(), size_t(2));
        CHECK(p.find("ok")->as_bool());
    }
    CHECK(res.manifest.find("search")->find("candidate_flags")->find("O2") != nullptr);
    // the budget is a hard bound for adaptive too, and the same accounting as fixed/random
    for (int budget : {0, 1}) {
        TuneConfig c2 = base("ad-budget-" + std::to_string(budget), budget);
        TuneResult r2;
        CHECK_MSG(run_tune(c2, default_space(), Whitelist::production(), r2, err, nullptr), err);
        CHECK_EQ(r2.evaluations, budget);
        CHECK_EQ(r2.builds_attempted, 2 + budget);
    }
}

void test_adaptive_determinism_and_ablation_flag() {
    std::string err;
    TuneConfig a = base("ad-det-a", 3), b = base("ad-det-b", 3);
    a.ablation.use_feedback = false;  // outcomes cannot change the sequence -> reproducible despite timing noise
    b.ablation.use_feedback = false;
    TuneResult ra, rb;
    CHECK_MSG(run_tune(a, default_space(), Whitelist::production(), ra, err, nullptr), err);
    CHECK_MSG(run_tune(b, default_space(), Whitelist::production(), rb, err, nullptr), err);
    CHECK(ra.sequence == rb.sequence);
    CHECK_EQ(ra.manifest.find("search")->find("adaptive")->find("ablation")->as_string(), std::string("no-feedback"));
    // different seed => different first pick when nothing else distinguishes candidates (no features, no history, no feedback)
    TuneConfig c = base("ad-det-c", 3);
    c.ablation.use_feedback = false;
    c.ablation.use_features = false;
    TuneConfig d = c;
    d.experiment_id = "ad-det-d";
    d.out_dir = kTmp + "/ad-det-d";
    d.search_seed = 2;
    TuneResult rc, rd;
    CHECK(run_tune(c, default_space(), Whitelist::production(), rc, err, nullptr));
    CHECK(run_tune(d, default_space(), Whitelist::production(), rd, err, nullptr));
    CHECK(rc.sequence != rd.sequence);
}

void test_history_and_no_leakage() {
    std::string err;
    const std::string path = kTmp + "/ad-history.json";
    std::remove(path.c_str());
    HistoryEntry good, hold;
    good.benchmark = "matmul"; good.size = 256; good.size_role = "tuning"; good.candidate = "O3+prefetch+unroll+native"; good.log_gain = 0.9; good.source = "t";
    hold = good;
    hold.size = 320; hold.size_role = "holdout"; hold.candidate = "O2+vec"; hold.log_gain = 9.0;
    // Write a history file that (illegitimately) contains a holdout entry: it must be ignored when read.
    Json j = history_to_json({good, hold});
    CHECK(write_file(path, j.dump(2)));
    const std::string path2 = kTmp + "/ad-history-clean.json";
    CHECK(write_file(path2, history_to_json({good}).dump(2)));

    TuneConfig with_hold = base("ad-hist-a", 4), clean = base("ad-hist-b", 4);
    with_hold.history_path = path;
    clean.history_path = path2;
    for (TuneConfig* c : {&with_hold, &clean}) c->ablation.use_feedback = false;  // sequence = f(prior, history) only
    TuneResult ra, rb;
    CHECK_MSG(run_tune(with_hold, default_space(), Whitelist::production(), ra, err, nullptr), err);
    CHECK_MSG(run_tune(clean, default_space(), Whitelist::production(), rb, err, nullptr), err);
    CHECK_EQ(ra.history_used.size(), size_t(1));  // the holdout entry was dropped
    CHECK(ra.sequence == rb.sequence);             // ...so it cannot influence the selection
    CHECK_EQ(ra.manifest.find("search")->find("adaptive")->find("history_entries_used")->as_int(), std::int64_t(1));
    // exclude-same removes entries of the very workload being tuned
    TuneConfig same = base("ad-hist-c", 1);
    same.benchmark = "matmul";
    same.size = 256;
    same.history_path = path2;
    same.history_exclude_same = true;
    same.repetitions = 2;
    TuneResult rs;
    CHECK_MSG(run_tune(same, default_space(), Whitelist::production(), rs, err, nullptr), err);
    CHECK_EQ(rs.history_used.size(), size_t(0));
    same.history_exclude_same = false;
    same.experiment_id = "ad-hist-d";
    same.out_dir = kTmp + "/ad-hist-d";
    TuneResult rs2;
    CHECK_MSG(run_tune(same, default_space(), Whitelist::production(), rs2, err, nullptr), err);
    CHECK_EQ(rs2.history_used.size(), size_t(1));
    // A missing history file is an empty history; a corrupt one is an error before any build.
    TuneConfig missing = base("ad-hist-e", 1);
    missing.history_path = kTmp + "/definitely-missing-history.json";
    TuneResult rm;
    CHECK_MSG(run_tune(missing, default_space(), Whitelist::production(), rm, err, nullptr), err);
    CHECK_EQ(rm.history_used.size(), size_t(0));
    CHECK(write_file(kTmp + "/ad-corrupt.json", "{oops"));
    TuneConfig corrupt = base("ad-hist-f", 1);
    corrupt.history_path = kTmp + "/ad-corrupt.json";
    TuneResult rc;
    CHECK(!run_tune(corrupt, default_space(), Whitelist::production(), rc, err, nullptr));
    CHECK_MSG(err.find("history") != std::string::npos, err);
}

void test_record_history_only_tuning_sizes() {
    std::string err;
    const std::string path = kTmp + "/ad-record.json";
    std::remove(path.c_str());
    // ad-hoc size: results are NOT recorded (only tuning-size outcomes may enter a history)
    TuneConfig adhoc = base("ad-rec-a", 2);
    adhoc.history_path = path;
    adhoc.record_history = true;
    TuneResult r1;
    CHECK_MSG(run_tune(adhoc, default_space(), Whitelist::production(), r1, err, nullptr), err);
    CHECK_EQ(r1.new_history.size(), size_t(0));
    CHECK(record_tune_history(adhoc, r1, err));
    std::vector<HistoryEntry> loaded;
    CHECK(load_history_file(path, loaded, err));
    CHECK_EQ(loaded.size(), size_t(0));
    // registered tuning size: entries are produced, all with role "tuning"
    TuneConfig tuning = base("ad-rec-b", 2);
    tuning.size = 1000000;
    tuning.history_path = path;
    tuning.record_history = true;
    TuneResult r2;
    CHECK_MSG(run_tune(tuning, default_space(), Whitelist::production(), r2, err, nullptr), err);
    CHECK_EQ(r2.new_history.size(), size_t(2));
    for (const HistoryEntry& h : r2.new_history) {
        CHECK_EQ(h.size_role, std::string("tuning"));
        CHECK_EQ(h.size, std::int64_t(1000000));
        CHECK(h.candidate != "O2" && h.candidate != "O3");
    }
    CHECK(record_tune_history(tuning, r2, err));
    CHECK(load_history_file(path, loaded, err));
    CHECK_EQ(loaded.size(), size_t(2));
}

void test_evaluate_is_one_way() {
    std::string err;
    TuneConfig cfg = base("ad-eval-src", 2);
    cfg.strategy = Strategy::Random;
    TuneResult res;
    CHECK_MSG(run_tune(cfg, default_space(), Whitelist::production(), res, err, nullptr), err);
    CHECK_MSG(write_tune(cfg, res, err), err);
    const std::string before = dir_fingerprint(cfg.out_dir);
    const std::string hist = kTmp + "/ad-eval-history.json";
    CHECK(write_file(hist, history_to_json({}).dump(2)));
    std::string hist_before;
    CHECK(read_file(hist, hist_before));

    EvalConfig ec;
    ec.tune_dir = cfg.out_dir;
    ec.out_dir = kTmp + "/ad-eval-out";
    ec.experiment_id = "ad-eval-out";
    ec.sizes = {6000, 9000};
    ec.compiler = kCompiler;
    ec.repetitions = 3;
    ec.warmup_runs = 0;
    ExperimentResult out;
    CHECK_MSG(run_evaluate(ec, Whitelist::production(), out, err, nullptr), err);
    BenchConfig bc;
    bc.out_dir = ec.out_dir;
    bc.experiment_id = ec.experiment_id;
    CHECK_MSG(write_experiment(bc, out, err), err);
    // records: O2, O3 and (if different) the winner, per size; all complete and correct
    std::set<std::string> ids;
    for (const RunRecord& r : out.records) {
        ids.insert(r.candidate_id);
        CHECK_MSG(validate_record(r).empty(), r.candidate_id + ": " + join(validate_record(r), "; "));
        CHECK_EQ(r.correctness_status, std::string("PASS"));
        CHECK_EQ(static_cast<int>(r.timing_samples_ms.size()), 3);
        CHECK(!r.selected && r.rank == 0);  // evaluation never selects anything
    }
    CHECK(ids.count("O2") && ids.count("O3"));
    CHECK(ids.count(res.winner_id));
    CHECK_EQ(out.manifest.find("kind")->as_string(), std::string("holdout-eval"));
    CHECK_EQ(out.manifest.find("selected_candidate")->as_string(), res.winner_id);
    // One-way flow: the tuning result and the history are byte-for-byte unchanged.
    CHECK_EQ(dir_fingerprint(cfg.out_dir), before);
    std::string hist_after;
    CHECK(read_file(hist, hist_after));
    CHECK_EQ(hist_after, hist_before);
    // The report prints a per-size comparison.
    std::string report = format_holdout_report(out.records);
    CHECK(report.find("size 6000") != std::string::npos && report.find("size 9000") != std::string::npos);
    CHECK(report.find("selected vs best baseline") != std::string::npos || report.find("is a baseline") != std::string::npos);
    // Errors: not a tuning result; missing directory; refusing to overwrite the tuning directory via the CLI.
    EvalConfig bad = ec;
    bad.tune_dir = kTmp + "/does-not-exist";
    CHECK(!run_evaluate(bad, Whitelist::production(), out, err, nullptr));
    std::ostringstream o, e;
    CHECK_EQ(run_evaluate_command({cfg.out_dir, "--out=" + cfg.out_dir, "--compiler=" + kCompiler}, o, e), 2);
    CHECK_EQ(dir_fingerprint(cfg.out_dir), before);
    CHECK_EQ(run_evaluate_command({}, o, e), 2);
    // Tampered flags in a tuning manifest are re-validated against the whitelist before anything is built.
    Json manifest;
    std::vector<RunRecord> recs;
    CHECK(load_experiment(cfg.out_dir, manifest, recs, err));
    Json tampered = manifest;
    Json search = *manifest.find("search");
    Json cflags = *search.find("candidate_flags");
    Json evil = Json::array();
    evil.push(Json::string("-O2"));
    evil.push(Json::string("-ffast-math"));
    cflags.set("O2", evil);
    search.set("candidate_flags", cflags);
    tampered.set("search", search);
    const std::string tdir = kTmp + "/ad-eval-tampered";
    CHECK(make_dirs(tdir));
    CHECK(write_file(tdir + "/manifest.json", tampered.dump(2)));
    std::string results_text;
    CHECK(read_file(cfg.out_dir + "/results.json", results_text));
    CHECK(write_file(tdir + "/results.json", results_text));
    EvalConfig te = ec;
    te.tune_dir = tdir;
    te.out_dir = kTmp + "/ad-eval-tampered-out";
    CHECK(!run_evaluate(te, Whitelist::production(), out, err, nullptr));
    CHECK_MSG(err.find("-ffast-math") != std::string::npos, err);
    CHECK_MSG(!file_exists(te.out_dir + "/bin"), "nothing was built from tampered flags");
}

void test_table_and_study_pipeline() {
    std::string err;
    TableConfig tc;
    tc.benchmark = "vector_add";
    tc.sizes = {1000000, 2000000};  // one tuning size and one holdout size of the registry
    tc.compiler = kCompiler;
    tc.kernel_dir = kSrc + "/benchmarks/kernels";
    tc.experiment_id = "tb-it";
    tc.out_dir = kTmp + "/tb-it";
    tc.repetitions = 3;
    tc.warmup_runs = 0;
    ExperimentResult res;
    CHECK_MSG(run_table(tc, default_space(), Whitelist::production(), res, err, nullptr), err);
    CHECK_EQ(res.records.size(), size_t(48));  // 24 configurations x 2 sizes
    int tuning = 0, holdout = 0;
    for (const RunRecord& r : res.records) {
        CHECK_MSG(r.correctness_status == "PASS", r.candidate_id + ": " + r.failure_reason);
        CHECK_MSG(validate_record(r).empty(), r.candidate_id);
        CHECK_EQ(static_cast<int>(r.timing_samples_ms.size()), 3);
        CHECK(!r.selected && r.rank == 0);  // a table selects nothing
        tuning += r.size_role == "tuning";
        holdout += r.size_role == "holdout";
    }
    CHECK_EQ(tuning, 24);
    CHECK_EQ(holdout, 24);
    BenchConfig bc;
    bc.out_dir = tc.out_dir;
    bc.experiment_id = tc.experiment_id;
    CHECK_MSG(write_experiment(bc, res, err), err);
    std::vector<WorkloadTable> tables;
    CHECK_MSG(load_tables(tc.out_dir, default_space(), 2.0, tables, err), err);
    CHECK_EQ(tables.size(), size_t(2));
    // study on the real (tiny) table: all rows well-formed, holdout scored, B=22 exhaustive
    StudyConfig sc;
    sc.budgets = {2, 22};
    sc.seeds = 2;
    sc.methods = {"fixed", "random", "adaptive", "adaptive-no-features"};
    StudyResult sr;
    CHECK_MSG(run_study(tables, default_space(), sc, sr, err), err);
    CHECK(!sr.rows.empty());
    for (const StudyRow& r : sr.rows) {
        CHECK(r.evaluations <= r.budget && r.tuning_speedup >= 1.0 - 1e-9 && r.holdout.size() == 1);
        if (r.budget == 22) CHECK(std::abs(r.regret - 1.0) < 1e-9);
    }
    std::string wd = kTmp + "/study-it";
    CHECK_MSG(write_study(wd, sr, err), err);
    CHECK(file_exists(wd + "/study_runs.csv") && file_exists(wd + "/study_summary.csv") && file_exists(wd + "/manifest.json"));
    // the table directory is not a tuning result
    std::ostringstream o, e;
    CHECK_EQ(run_explain_command({tc.out_dir}, o, e), 1);
    // an incomplete table (a configuration removed) is rejected
    Json manifest;
    std::vector<RunRecord> recs;
    CHECK(load_experiment(tc.out_dir, manifest, recs, err));
    Json arr = Json::array();
    for (const RunRecord& r : recs)
        if (r.candidate_id != "O3+unroll") arr.push(record_to_json(r));
    Json results = Json::object();
    results.set("records", arr);
    std::string idir = kTmp + "/tb-incomplete";
    CHECK(make_dirs(idir));
    CHECK(write_file(idir + "/manifest.json", manifest.dump(2)));
    CHECK(write_file(idir + "/results.json", results.dump(2)));
    std::vector<WorkloadTable> none;
    CHECK(!load_tables(idir, default_space(), 2.0, none, err));
    CHECK_MSG(err.find("O3+unroll") != std::string::npos, err);
}

void test_cli_usage_errors() {
    auto rc = [](std::vector<std::string> args, const char* which) {
        std::ostringstream o, e;
        std::string cmd = which;
        if (cmd == "tune") return run_tune_command(args, o, e);
        if (cmd == "table") return run_table_command(args, o, e);
        if (cmd == "study") return run_study_command(args, o, e);
        if (cmd == "history") return run_history_command(args, o, e);
        return run_evaluate_command(args, o, e);
    };
    CHECK_EQ(rc({"stencil", "--budget=1", "--ablation=bogus"}, "tune"), 2);
    CHECK_EQ(rc({"stencil", "--budget=1", "--record-history"}, "tune"), 2);
    CHECK_EQ(rc({"stencil", "--budget=1", "--pilot-reps=0"}, "tune"), 2);
    CHECK_EQ(rc({}, "table"), 2);
    CHECK_EQ(rc({"stencil", "--role=weird"}, "table"), 1);
    CHECK_EQ(rc({}, "study"), 2);
    CHECK_EQ(rc({"--tables=x"}, "study"), 2);
    CHECK_EQ(rc({"--tables=/nonexistent", "--out=" + kTmp + "/x"}, "study"), 1);
    CHECK_EQ(rc({}, "history"), 2);
    CHECK_EQ(rc({"--bogus"}, "history"), 2);
    CHECK_EQ(rc({}, "evaluate"), 2);
}

}  // namespace

int main() {
    make_dirs(kTmp);
    test_adaptive_tune_records_its_search();
    test_adaptive_determinism_and_ablation_flag();
    test_history_and_no_leakage();
    test_record_history_only_tuning_sizes();
    test_evaluate_is_one_way();
    test_table_and_study_pipeline();
    test_cli_usage_errors();
    return nova_test::finish("adaptive-integration");
}

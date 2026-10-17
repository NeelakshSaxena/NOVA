// SmartPass lifecycle tests with a real compiler and a fixture kernel whose behaviour is switched by test-only flags.
#include <algorithm>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "check.hpp"
#include "nova/bench/harness.hpp"
#include "nova/bench/util.hpp"
#include "nova/tune/cli.hpp"
#include "nova/tune/tuner.hpp"

using namespace nova::tune;
using namespace nova::bench;

namespace {

const std::string kSrc = NOVA_SOURCE_DIR;
const std::string kCompiler = NOVA_TEST_COMPILER;
const std::string kTmp = NOVA_TEST_TMP_DIR;

const Whitelist& test_whitelist() {
    static const Whitelist w([] {
        std::set<std::string> f = Whitelist::production().flags();
        for (const char* x : {"-DNOVA_FIXTURE_WRONG", "-DNOVA_FIXTURE_HANG", "-DNOVA_FIXTURE_BREAK", "-DNOVA_FIXTURE_BIG"}) f.insert(x);
        return f;
    }());
    return w;
}

TuneConfig fixture_config(const std::string& id, int budget) {
    TuneConfig cfg;
    cfg.benchmark = "vector_add";
    cfg.budget = budget;
    cfg.compiler = kCompiler;
    cfg.kernel_dir = kSrc + "/tests/fixtures/flagged";
    cfg.experiment_id = id;
    cfg.out_dir = kTmp + "/" + id;
    cfg.size = 20000;  // an ad-hoc size: cheap, and not a holdout size
    cfg.repetitions = 3;
    cfg.warmup_runs = 0;
    cfg.run_timeout_ms = 2500;
    cfg.strategy = Strategy::Fixed;
    return cfg;
}

SearchSpace lifecycle_space() {
    SearchSpace s;
    std::string err;
    bool ok = make_space({{"-O2"}, {"-O3"}},
                         {{"-O2", "-funroll-loops"},            // C1 good
                          {"-O2", "-DNOVA_FIXTURE_WRONG"},      // C2 fast but incorrect
                          {"-O2", "-DNOVA_FIXTURE_BREAK"},      // C3 does not build
                          {"-O2", "-DNOVA_FIXTURE_HANG"},       // C4 never finishes
                          {"-O3", "-DNOVA_FIXTURE_BIG"},        // C5 correct but 400 KB bigger
                          {"-O3", "-funroll-loops"}},           // C6 good
                         test_whitelist(), s, err);
    CHECK_MSG(ok, err);
    return s;
}

const RunRecord* by_id(const std::vector<RunRecord>& rs, const std::string& id) {
    for (const RunRecord& r : rs)
        if (r.candidate_id == id) return &r;
    return nullptr;
}

int count_build_commands(const Json& manifest) {
    int n = 0;
    for (const Json& c : manifest.find("commands")->items())
        if (c.find("phase")->as_string() == "build") ++n;
    return n;
}

// Every lifecycle path, in one run: eligible, correctness failure, build failure, timeout, policy rejection.
void test_all_lifecycle_paths() {
    TuneConfig cfg = fixture_config("tn-lifecycle", 6);
    TuneResult res;
    std::string err;
    CHECK_MSG(run_tune(cfg, lifecycle_space(), test_whitelist(), res, err, nullptr), err);
    CHECK_EQ(res.records.size(), size_t(8));
    CHECK_EQ(res.evaluations, 6);
    CHECK_EQ(res.builds_attempted, 8);

    auto state = [&](const char* id) {
        const RunRecord* r = by_id(res.records, id);
        CHECK_MSG(r != nullptr, std::string("record exists for ") + id);
        return r ? r->lifecycle : std::string("MISSING");
    };
    CHECK_EQ(state("O2"), std::string("ELIGIBLE"));
    CHECK_EQ(state("O3"), std::string("ELIGIBLE"));
    CHECK_EQ(state("O2+unroll"), std::string("ELIGIBLE"));
    CHECK_EQ(state("O3+unroll"), std::string("ELIGIBLE"));
    CHECK_EQ(state("O2+-DNOVA_FIXTURE_WRONG"), std::string("CORRECTNESS_FAILED"));
    CHECK_EQ(state("O2+-DNOVA_FIXTURE_BREAK"), std::string("BUILD_FAILED"));
    CHECK_EQ(state("O2+-DNOVA_FIXTURE_HANG"), std::string("CORRECTNESS_FAILED"));
    CHECK_EQ(state("O3+-DNOVA_FIXTURE_BIG"), std::string("REJECTED_BY_POLICY"));

    // Failure reasons are recorded for every failed candidate.
    const RunRecord* wrong = by_id(res.records, "O2+-DNOVA_FIXTURE_WRONG");
    const RunRecord* brk = by_id(res.records, "O2+-DNOVA_FIXTURE_BREAK");
    const RunRecord* hang = by_id(res.records, "O2+-DNOVA_FIXTURE_HANG");
    const RunRecord* big = by_id(res.records, "O3+-DNOVA_FIXTURE_BIG");
    CHECK_MSG(wrong && wrong->failure_reason.find("differs from the reference") != std::string::npos, wrong ? wrong->failure_reason : "");
    CHECK_MSG(brk && brk->failure_reason.find("compiler exited") != std::string::npos, brk ? brk->failure_reason : "");
    CHECK_MSG(hang && hang->failure_reason.find("timed out") != std::string::npos, hang ? hang->failure_reason : "");
    CHECK_MSG(big && big->failure_reason.find("binary size") != std::string::npos, big ? big->failure_reason : "");
    CHECK(brk && brk->correctness_status == "NOT_RUN" && brk->build_status == "FAILED");
    CHECK(wrong && wrong->build_status == "OK" && wrong->correctness_status == "FAIL");

    // Ineligible candidates are never ranked, never selected, and have no timing samples (except policy rejects).
    for (const char* id : {"O2+-DNOVA_FIXTURE_WRONG", "O2+-DNOVA_FIXTURE_BREAK", "O2+-DNOVA_FIXTURE_HANG", "O3+-DNOVA_FIXTURE_BIG"}) {
        const RunRecord* r = by_id(res.records, id);
        CHECK_MSG(r && r->rank == 0 && !r->selected, std::string(id) + " is not ranked or selected");
    }
    CHECK(wrong && wrong->timing_samples_ms.empty());
    // Ranking covers exactly the eligible candidates, 1..4, and the winner has rank 1.
    std::vector<int> ranks;
    int selected = 0;
    for (const RunRecord& r : res.records) {
        if (r.rank > 0) ranks.push_back(r.rank);
        if (r.selected) {
            ++selected;
            CHECK_EQ(r.rank, 1);
            CHECK_EQ(r.lifecycle, std::string("ELIGIBLE"));
            CHECK_EQ(r.candidate_id, res.winner_id);
        }
    }
    std::sort(ranks.begin(), ranks.end());
    CHECK((ranks == std::vector<int>{1, 2, 3, 4}));
    CHECK_EQ(selected, 1);
    CHECK(res.has_winner);
    // Eligible medians are in ascending rank order.
    double prev = -1;
    for (int rank = 1; rank <= 4; ++rank)
        for (const RunRecord& r : res.records)
            if (r.rank == rank) {
                CHECK(r.median_ms >= prev);
                prev = r.median_ms;
            }
    // Every record is complete evidence (failures included).
    for (const RunRecord& r : res.records) {
        std::vector<std::string> problems = validate_record(r);
        CHECK_MSG(problems.empty(), r.candidate_id + ": " + join(problems, "; "));
    }
    // Baselines stay visible and are labelled as such.
    CHECK_EQ(by_id(res.records, "O2")->search_strategy, std::string("baseline"));
    CHECK_EQ(by_id(res.records, "O2+unroll")->search_strategy, std::string("fixed"));
}

// An incorrect candidate cannot win even when it is by far the fastest.
void test_incorrect_candidate_cannot_win() {
    TuneConfig cfg = fixture_config("tn-nowin", 1);
    SearchSpace s;
    std::string err;
    CHECK_MSG(make_space({{"-O2"}, {"-O3"}}, {{"-O2", "-DNOVA_FIXTURE_WRONG"}}, test_whitelist(), s, err), err);
    // Evidence that the wrong candidate really is the fastest if correctness were ignored.
    BuildOutcome wrong_build = build_kernel(kCompiler, {"-std=c++17", "-msse2", "-mfpmath=sse", "-O2", "-DNOVA_FIXTURE_WRONG"},
                                            kSrc + "/tests/fixtures/flagged/vector_add.cpp", kTmp + "/wrong_fast.exe", 120000);
    BuildOutcome ok_build = build_kernel(kCompiler, {"-std=c++17", "-msse2", "-mfpmath=sse", "-O3"},
                                         kSrc + "/tests/fixtures/flagged/vector_add.cpp", kTmp + "/right.exe", 120000);
    CHECK(wrong_build.ok && ok_build.ok);
    double wrong_t = run_kernel(kTmp + "/wrong_fast.exe", 2000000, 1, 60000, nullptr).output.time_ms;
    double right_t = run_kernel(kTmp + "/right.exe", 2000000, 1, 60000, nullptr).output.time_ms;
    CHECK_MSG(wrong_t < right_t / 10, "the incorrect kernel is >10x faster (it skips the work)");

    TuneResult res;
    CHECK_MSG(run_tune(cfg, s, test_whitelist(), res, err, nullptr), err);
    CHECK(res.has_winner);
    CHECK(res.winner_id == "O2" || res.winner_id == "O3");
    const RunRecord* w = by_id(res.records, "O2+-DNOVA_FIXTURE_WRONG");
    CHECK(w && w->lifecycle == "CORRECTNESS_FAILED" && !w->selected && w->rank == 0);
}

// No eligible candidate at all: reported, not hidden; baselines stay visible with their failure reason.
void test_no_eligible_candidate() {
    TuneConfig cfg = fixture_config("tn-none", 2);
    SearchSpace s;
    std::string err;
    CHECK_MSG(make_space({{"-O2", "-DNOVA_FIXTURE_BREAK"}, {"-O3", "-DNOVA_FIXTURE_WRONG"}},
                         {{"-O2", "-DNOVA_FIXTURE_BREAK", "-funroll-loops"}, {"-O3", "-DNOVA_FIXTURE_WRONG", "-funroll-loops"}},
                         test_whitelist(), s, err), err);
    TuneResult res;
    CHECK_MSG(run_tune(cfg, s, test_whitelist(), res, err, nullptr), err);
    CHECK(!res.has_winner);
    CHECK(res.winner_id.empty());
    CHECK_EQ(res.records.size(), size_t(4));
    for (const RunRecord& r : res.records) {
        CHECK_MSG(!r.selected && r.rank == 0, r.candidate_id);
        CHECK_MSG(!r.failure_reason.empty(), r.candidate_id + " keeps its failure reason");
    }
    CHECK(by_id(res.records, "O2+-DNOVA_FIXTURE_BREAK") != nullptr && by_id(res.records, "O3+-DNOVA_FIXTURE_WRONG") != nullptr);  // baselines stay visible
    std::string text = explain_tune(res.manifest, res.records);
    CHECK(text.find("NO ELIGIBLE CANDIDATE") != std::string::npos);
    CHECK(text.find("BUILD_FAILED") != std::string::npos);
}

// Budget B => at most B search candidates evaluated; baselines are always included on top.
void test_budget_enforcement() {
    SearchSpace s = lifecycle_space();
    // Use only the cheap, valid candidates plus failures: budget covers prefixes of the fixed order.
    for (int budget : {0, 1, 2, 3, 6, 10}) {
        TuneConfig cfg = fixture_config("tn-budget-" + std::to_string(budget), budget);
        cfg.repetitions = 1;
        TuneResult res;
        std::string err;
        CHECK_MSG(run_tune(cfg, s, test_whitelist(), res, err, nullptr), err);
        int expected = std::min(budget, 6);
        CHECK_MSG(res.evaluations == expected, "evaluations == min(B, space) for B=" + std::to_string(budget));
        CHECK_MSG(res.evaluations <= budget, "never more than B evaluations for B=" + std::to_string(budget));
        CHECK_EQ(res.builds_attempted, 2 + expected);
        CHECK_EQ(count_build_commands(res.manifest), 2 + expected);  // independent evidence: commands actually issued
        CHECK_EQ(res.records.size(), static_cast<size_t>(2 + expected));
        CHECK_EQ(res.sequence.size(), static_cast<size_t>(expected));
        int searched = 0, baselines = 0;
        for (const RunRecord& r : res.records) (r.search_strategy == "baseline" ? baselines : searched)++;
        CHECK_EQ(baselines, 2);
        CHECK_EQ(searched, expected);
        CHECK_EQ(res.manifest.find("search")->find("budget")->as_int(), std::int64_t(budget));
        // Baselines are in the result for every budget (including 0) and the winner exists (a baseline when B = 0).
        CHECK(by_id(res.records, "O2") != nullptr && by_id(res.records, "O3") != nullptr);
        CHECK(res.has_winner);
        if (budget == 0) CHECK(res.winner_id == "O2" || res.winner_id == "O3");
    }
}

// Same seed + same configuration => same candidate sequence; fixed and random use the same accounting.
void test_determinism_and_equal_budgets() {
    SearchSpace s = default_space();
    Whitelist w = Whitelist::production();
    std::vector<std::string> ref;
    for (int round = 0; round < 2; ++round) {
        TuneConfig cfg = fixture_config("tn-det-" + std::to_string(round), 3);
        cfg.benchmark = "vector_add";
        cfg.kernel_dir = kSrc + "/benchmarks/kernels";
        cfg.strategy = Strategy::Random;
        cfg.search_seed = 11;
        cfg.repetitions = 2;
        TuneResult res;
        std::string err;
        CHECK_MSG(run_tune(cfg, s, w, res, err, nullptr), err);
        CHECK_EQ(res.evaluations, 3);
        if (round == 0) ref = res.sequence;
        else CHECK(res.sequence == ref);
        // the manifest records the same sequence
        std::vector<std::string> from_manifest;
        for (const Json& j : res.manifest.find("search")->find("sequence")->items()) from_manifest.push_back(j.as_string());
        CHECK(from_manifest == res.sequence);
        // order in the records: baselines first, then the sequence
        CHECK_EQ(res.records[0].candidate_id, std::string("O2"));
        CHECK_EQ(res.records[2].candidate_id, ref[0]);
    }
    // Fixed under the same budget evaluates the same number of candidates.
    TuneConfig cfg = fixture_config("tn-fixed", 3);
    cfg.kernel_dir = kSrc + "/benchmarks/kernels";
    cfg.repetitions = 2;
    TuneResult res;
    std::string err;
    CHECK_MSG(run_tune(cfg, s, w, res, err, nullptr), err);
    CHECK_EQ(res.evaluations, 3);
    CHECK_EQ(res.sequence[0], s.candidates[0].id);
}

// Invalid configuration is rejected before any process is executed.
void test_invalid_config_rejected_before_execution() {
    std::string err;
    // (1) a flag outside the whitelist, hand-built into the space
    SearchSpace s;
    s.baselines.push_back(Candidate{"O2", {"-O2"}, true});
    s.candidates.push_back(Candidate{"evil", {"-O2", "-ffast-math"}, false});
    TuneConfig cfg = fixture_config("tn-invalid-flag", 1);
    cfg.compiler = "definitely-not-a-compiler-nova";  // would fail first if validation ran later
    TuneResult res;
    CHECK(!run_tune(cfg, s, Whitelist::production(), res, err, nullptr));
    CHECK_MSG(err.find("-ffast-math") != std::string::npos, err);
    CHECK_EQ(res.builds_attempted, 0);
    CHECK_MSG(!file_exists(cfg.out_dir + "/bin"), "no output directory was created");
    // (2) a candidate that only becomes invalid through shell metacharacters
    s.candidates[0].flags = {"-O2", "-funroll-loops && echo pwned"};
    CHECK(!run_tune(cfg, s, Whitelist::production(), res, err, nullptr));
    CHECK_MSG(err.find("echo pwned") != std::string::npos, err);
    // (3) an invalid baseline
    SearchSpace s2;
    s2.baselines.push_back(Candidate{"bad", {"-O2", "-O3"}, true});
    CHECK(!run_tune(cfg, s2, Whitelist::production(), res, err, nullptr));
    // (4) no baselines at all
    SearchSpace s3;
    CHECK(!run_tune(cfg, s3, Whitelist::production(), res, err, nullptr));
    // (5) holdout sizes are refused for tuning
    TuneConfig h = fixture_config("tn-holdout", 1);
    h.benchmark = "matmul";
    h.kernel_dir = kSrc + "/benchmarks/kernels";
    h.size = 320;  // a registered holdout size
    h.compiler = "definitely-not-a-compiler-nova";
    CHECK(!run_tune(h, default_space(), Whitelist::production(), res, err, nullptr));
    CHECK_MSG(err.find("holdout") != std::string::npos, err);
    CHECK_EQ(res.builds_attempted, 0);
    // (6) other config errors
    TuneConfig bad = fixture_config("tn-bad", 1);
    bad.benchmark = "nope";
    CHECK(!run_tune(bad, default_space(), Whitelist::production(), res, err, nullptr));
    bad = fixture_config("tn-bad", -1);
    CHECK(!run_tune(bad, default_space(), Whitelist::production(), res, err, nullptr));
    bad = fixture_config("", 1);
    CHECK(!run_tune(bad, default_space(), Whitelist::production(), res, err, nullptr));
    bad = fixture_config("tn-bad", 1);
    bad.repetitions = 0;
    CHECK(!run_tune(bad, default_space(), Whitelist::production(), res, err, nullptr));
    // (7) a missing compiler is an error with the compiler named (after flag validation passed)
    bad = fixture_config("tn-bad", 0);
    bad.compiler = "definitely-not-a-compiler-nova";
    CHECK(!run_tune(bad, default_space(), Whitelist::production(), res, err, nullptr));
    CHECK_MSG(err.find("definitely-not-a-compiler-nova") != std::string::npos, err);
}

// Result files preserve raw samples and metadata and round-trip.
void test_result_files() {
    TuneConfig cfg = fixture_config("tn-files", 2);
    cfg.repetitions = 4;
    TuneResult res;
    std::string err;
    SearchSpace s;
    CHECK_MSG(make_space({{"-O2"}, {"-O3"}}, {{"-O2", "-funroll-loops"}, {"-O2", "-DNOVA_FIXTURE_WRONG"}}, test_whitelist(), s, err), err);
    CHECK_MSG(run_tune(cfg, s, test_whitelist(), res, err, nullptr), err);
    CHECK_MSG(write_tune(cfg, res, err), err);
    for (const char* f : {"manifest.json", "results.json", "results.csv", "raw_timings.csv"})
        CHECK_MSG(file_exists(cfg.out_dir + "/" + f), std::string("wrote ") + f);
    Json manifest;
    std::vector<RunRecord> back;
    CHECK_MSG(load_experiment(cfg.out_dir, manifest, back, err), err);
    CHECK_EQ(back.size(), res.records.size());
    for (size_t i = 0; i < back.size(); ++i) {
        CHECK_MSG(record_to_json(back[i]) == record_to_json(res.records[i]), "record " + std::to_string(i) + " round-trips");
        CHECK(back[i].timing_samples_ms == res.records[i].timing_samples_ms);
    }
    // raw samples: exactly repetitions per timed candidate, none for the incorrect one
    std::string raw;
    CHECK(read_file(cfg.out_dir + "/raw_timings.csv", raw));
    int lines = static_cast<int>(std::count(raw.begin(), raw.end(), '\n'));
    CHECK_EQ(lines, 1 + 3 * 4);  // header + (O2, O3, O2+unroll) x 4 repetitions
    CHECK(raw.find("DNOVA_FIXTURE_WRONG") == std::string::npos);
    // metadata present
    CHECK(manifest.find("search")->find("accounting") != nullptr);
    CHECK(manifest.find("search")->find("whitelist") != nullptr);
    CHECK(manifest.find("environment") != nullptr && manifest.find("commands") != nullptr);
    CHECK_EQ(manifest.find("kind")->as_string(), std::string("tune"));
    CHECK_EQ(manifest.find("winner")->as_string(), res.winner_id);
    // the CLI explain command renders the saved result
    std::ostringstream out, errs;
    CHECK_EQ(run_explain_command({cfg.out_dir}, out, errs), 0);
    CHECK_MSG(out.str().find("WINNER") != std::string::npos, out.str());
    CHECK(out.str().find("CORRECTNESS_FAILED") != std::string::npos);
    CHECK_EQ(run_explain_command({kTmp + "/does-not-exist"}, out, errs), 1);
    // baseline experiments are not tuning results
    CHECK_EQ(run_explain_command({}, out, errs), 2);
}

// CLI-level behaviour: exit code is non-zero when nothing is eligible; usage errors are exit 2.
void test_cli_exit_codes() {
    std::ostringstream out, err;
    // every candidate of the bad matmul kernel is incorrect -> no eligible candidate -> exit 1
    int rc = run_tune_command({"matmul", "--budget=1", "--size=24", "--reps=2", "--warmup=0", "--compiler=" + kCompiler,
                               "--kernel-dir=" + kSrc + "/tests/fixtures/bad", "--id=tn-cli-none", "--out=" + kTmp + "/tn-cli-none"},
                              out, err);
    CHECK_EQ(rc, 1);
    CHECK_MSG(out.str().find("NO ELIGIBLE CANDIDATE") != std::string::npos, out.str());
    CHECK_MSG(err.str().find("no eligible candidate") != std::string::npos, err.str());
    CHECK(file_exists(kTmp + "/tn-cli-none/results.json"));  // evidence is still saved
    // success path
    std::ostringstream out2, err2;
    rc = run_tune_command({"vector_add", "--budget=1", "--size=20000", "--reps=2", "--warmup=0", "--compiler=" + kCompiler,
                           "--kernel-dir=" + kSrc + "/benchmarks/kernels", "--strategy=fixed", "--id=tn-cli-ok", "--out=" + kTmp + "/tn-cli-ok"},
                          out2, err2);
    CHECK_MSG(rc == 0, err2.str());
    // usage errors
    for (const std::vector<std::string>& bad : std::vector<std::vector<std::string>>{
             {}, {"vector_add"}, {"vector_add", "--budget=-1"}, {"vector_add", "--budget=x"}, {"vector_add", "--budget=1", "--strategy=bayesian"},
             {"vector_add", "--budget=1", "--bogus=2"}, {"vector_add", "other", "--budget=1"}, {"vector_add", "--budget=1", "--affinity=0"}}) {
        std::ostringstream o, e;
        CHECK_MSG(run_tune_command(bad, o, e) == 2, "usage error for: " + join(bad));
    }
}

}  // namespace

int main() {
    make_dirs(kTmp);
    test_all_lifecycle_paths();
    test_incorrect_candidate_cannot_win();
    test_no_eligible_candidate();
    test_budget_enforcement();
    test_determinism_and_equal_budgets();
    test_invalid_config_rejected_before_execution();
    test_result_files();
    test_cli_exit_codes();
    return nova_test::finish("tune-integration");
}

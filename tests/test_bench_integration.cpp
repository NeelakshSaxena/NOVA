// End-to-end benchmark-lab tests: real compiler, real kernels, real processes.
#include <cmath>
#include <string>
#include <vector>

#include "check.hpp"
#include "nova/bench/harness.hpp"
#include "nova/bench/util.hpp"

#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

using namespace nova::bench;

namespace {

const std::string kSrc = NOVA_SOURCE_DIR;
const std::string kCompiler = NOVA_TEST_COMPILER;
const std::string kTmp = NOVA_TEST_TMP_DIR;

BenchConfig base_config(const std::string& id) {
    BenchConfig cfg;
    cfg.compiler = kCompiler;
    cfg.kernel_dir = kSrc + "/benchmarks/kernels";
    cfg.experiment_id = id;
    cfg.out_dir = kTmp + "/" + id;
    cfg.repetitions = 5;
    cfg.warmup_runs = 1;
    cfg.seed = 3;
    return cfg;
}

const RunRecord* find(const std::vector<RunRecord>& rs, const std::string& id, std::int64_t n) {
    for (const RunRecord& r : rs)
        if (r.candidate_id == id && r.input_size == n) return &r;
    return nullptr;
}

// Every baseline executable builds, passes the oracle before being timed, and stores exactly the configured
// number of raw samples with complete metadata.
void test_all_kernels_build_and_validate() {
    BenchConfig cfg = base_config("it-all");
    cfg.sizes = {48};
    ExperimentResult res;
    std::string err;
    CHECK_MSG(run_experiment(cfg, res, err, nullptr), err);
    CHECK_EQ(res.records.size(), size_t(6));  // 3 kernels x {O2, O3}
    for (const RunRecord& r : res.records) {
        std::string what = r.candidate_id + " n=" + std::to_string(r.input_size);
        CHECK_MSG(r.build_status == "OK", what + " builds: " + r.failure_reason);
        CHECK_MSG(r.correctness_status == "PASS", what + " passes the oracle: " + r.failure_reason);
        CHECK_MSG(static_cast<int>(r.timing_samples_ms.size()) == cfg.repetitions, what + " stores the configured sample count");
        CHECK_MSG(validate_record(r).empty(), what + " has complete metadata");
        CHECK_MSG(r.fp_semantics == "strict" && r.flags.find("-O") != std::string::npos, what + " flags recorded");
        CHECK_MSG(r.median_ms > 0, what + " measured a positive time");
        CHECK_MSG(r.source_hash.size() == 64, what + " has a source hash");
    }
    CHECK_EQ(res.raw.size(), size_t(6 * cfg.repetitions));
    // O2 and O3 of the same kernel are the same source.
    CHECK_EQ(find(res.records, "matmul-O2", 48)->source_hash, find(res.records, "matmul-O3", 48)->source_hash);
    CHECK(find(res.records, "matmul-O2", 48)->source_hash != find(res.records, "stencil-O2", 48)->source_hash);
    // Interleaving: within each round every variant runs once and the order is recorded.
    for (int rep = 1; rep <= cfg.repetitions; ++rep) {
        int in_round = 0;
        for (const RawSample& s : res.raw)
            if (s.benchmark == "matmul" && s.repetition == rep) ++in_round;
        CHECK_MSG(in_round == 2, "each round times both matmul variants once");
    }
}

// Results round-trip through the files on disk, including samples and metadata.
void test_serialization_round_trip() {
    BenchConfig cfg = base_config("it-roundtrip");
    cfg.benchmarks = {"vector_add"};
    cfg.sizes = {20000, 30000};
    cfg.include_ofast = true;
    ExperimentResult res;
    std::string err;
    CHECK_MSG(run_experiment(cfg, res, err, nullptr), err);
    CHECK_MSG(write_experiment(cfg, res, err), err);
    for (const char* f : {"manifest.json", "results.json", "results.csv", "raw_timings.csv"})
        CHECK_MSG(file_exists(cfg.out_dir + "/" + f), std::string("wrote ") + f);
    Json manifest;
    std::vector<RunRecord> back;
    CHECK_MSG(load_experiment(cfg.out_dir, manifest, back, err), err);
    CHECK_EQ(back.size(), res.records.size());
    CHECK_EQ(back.size(), size_t(6));  // 2 sizes x {O2, O3, Ofast}
    for (size_t i = 0; i < back.size(); ++i) {
        CHECK_MSG(record_to_json(back[i]) == record_to_json(res.records[i]), "record " + std::to_string(i) + " round-trips exactly");
        CHECK(back[i].timing_samples_ms == res.records[i].timing_samples_ms);
    }
    // -Ofast is a separate, labelled condition
    int fast = 0;
    for (const RunRecord& r : back) {
        if (r.opt_level == "Ofast") {
            ++fast;
            CHECK_EQ(r.fp_semantics, std::string("fast-math"));
        } else {
            CHECK_EQ(r.fp_semantics, std::string("strict"));
        }
    }
    CHECK_EQ(fast, 2);
    // Manifest carries environment + exact commands + protocol
    CHECK(manifest.find("environment") != nullptr);
    CHECK(manifest.find("commands") != nullptr && !manifest.find("commands")->items().empty());
    CHECK(manifest.find("protocol")->find("order_seed") != nullptr);
    Environment env;
    CHECK_MSG(environment_from_json(*manifest.find("environment"), env, err), err);
    CHECK_MSG(missing_environment_fields(env).empty(), "environment metadata complete");
    // The report renders and flags -Ofast separately.
    std::string report = format_report(back);
    CHECK(report.find("[fast-math]") != std::string::npos);
    CHECK(report.find("vector_add  n=20000") != std::string::npos);

    // Missing metadata is detected on load rather than silently omitted.
    std::string text;
    CHECK(read_file(cfg.out_dir + "/results.json", text));
    Json results;
    CHECK(Json::parse(text, results, err));
    Json damaged = Json::object();
    Json recs = Json::array();
    for (size_t i = 0; i < results.find("records")->items().size(); ++i) {
        Json r = results.find("records")->items()[i];
        if (i == 0) {
            Json stripped = Json::object();
            for (const auto& kv : r.members())
                if (kv.first != "compiler_version") stripped.set(kv.first, kv.second);
            r = stripped;
        }
        recs.push(r);
    }
    damaged.set("records", recs);
    std::string dir = kTmp + "/it-roundtrip-damaged";
    CHECK(make_dirs(dir));
    CHECK(write_file(dir + "/results.json", damaged.dump(2)));
    CHECK(write_file(dir + "/manifest.json", manifest.dump(2)));
    std::vector<RunRecord> none;
    Json m2;
    CHECK(!load_experiment(dir, m2, none, err));
    CHECK_MSG(err.find("compiler_version") != std::string::npos, "load error names the missing field: " + err);
}

// A deliberately incorrect kernel is detected by the oracle and never timed.
void test_incorrect_output_is_detected() {
    BenchConfig cfg = base_config("it-bad");
    cfg.kernel_dir = kSrc + "/tests/fixtures/bad";
    cfg.benchmarks = {"matmul"};
    cfg.sizes = {32};
    ExperimentResult res;
    std::string err;
    CHECK_MSG(run_experiment(cfg, res, err, nullptr), err);
    CHECK_EQ(res.records.size(), size_t(2));
    for (const RunRecord& r : res.records) {
        CHECK_EQ(r.build_status, std::string("OK"));
        CHECK_EQ(r.correctness_status, std::string("FAIL"));
        CHECK_MSG(r.failure_reason.find("differs from the reference") != std::string::npos, r.failure_reason);
        CHECK_MSG(r.timing_samples_ms.empty(), "an incorrect candidate is never timed");
        CHECK_MSG(validate_record(r).empty(), "the failure record itself is complete: " + join(validate_record(r), "; "));
    }
    CHECK(res.raw.empty());
    // ...and the report shows the failure rather than dropping the row.
    std::string report = format_report(res.records);
    CHECK(report.find("FAIL") != std::string::npos);
}

// A build failure is recorded with the compiler's reason; the run continues.
void test_build_failure_is_recorded() {
    BenchConfig cfg = base_config("it-buildfail");
    cfg.benchmarks = {"matmul"};
    cfg.sizes = {16};
    cfg.fixed_flags = {"-std=c++17", "-fnova-no-such-flag"};
    ExperimentResult res;
    std::string err;
    CHECK_MSG(run_experiment(cfg, res, err, nullptr), err);
    CHECK_EQ(res.records.size(), size_t(2));
    for (const RunRecord& r : res.records) {
        CHECK_EQ(r.build_status, std::string("FAILED"));
        CHECK_EQ(r.correctness_status, std::string("NOT_RUN"));
        CHECK_MSG(!r.failure_reason.empty(), "build failure reason recorded");
        CHECK_MSG(validate_record(r).empty(), join(validate_record(r), "; "));
    }
}

// Configuration problems are errors, not silent defaults.
void test_config_errors() {
    ExperimentResult res;
    std::string err;
    BenchConfig cfg = base_config("it-cfg");
    cfg.benchmarks = {"nope"};
    CHECK(!run_experiment(cfg, res, err, nullptr));
    cfg = base_config("it-cfg");
    cfg.repetitions = 0;
    CHECK(!run_experiment(cfg, res, err, nullptr));
    cfg = base_config("");
    CHECK(!run_experiment(cfg, res, err, nullptr));
    cfg = base_config("it-cfg");
    cfg.compiler = "definitely-not-a-compiler-nova";
    cfg.benchmarks = {"matmul"};
    CHECK(!run_experiment(cfg, res, err, nullptr));
    CHECK_MSG(err.find("definitely-not-a-compiler-nova") != std::string::npos, err);
    cfg = base_config("it-cfg");
    cfg.kernel_dir = kTmp + "/no-such-dir";
    cfg.benchmarks = {"matmul"};
    CHECK(!run_experiment(cfg, res, err, nullptr));
    cfg = base_config("it-cfg");
    cfg.role = "bogus";
    CHECK(!run_experiment(cfg, res, err, nullptr));
}

// The oracle agrees with the real kernels for several sizes and seeds (so the tolerance is not vacuous),
// and the benchmark can be rerun from a clean output directory.
void test_clean_rerun_and_seeds() {
    std::string first_hash;
    for (int round = 0; round < 2; ++round) {
        BenchConfig cfg = base_config("it-rerun-" + std::to_string(round));
        cfg.benchmarks = {"stencil", "matmul"};
        cfg.sizes = {37};
        cfg.seed = 99 + static_cast<std::uint64_t>(round);
        ExperimentResult res;
        std::string err;
        CHECK_MSG(run_experiment(cfg, res, err, nullptr), err);
        for (const RunRecord& r : res.records) CHECK_MSG(r.correctness_status == "PASS", r.candidate_id + ": " + r.failure_reason);
        if (round == 0) first_hash = res.records[0].source_hash;
        else CHECK_EQ(res.records[0].source_hash, first_hash);  // same source -> same hash across runs
    }
    // Direct oracle check with an odd size and a large seed.
    std::string exe = kTmp + "/oracle_matmul";
#ifdef _WIN32
    exe += ".exe";
#endif
    BuildOutcome b = build_kernel(kCompiler, {"-std=c++17", "-O3", "-msse2", "-mfpmath=sse"}, kSrc + "/benchmarks/kernels/matmul.cpp", exe, 120000);
    CHECK_MSG(b.ok, b.reason);
    ReferenceCache cache;
    for (std::uint64_t seed : {0ULL, 1ULL, 18446744073709551615ULL}) {
        for (std::int64_t n : {1, 2, 7, 33}) {
            std::string err;
            const KernelOutput* ref = cache.get("matmul", n, seed, err);
            CHECK_MSG(ref != nullptr, err);
            KernelRun run = run_kernel(exe, n, seed, 60000, ref);
            CHECK_MSG(run.ok, run.reason);
            CHECK_MSG(run.correct(), "matmul n=" + i64_to_string(n) + " seed=" + u64_to_string(seed) + ": " + join(run.mismatches, "; "));
        }
    }
    // A wrong seed makes the same binary fail the oracle (the oracle really depends on the input).
    std::string err;
    const KernelOutput* ref = cache.get("matmul", 7, 5, err);
    KernelRun run = run_kernel(exe, 7, 6, 60000, ref);
    CHECK(run.ok && !run.correct());
}

// Regression: a relative output directory with '/' separators (the CLI default) must work; the built
// binaries are launched through that relative path.
void test_relative_paths() {
    // Run from a directory that is NOT the test executable's directory: Windows resolves a '/'-separated relative
    // program path against the application directory, which hid this bug when both coincided.
    CHECK(make_dirs(kTmp + "/cwd"));
#ifdef _WIN32
    CHECK(_chdir((kTmp + "/cwd").c_str()) == 0);
#else
    CHECK(chdir((kTmp + "/cwd").c_str()) == 0);
#endif
    BenchConfig cfg = base_config("it-rel");
    cfg.out_dir = "it-rel.V0.6-out";  // relative to the (changed) working directory, forward slashes
    cfg.out_dir = "sub/" + cfg.out_dir;
    cfg.kernel_dir = kSrc + "/benchmarks/kernels";
    cfg.benchmarks = {"vector_add"};
    cfg.sizes = {5000};
    ExperimentResult res;
    std::string err;
    CHECK_MSG(run_experiment(cfg, res, err, nullptr), err);
    CHECK_EQ(res.records.size(), size_t(2));
    for (const RunRecord& r : res.records) CHECK_MSG(r.correctness_status == "PASS", r.candidate_id + ": " + r.failure_reason);
}

}  // namespace

int main() {
    make_dirs(kTmp);
    test_all_kernels_build_and_validate();
    test_serialization_round_trip();
    test_incorrect_output_is_detected();
    test_build_failure_is_recorded();
    test_config_errors();
    test_clean_rerun_and_seeds();
    test_relative_paths();
    return nova_test::finish("bench-integration");
}

#include "nova/tune/cli.hpp"

#include <cstdlib>
#include <ostream>

#include "nova/bench/harness.hpp"
#include "nova/bench/util.hpp"
#include "nova/tune/tuner.hpp"
#include "nova/tune/evaluate.hpp"

namespace nova {
namespace tune {

const char* tune_usage() {
    return "SmartPass (bounded GCC flag search over the benchmark kernels):\n"
           "  tune <benchmark> --budget=B [options]   Evaluate the baselines + at most B searched candidates\n"
           "      --strategy=fixed|random|adaptive (default random)  --seed=N (search seed, default 1)\n"
           "      adaptive only: --ablation=none|no-features|no-history|no-feedback  --history=FILE  --record-history\n"
           "                     --history-exclude-same  --pilot-reps=N (default 5)\n"
           "      --size=N (tuning size; holdout sizes are refused)  --reps=N  --warmup=N  --input-seed=N\n"
           "      --max-binary-growth=R (policy, default 2.0; 0 disables)  --affinity=HEXMASK  --high-priority\n"
           "      --compiler=g++  --kernel-dir=benchmarks/kernels  --id=NAME  --out=DIR\n"
           "      Exit 0 only if an eligible (built, correct, fully timed) winner exists.\n"
           "  explain <results-dir>                   Show every candidate, its state and failure reason, and the winner\n";
}

namespace {

int usage_error(std::ostream& err, const std::string& msg) {
    err << "nova: " << msg << "\nTry 'nova --help' for usage.\n";
    return 2;
}

bool parse_int(const std::string& s, std::int64_t& v) {
    if (s.empty()) return false;
    char* end = nullptr;
    long long x = std::strtoll(s.c_str(), &end, 10);
    if (*end != '\0') return false;
    v = x;
    return true;
}

}  // namespace

int run_tune_command(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    TuneConfig cfg;
    bool have_budget = false;
    std::string out_dir;
    for (size_t k = 0; k < args.size(); ++k) {
        const std::string& a = args[k];
        if (a.rfind("--", 0) != 0) {
            if (!cfg.benchmark.empty()) return usage_error(err, "'tune' takes exactly one benchmark name");
            cfg.benchmark = a;
            continue;
        }
        size_t eq = a.find('=');
        std::string key = a.substr(0, eq);
        std::string val = eq == std::string::npos ? "" : a.substr(eq + 1);
        std::int64_t n = 0;
        if (key == "--high-priority" && eq == std::string::npos) {
            cfg.high_priority = true;
        } else if (key == "--history-exclude-same" && eq == std::string::npos) {
            cfg.history_exclude_same = true;
        } else if (key == "--record-history" && eq == std::string::npos) {
            cfg.record_history = true;
        } else if (eq == std::string::npos) {
            return usage_error(err, "unknown option '" + a + "' for 'tune'");
        } else if (key == "--budget") {
            if (!parse_int(val, n) || n < 0 || n > 100000) return usage_error(err, "--budget must be an integer 0..100000");
            cfg.budget = static_cast<int>(n);
            have_budget = true;
        } else if (key == "--strategy") {
            if (!parse_strategy(val, cfg.strategy)) return usage_error(err, "--strategy must be 'fixed', 'random' or 'adaptive'");
        } else if (key == "--seed") {
            if (!parse_int(val, n) || n < 0) return usage_error(err, "--seed must be a non-negative integer");
            cfg.search_seed = static_cast<std::uint64_t>(n);
        } else if (key == "--input-seed") {
            if (!parse_int(val, n) || n < 0) return usage_error(err, "--input-seed must be a non-negative integer");
            cfg.seed = static_cast<std::uint64_t>(n);
        } else if (key == "--size") {
            if (!parse_int(val, n) || n <= 0) return usage_error(err, "--size must be a positive integer");
            cfg.size = n;
        } else if (key == "--reps") {
            if (!parse_int(val, n) || n < 1 || n > 10000) return usage_error(err, "--reps must be 1..10000");
            cfg.repetitions = static_cast<int>(n);
        } else if (key == "--warmup") {
            if (!parse_int(val, n) || n < 0 || n > 1000) return usage_error(err, "--warmup must be 0..1000");
            cfg.warmup_runs = static_cast<int>(n);
        } else if (key == "--max-binary-growth") {
            char* end = nullptr;
            double g = std::strtod(val.c_str(), &end);
            if (val.empty() || *end != '\0' || g < 0) return usage_error(err, "--max-binary-growth must be a number >= 0");
            cfg.max_binary_growth = g;
        } else if (key == "--affinity") {
            char* end = nullptr;
            unsigned long long mask = std::strtoull(val.c_str(), &end, 16);
            if (val.empty() || *end != '\0' || mask == 0) return usage_error(err, "--affinity must be a non-zero hex mask");
            cfg.affinity_mask = mask;
        } else if (key == "--ablation") {
            if (!parse_ablation(val, cfg.ablation)) return usage_error(err, "--ablation must be none, no-features, no-history or no-feedback");
        } else if (key == "--history") {
            cfg.history_path = val;
        } else if (key == "--pilot-reps") {
            if (!parse_int(val, n) || n < 1 || n > 1000) return usage_error(err, "--pilot-reps must be 1..1000");
            cfg.pilot_reps = static_cast<int>(n);
        } else if (key == "--compiler") {
            cfg.compiler = val;
        } else if (key == "--kernel-dir") {
            cfg.kernel_dir = val;
        } else if (key == "--id") {
            cfg.experiment_id = val;
        } else if (key == "--out") {
            out_dir = val;
        } else {
            return usage_error(err, "unknown option '" + key + "' for 'tune'");
        }
    }
    if (cfg.benchmark.empty()) return usage_error(err, "'tune' requires a benchmark name (see 'nova bench list')");
    if (!have_budget) return usage_error(err, "'tune' requires --budget=B");
    if (cfg.record_history && cfg.history_path.empty()) return usage_error(err, "--record-history requires --history=FILE");
    if (cfg.experiment_id.empty()) {
        std::string compact;
        for (char c : bench::utc_timestamp())
            if (c >= '0' && c <= '9') compact.push_back(c);
        cfg.experiment_id = "tune-" + compact;
    }
    cfg.out_dir = out_dir.empty() ? "benchmarks/results/" + cfg.experiment_id : out_dir;

    TuneResult result;
    std::string error;
    out << "tuning " << cfg.benchmark << " -> " << cfg.out_dir << "\n";
    if (!run_tune(cfg, default_space(), Whitelist::production(), result, error, &out)) {
        err << "nova tune: " << error << "\n";
        return 1;
    }
    if (!write_tune(cfg, result, error)) {
        err << "nova tune: " << error << "\n";
        return 1;
    }
    if (cfg.record_history) {
        if (cfg.history_path.empty()) return usage_error(err, "--record-history requires --history=FILE");
        if (!record_tune_history(cfg, result, error)) {
            err << "nova tune: " << error << "\n";
            return 1;
        }
        out << "recorded " << result.new_history.size() << " history entries in " << cfg.history_path << "\n";
    }
    out << "\n" << explain_tune(result.manifest, result.records);
    out << "results written to " << cfg.out_dir << "\n";
    if (!result.has_winner) {
        err << "nova tune: no eligible candidate\n";
        return 1;
    }
    return 0;
}

int run_explain_command(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    if (args.size() != 1) return usage_error(err, "'explain' requires exactly one results directory");
    bench::Json manifest;
    std::vector<bench::RunRecord> records;
    std::string error;
    if (!bench::load_experiment(args[0], manifest, records, error)) {
        err << "nova explain: " << error << "\n";
        return 1;
    }
    const bench::Json* kind = manifest.find("kind");
    if (!kind || kind->type() != bench::Json::Type::String || kind->as_string() != "tune") {
        err << "nova explain: '" << args[0] << "' is not a tuning result (use 'nova bench report' for baselines)\n";
        return 1;
    }
    int bad = 0;
    for (const bench::RunRecord& r : records)
        for (const std::string& p : bench::validate_record(r)) {
            err << "nova explain: record " << r.candidate_id << ": " << p << "\n";
            ++bad;
        }
    out << explain_tune(manifest, records);
    return bad == 0 ? 0 : 1;
}

}  // namespace tune
}  // namespace nova

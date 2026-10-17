#include "nova/bench/cli.hpp"

#include <cstdlib>
#include <ostream>

#include "nova/bench/harness.hpp"
#include "nova/bench/util.hpp"

namespace nova {
namespace bench {

const char* bench_usage() {
    return "Benchmark lab (C++ kernels built with GCC; independent of NOVA Script):\n"
           "  bench list                      List registered benchmarks and their tuning/holdout sizes\n"
           "  bench run [options]             Build -O2/-O3 baselines, validate against the oracle, time them\n"
           "      --kernels=a,b   benchmarks (default: all)       --sizes=n1,n2  explicit sizes\n"
           "      --role=tuning|holdout|all  sizes when --sizes is absent (default tuning)\n"
           "      --reps=N (default 9)  --warmup=N (default 1)  --seed=N (default 1)\n"
           "      --include-ofast  also build -Ofast, reported as a separate fast-math condition\n"
           "      --affinity=HEXMASK  --high-priority  pin / prioritize benchmark processes (less scheduler noise)\n"
           "      --compiler=g++  --kernel-dir=benchmarks/kernels  --id=NAME  --out=DIR\n"
           "  bench report <dir>              Validate and summarize a saved experiment directory\n";
}

namespace {

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> parts;
    size_t pos = 0;
    while (true) {
        size_t c = s.find(sep, pos);
        parts.push_back(s.substr(pos, c == std::string::npos ? std::string::npos : c - pos));
        if (c == std::string::npos) break;
        pos = c + 1;
    }
    return parts;
}

bool parse_int(const std::string& s, std::int64_t& v) {
    if (s.empty()) return false;
    char* end = nullptr;
    long long x = std::strtoll(s.c_str(), &end, 10);
    if (*end != '\0') return false;
    v = x;
    return true;
}

int usage_error(std::ostream& err, const std::string& msg) {
    err << "nova bench: " << msg << "\nTry 'nova --help' for usage.\n";
    return 2;
}

int cmd_list(std::ostream& out) {
    for (const BenchmarkDef& d : registry()) {
        out << d.name << "  - " << d.description << "\n";
        out << "    tuning sizes: ";
        for (size_t i = 0; i < d.tuning_sizes.size(); ++i) out << (i ? ", " : "") << i64_to_string(d.tuning_sizes[i]);
        out << "\n    holdout sizes: ";
        for (size_t i = 0; i < d.holdout_sizes.size(); ++i) out << (i ? ", " : "") << i64_to_string(d.holdout_sizes[i]);
        out << "\n";
    }
    return 0;
}

int cmd_run(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    BenchConfig cfg;
    std::string out_dir;
    for (size_t k = 1; k < args.size(); ++k) {
        const std::string& a = args[k];
        size_t eq = a.find('=');
        std::string key = a.substr(0, eq);
        std::string val = eq == std::string::npos ? "" : a.substr(eq + 1);
        std::int64_t n = 0;
        if (key == "--include-ofast" && eq == std::string::npos) {
            cfg.include_ofast = true;
        } else if (key == "--high-priority" && eq == std::string::npos) {
            cfg.high_priority = true;
        } else if (eq == std::string::npos) {
            return usage_error(err, "unknown option '" + a + "' for 'bench run'");
        } else if (key == "--kernels") {
            cfg.benchmarks = split(val, ',');
        } else if (key == "--sizes") {
            for (const std::string& s : split(val, ',')) {
                if (!parse_int(s, n) || n <= 0) return usage_error(err, "bad size '" + s + "'");
                cfg.sizes.push_back(n);
            }
        } else if (key == "--role") {
            cfg.role = val;
        } else if (key == "--reps") {
            if (!parse_int(val, n) || n < 1 || n > 10000) return usage_error(err, "--reps must be 1..10000");
            cfg.repetitions = static_cast<int>(n);
        } else if (key == "--warmup") {
            if (!parse_int(val, n) || n < 0 || n > 1000) return usage_error(err, "--warmup must be 0..1000");
            cfg.warmup_runs = static_cast<int>(n);
        } else if (key == "--seed") {
            if (!parse_int(val, n) || n < 0) return usage_error(err, "--seed must be a non-negative integer");
            cfg.seed = static_cast<std::uint64_t>(n);
        } else if (key == "--affinity") {
            char* end = nullptr;
            unsigned long long mask = std::strtoull(val.c_str(), &end, 16);
            if (val.empty() || *end != '\0' || mask == 0) return usage_error(err, "--affinity must be a non-zero hex mask");
            cfg.affinity_mask = mask;
        } else if (key == "--compiler") {
            cfg.compiler = val;
        } else if (key == "--kernel-dir") {
            cfg.kernel_dir = val;
        } else if (key == "--id") {
            cfg.experiment_id = val;
        } else if (key == "--out") {
            out_dir = val;
        } else {
            return usage_error(err, "unknown option '" + key + "' for 'bench run'");
        }
    }
    if (cfg.experiment_id.empty()) {
        std::string ts = utc_timestamp();
        std::string compact;
        for (char c : ts)
            if (c >= '0' && c <= '9') compact.push_back(c);
        cfg.experiment_id = "exp-" + compact;
    }
    cfg.out_dir = out_dir.empty() ? "benchmarks/results/" + cfg.experiment_id : out_dir;

    ExperimentResult result;
    std::string error;
    out << "experiment " << cfg.experiment_id << " -> " << cfg.out_dir << "\n";
    if (!run_experiment(cfg, result, error, &out)) {
        err << "nova bench: " << error << "\n";
        return 1;
    }
    if (!write_experiment(cfg, result, error)) {
        err << "nova bench: " << error << "\n";
        return 1;
    }
    out << "\n" << format_report(result.records);
    int bad = 0;
    for (const RunRecord& r : result.records) {
        std::vector<std::string> problems = validate_record(r);
        for (const std::string& p : problems) err << "nova bench: record " << r.candidate_id << " n=" << i64_to_string(r.input_size) << ": " << p << "\n";
        if (!problems.empty()) ++bad;
    }
    out << "results written to " << cfg.out_dir << " (manifest.json, results.json, results.csv, raw_timings.csv)\n";
    return bad == 0 ? 0 : 1;
}

int cmd_report(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    if (args.size() != 2) return usage_error(err, "'bench report' requires exactly one directory");
    Json manifest;
    std::vector<RunRecord> records;
    std::string error;
    if (!load_experiment(args[1], manifest, records, error)) {
        err << "nova bench: " << error << "\n";
        return 1;
    }
    int bad = 0;
    for (const RunRecord& r : records) {
        for (const std::string& p : validate_record(r)) {
            err << "nova bench: record " << r.candidate_id << " n=" << i64_to_string(r.input_size) << ": " << p << "\n";
            ++bad;
        }
    }
    const Json* env = manifest.find("environment");
    Environment e;
    if (!env || !environment_from_json(*env, e, error)) {
        err << "nova bench: manifest: " << (error.empty() ? "missing environment" : error) << "\n";
        ++bad;
    } else {
        for (const std::string& f : missing_environment_fields(e)) {
            err << "nova bench: manifest environment field '" << f << "' is empty\n";
            ++bad;
        }
        out << "environment: " << e.os << "; " << e.cpu << "; " << e.logical_cpus << " logical CPUs; " << u64_to_string(e.ram_bytes / (1024 * 1024)) << " MiB RAM\n"
            << "compiler: " << e.compiler_version << " [" << e.compiler_target << "]\n\n";
    }
    out << format_report(records);
    return bad == 0 ? 0 : 1;
}

}  // namespace

int run_bench_command(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    if (args.empty()) return usage_error(err, "missing subcommand (list, run, report)");
    if (args[0] == "list") return args.size() == 1 ? cmd_list(out) : usage_error(err, "'bench list' takes no arguments");
    if (args[0] == "run") return cmd_run(args, out, err);
    if (args[0] == "report") return cmd_report(args, out, err);
    return usage_error(err, "unknown subcommand '" + args[0] + "'");
}

}  // namespace bench
}  // namespace nova

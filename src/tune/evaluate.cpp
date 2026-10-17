#include "nova/tune/evaluate.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ostream>
#include <set>
#include <sstream>

#include "nova/bench/registry.hpp"
#include "nova/bench/util.hpp"

namespace nova {
namespace tune {

using bench::Json;
using bench::RunRecord;

const char* evaluate_usage() {
    return "  evaluate <tune-dir> [options]            Measure a finished tuning result's winner vs -O2/-O3 on held-out sizes\n"
           "      --sizes=n1,n2 (default: the benchmark's registered holdout sizes)  --reps=N (15)  --warmup=N (2)\n"
           "      --affinity=HEXMASK  --high-priority  --compiler=g++  --kernel-dir=DIR  --id=NAME  --out=DIR\n";
}

namespace {

const char* chr_nl() { return "\n"; }

const char* exe_suffix() {
#ifdef _WIN32
    return ".exe";
#else
    return "";
#endif
}

Json to_json_array(const std::vector<std::string>& v) {
    Json a = Json::array();
    for (const std::string& s : v) a.push(Json::string(s));
    return a;
}

}  // namespace

bool run_evaluate(const EvalConfig& cfg, const Whitelist& whitelist, bench::ExperimentResult& result, std::string& error,
                  std::ostream* log) {
    if (cfg.experiment_id.empty() || cfg.out_dir.empty()) {
        error = "experiment id and output directory are required";
        return false;
    }
    if (cfg.repetitions < 1 || cfg.warmup_runs < 0) {
        error = "repetitions must be >= 1 and warm-up runs >= 0";
        return false;
    }
    Json manifest;
    std::vector<RunRecord> records;
    if (!bench::load_experiment(cfg.tune_dir, manifest, records, error)) return false;
    const Json* kind = manifest.find("kind");
    if (!kind || kind->type() != Json::Type::String || kind->as_string() != "tune") {
        error = "'" + cfg.tune_dir + "' is not a tuning result";
        return false;
    }
    const Json *bench_name = manifest.find("benchmark"), *winner = manifest.find("winner"), *has = manifest.find("has_winner"),
               *search = manifest.find("search");
    if (!bench_name || !winner || !has || !search || !has->as_bool() || winner->as_string().empty()) {
        error = "the tuning result has no selected winner to evaluate";
        return false;
    }
    const std::string benchmark = bench_name->as_string();
    const bench::BenchmarkDef* def = bench::find_benchmark(benchmark);
    if (!def) {
        error = "unknown benchmark '" + benchmark + "'";
        return false;
    }
    const Json* cflags = search->find("candidate_flags");
    if (!cflags || cflags->type() != Json::Type::Object) {
        error = "the tuning manifest does not record candidate flags";
        return false;
    }
    // Variants to measure: the plain baselines and the winner (if it is not itself a baseline).
    std::vector<std::string> ids = {"O2", "O3"};
    if (std::find(ids.begin(), ids.end(), winner->as_string()) == ids.end()) ids.push_back(winner->as_string());
    std::vector<std::vector<std::string>> variant_flags;
    for (const std::string& id : ids) {
        const Json* fl = cflags->find(id);
        if (!fl || fl->type() != Json::Type::Array) {
            error = "candidate '" + id + "' has no recorded flags";
            return false;
        }
        std::vector<std::string> flags;
        for (const Json& f : fl->items()) flags.push_back(f.as_string());
        std::string why;
        if (!validate_flags(flags, whitelist, why)) {  // flags from a file are untrusted: validate before building
            error = "recorded flags of '" + id + "' are invalid: " + why;
            return false;
        }
        variant_flags.push_back(flags);
    }
    std::vector<std::int64_t> sizes = cfg.sizes.empty() ? def->holdout_sizes : cfg.sizes;
    for (std::int64_t n : sizes)
        if (n <= 0 || (benchmark == "stencil" && n < 3)) {
            error = "invalid size " + bench::i64_to_string(n);
            return false;
        }
    std::string tune_strategy = "unknown";
    for (const RunRecord& r : records)
        if (r.candidate_id == winner->as_string()) tune_strategy = r.search_strategy;

    std::string kernel_dir = cfg.kernel_dir;
    if (kernel_dir.empty()) {
        const Json* kd = manifest.find("kernel_dir");
        kernel_dir = kd && kd->type() == Json::Type::String ? kd->as_string() : "benchmarks/kernels";
    }
    bench::Environment env = bench::collect_environment(cfg.compiler);
    if (env.compiler_version.empty()) {
        error = "cannot run compiler '" + cfg.compiler + "' (is it on PATH?)";
        return false;
    }
    std::string hash;
    if (!bench::kernel_source_hash(kernel_dir, benchmark, hash, error)) return false;
    if (!bench::make_dirs(cfg.out_dir + "/bin")) {
        error = "cannot create output directory '" + cfg.out_dir + "'";
        return false;
    }

    result = bench::ExperimentResult();
    Json commands = Json::array();
    std::vector<bench::BuildOutcome> builds;
    std::vector<std::string> exes;
    for (size_t i = 0; i < ids.size(); ++i) {
        std::vector<std::string> flags = cfg.fixed_flags;
        flags.insert(flags.end(), variant_flags[i].begin(), variant_flags[i].end());
        std::string exe = cfg.out_dir + "/bin/" + benchmark + "_" + std::to_string(i) + exe_suffix();
        if (log) *log << "build " << ids[i] << " ..." << std::flush;
        bench::BuildOutcome b = bench::build_kernel(cfg.compiler, flags, kernel_dir + "/" + benchmark + ".cpp", exe, cfg.build_timeout_ms);
        if (log) *log << (b.ok ? " ok" : " FAILED: " + b.reason) << "\n";
        Json c = Json::object();
        c.set("phase", Json::string("build"));
        c.set("candidate_id", Json::string(ids[i]));
        c.set("argv", to_json_array(b.command));
        c.set("exit_ok", Json::boolean(b.ok));
        commands.push(c);
        result.build_logs[ids[i]] = b.log;
        builds.push_back(b);
        exes.push_back(exe);
    }

    bench::ReferenceCache refs;
    bench::TimingParams tp;
    tp.experiment_id = cfg.experiment_id;
    tp.repetitions = cfg.repetitions;
    tp.warmup_runs = cfg.warmup_runs;
    tp.seed = cfg.seed;
    tp.order_seed = cfg.order_seed;
    tp.run_timeout_ms = cfg.run_timeout_ms;
    tp.affinity_mask = cfg.affinity_mask;
    tp.high_priority = cfg.high_priority;
    std::uint64_t salt = 0;
    for (std::int64_t n : sizes) {
        std::vector<bench::TimingSlot> slots(ids.size());
        for (size_t i = 0; i < ids.size(); ++i) {
            RunRecord& r = slots[i].record;
            std::vector<std::string> flags = cfg.fixed_flags;
            flags.insert(flags.end(), variant_flags[i].begin(), variant_flags[i].end());
            r.experiment_id = cfg.experiment_id;
            r.benchmark = benchmark;
            r.source_hash = hash;
            r.compiler_version = env.compiler_version;
            r.flags = bench::join(flags);
            r.opt_level = variant_flags[i][0].substr(1);
            r.fp_semantics = "strict";
            r.input_size = n;
            bool is_holdout = std::find(def->holdout_sizes.begin(), def->holdout_sizes.end(), n) != def->holdout_sizes.end();
            bool is_tuning = std::find(def->tuning_sizes.begin(), def->tuning_sizes.end(), n) != def->tuning_sizes.end();
            r.size_role = is_holdout ? "holdout" : (is_tuning ? "tuning" : "adhoc");
            r.seed = cfg.seed;
            r.candidate_id = ids[i];
            r.warmup_runs = cfg.warmup_runs;
            r.repetitions = cfg.repetitions;
            bool baseline = ids[i] == "O2" || ids[i] == "O3";
            r.search_strategy = baseline ? "baseline" : tune_strategy;
            r.binary_size_bytes = builds[i].binary_size;
            r.build_time_s = builds[i].seconds;
            slots[i].exe = exes[i];
            if (builds[i].ok) {
                r.build_status = "OK";
            } else {
                r.build_status = "FAILED";
                r.correctness_status = "NOT_RUN";
                r.failure_reason = builds[i].reason;
            }
        }
        if (!bench::validate_and_time(slots, benchmark, n, tp, salt++, refs, result.raw, &commands, log, error)) return false;
        for (bench::TimingSlot& s : slots) result.records.push_back(s.record);
    }

    Json prot = Json::object();
    prot.set("repetitions", Json::integer(cfg.repetitions));
    prot.set("warmup_runs", Json::integer(cfg.warmup_runs));
    prot.set("input_seed", Json::string(bench::u64_to_string(cfg.seed)));
    prot.set("order_seed", Json::string(bench::u64_to_string(cfg.order_seed)));
    prot.set("cpu_affinity_mask", Json::string(bench::u64_to_string(cfg.affinity_mask)));
    prot.set("high_priority", Json::boolean(cfg.high_priority));
    prot.set("relative_tolerance", Json::number(bench::kRelativeTolerance));
    prot.set("fixed_flags", to_json_array(cfg.fixed_flags));
    Json m = Json::object();
    m.set("experiment_id", Json::string(cfg.experiment_id));
    m.set("created_utc", Json::string(bench::utc_timestamp()));
    m.set("kind", Json::string("holdout-eval"));
    m.set("benchmark", Json::string(benchmark));
    m.set("source_hash", Json::string(hash));
    m.set("kernel_dir", Json::string(kernel_dir));
    const Json* src_id = manifest.find("experiment_id");
    m.set("source_tune_experiment", Json::string(src_id && src_id->type() == Json::Type::String ? src_id->as_string() : ""));
    m.set("source_tune_dir", Json::string(cfg.tune_dir));
    m.set("selected_candidate", Json::string(winner->as_string()));
    m.set("selected_by_strategy", Json::string(tune_strategy));
    m.set("note", Json::string("measurements on sizes the search did not use; nothing here feeds back into any selection or history"));
    m.set("protocol", prot);
    m.set("environment", bench::environment_to_json(env));
    m.set("commands", commands);
    result.manifest = m;
    return true;
}

std::string format_holdout_report(const std::vector<RunRecord>& records) {
    std::ostringstream os;
    std::set<std::int64_t> sizes;
    for (const RunRecord& r : records) sizes.insert(r.input_size);
    for (std::int64_t n : sizes) {
        const RunRecord *o2 = nullptr, *o3 = nullptr, *win = nullptr;
        for (const RunRecord& r : records) {
            if (r.input_size != n) continue;
            if (r.candidate_id == "O2") o2 = &r;
            else if (r.candidate_id == "O3") o3 = &r;
            else win = &r;
        }
        std::string role = "?";
        for (const RunRecord& r : records)
            if (r.input_size == n) {
                role = r.size_role;
                break;
            }
        os << "size " << bench::i64_to_string(n) << " (" << role << ")" << chr_nl();
        for (const RunRecord* r : {o2, o3, win}) {
            if (!r) continue;
            char line[300];
            if (r->correctness_status == "PASS") {
                std::snprintf(line, sizeof line, "  %-32s %10.4f ms  IQR %8.4f  (%d samples)\n", r->candidate_id.c_str(), r->median_ms, r->iqr_ms,
                              static_cast<int>(r->timing_samples_ms.size()));
            } else {
                std::snprintf(line, sizeof line, "  %-32s %s: %s\n", r->candidate_id.c_str(), r->correctness_status.c_str(), r->failure_reason.c_str());
            }
            os << line;
        }
        const RunRecord* best_base = nullptr;
        for (const RunRecord* b : {o2, o3})
            if (b && b->correctness_status == "PASS" && (!best_base || b->median_ms < best_base->median_ms)) best_base = b;
        if (win && win->correctness_status == "PASS" && best_base) {
            double ratio = best_base->median_ms / win->median_ms;
            bool overlap = !(win->q3_ms < best_base->q1_ms || best_base->q3_ms < win->q1_ms);
            char line[300];
            std::snprintf(line, sizeof line, "  selected vs best baseline (%s): median ratio %.3f; interquartile ranges %s%s\n", best_base->candidate_id.c_str(),
                          ratio, overlap ? "overlap -> inconclusive" : "are disjoint", (!overlap && ratio < 1.0) ? " (selected candidate is SLOWER here)" : "");
            os << line;
        } else if (!win) {
            os << "  (the selected candidate is a baseline)\n";
        }
        os << "\n";
    }
    return os.str();
}

int run_evaluate_command(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    EvalConfig cfg;
    std::string out_dir;
    auto usage = [&](const std::string& msg) {
        err << "nova evaluate: " << msg << "\nTry 'nova --help' for usage.\n";
        return 2;
    };
    for (const std::string& a : args) {
        if (a.rfind("--", 0) != 0) {
            if (!cfg.tune_dir.empty()) return usage("'evaluate' takes exactly one tuning result directory");
            cfg.tune_dir = a;
            continue;
        }
        size_t eq = a.find('=');
        std::string key = a.substr(0, eq), val = eq == std::string::npos ? "" : a.substr(eq + 1);
        auto num = [&](long long& v) {
            if (val.empty()) return false;
            char* end = nullptr;
            v = std::strtoll(val.c_str(), &end, 10);
            return *end == '\0';
        };
        long long n = 0;
        if (key == "--high-priority" && eq == std::string::npos) {
            cfg.high_priority = true;
        } else if (eq == std::string::npos) {
            return usage("unknown option '" + a + "'");
        } else if (key == "--sizes") {
            size_t pos = 0;
            while (true) {
                size_t c = val.find(',', pos);
                std::string tok = val.substr(pos, c == std::string::npos ? std::string::npos : c - pos);
                char* end = nullptr;
                long long v = std::strtoll(tok.c_str(), &end, 10);
                if (tok.empty() || *end != '\0' || v <= 0) return usage("bad size '" + tok + "'");
                cfg.sizes.push_back(v);
                if (c == std::string::npos) break;
                pos = c + 1;
            }
        } else if (key == "--reps") {
            if (!num(n) || n < 1 || n > 10000) return usage("--reps must be 1..10000");
            cfg.repetitions = static_cast<int>(n);
        } else if (key == "--warmup") {
            if (!num(n) || n < 0 || n > 1000) return usage("--warmup must be 0..1000");
            cfg.warmup_runs = static_cast<int>(n);
        } else if (key == "--affinity") {
            char* end = nullptr;
            unsigned long long mask = std::strtoull(val.c_str(), &end, 16);
            if (val.empty() || *end != '\0' || mask == 0) return usage("--affinity must be a non-zero hex mask");
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
            return usage("unknown option '" + key + "'");
        }
    }
    if (cfg.tune_dir.empty()) return usage("'evaluate' requires a tuning result directory");
    if (cfg.experiment_id.empty()) {
        std::string compact;
        for (char c : bench::utc_timestamp())
            if (c >= '0' && c <= '9') compact.push_back(c);
        cfg.experiment_id = "holdout-" + compact;
    }
    cfg.out_dir = out_dir.empty() ? "benchmarks/results/" + cfg.experiment_id : out_dir;
    if (cfg.out_dir == cfg.tune_dir) return usage("--out must differ from the tuning result directory (the tuning result is never modified)");

    bench::ExperimentResult result;
    std::string error;
    out << "evaluating " << cfg.tune_dir << " -> " << cfg.out_dir << "\n";
    if (!run_evaluate(cfg, Whitelist::production(), result, error, &out)) {
        err << "nova evaluate: " << error << "\n";
        return 1;
    }
    bench::BenchConfig bc;
    bc.out_dir = cfg.out_dir;
    bc.experiment_id = cfg.experiment_id;
    if (!bench::write_experiment(bc, result, error)) {
        err << "nova evaluate: " << error << "\n";
        return 1;
    }
    out << "\n" << format_holdout_report(result.records) << "results written to " << cfg.out_dir << "\n";
    int bad = 0;
    for (const RunRecord& r : result.records)
        for (const std::string& p : bench::validate_record(r)) {
            err << "nova evaluate: record " << r.candidate_id << " n=" << bench::i64_to_string(r.input_size) << ": " << p << "\n";
            ++bad;
        }
    return bad == 0 ? 0 : 1;
}

}  // namespace tune
}  // namespace nova

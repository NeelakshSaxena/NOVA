#include "nova/bench/harness.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <set>
#include <sstream>

#include "nova/bench/process.hpp"
#include "nova/bench/sha256.hpp"
#include "nova/bench/stats.hpp"
#include "nova/bench/util.hpp"

namespace nova {
namespace bench {

namespace {

using Clock = std::chrono::steady_clock;

double seconds_since(Clock::time_point t0) { return std::chrono::duration<double>(Clock::now() - t0).count(); }

std::string first_nonempty_line(const std::string& s) {
    std::istringstream in(s);
    for (std::string line; std::getline(in, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty()) return line;
    }
    return "";
}

Json to_json_array(const std::vector<std::string>& v) {
    Json a = Json::array();
    for (const std::string& s : v) a.push(Json::string(s));
    return a;
}

const char* exe_suffix() {
#ifdef _WIN32
    return ".exe";
#else
    return "";
#endif
}

std::string fmt_ms(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.4f", v);
    return buf;
}

}  // namespace

BuildOutcome build_kernel(const std::string& compiler, const std::vector<std::string>& flags,
                          const std::string& source, const std::string& exe, int timeout_ms) {
    BuildOutcome b;
    b.command.push_back(compiler);
    for (const std::string& f : flags) b.command.push_back(f);
    b.command.push_back(source);
    b.command.push_back("-o");
    b.command.push_back(exe);
    Clock::time_point t0 = Clock::now();
    ProcessResult r = run_process(b.command, timeout_ms);
    b.seconds = seconds_since(t0);
    b.log = r.out + r.err;
    if (!r.started) {
        b.reason = r.error;
    } else if (r.timed_out) {
        b.reason = "compiler timed out after " + std::to_string(timeout_ms) + " ms";
    } else if (r.exit_code != 0) {
        std::string line = first_nonempty_line(r.err.empty() ? r.out : r.err);
        b.reason = "compiler exited with code " + std::to_string(r.exit_code) + (line.empty() ? "" : ": " + line);
    } else {
        b.binary_size = file_size(exe);
        if (b.binary_size <= 0) {
            b.reason = "compiler reported success but produced no binary";
        } else {
            b.ok = true;
        }
    }
    return b;
}

KernelRun run_kernel(const std::string& exe, std::int64_t n, std::uint64_t seed, int timeout_ms,
                     const KernelOutput* reference, std::uint64_t affinity_mask, bool high_priority) {
    KernelRun k;
    ProcessOptions opts;
    opts.timeout_ms = timeout_ms;
    opts.cpu_affinity_mask = affinity_mask;
    opts.high_priority = high_priority;
    ProcessResult r = run_process({exe, i64_to_string(n), u64_to_string(seed)}, opts);
    k.process_wall_ms = r.wall_ms;
    if (!r.started) {
        k.reason = r.error;
        return k;
    }
    if (r.timed_out) {
        k.timed_out = true;
        k.reason = "run timed out after " + std::to_string(timeout_ms) + " ms";
        return k;
    }
    if (r.exit_code != 0) {
        k.reason = "kernel exited with code " + std::to_string(r.exit_code);
        return k;
    }
    std::string err;
    if (!parse_kernel_output(r.out, k.output, err)) {
        k.reason = err;
        return k;
    }
    k.ok = true;
    if (reference) k.mismatches = compare_to_reference(*reference, k.output);
    return k;
}

const KernelOutput* ReferenceCache::get(const std::string& benchmark, std::int64_t n, std::uint64_t seed,
                                        std::string& error) {
    std::string key = benchmark + "/" + i64_to_string(n) + "/" + u64_to_string(seed);
    auto it = cache_.find(key);
    if (it != cache_.end()) return &it->second;
    KernelOutput out;
    if (!reference_output(benchmark, n, seed, out, error)) return nullptr;
    return &cache_.emplace(key, out).first->second;
}

bool kernel_source_hash(const std::string& kernel_dir, const std::string& benchmark, std::string& hash,
                        std::string& error) {
    std::string src, common;
    if (!read_file(kernel_dir + "/" + benchmark + ".cpp", src)) {
        error = "cannot read kernel source '" + kernel_dir + "/" + benchmark + ".cpp'";
        return false;
    }
    read_file(kernel_dir + "/common.hpp", common);  // optional
    hash = sha256_hex(src + "\n--- common.hpp ---\n" + common);
    return true;
}

std::vector<Variant> baseline_variants(bool include_ofast) {
    std::vector<Variant> v = {{"O2", {"-O2"}, "strict"}, {"O3", {"-O3"}, "strict"}};
    if (include_ofast) v.push_back({"Ofast", {"-Ofast"}, "fast-math"});
    return v;
}

namespace {

std::string role_of(const BenchmarkDef& def, std::int64_t n) {
    if (std::find(def.tuning_sizes.begin(), def.tuning_sizes.end(), n) != def.tuning_sizes.end()) return "tuning";
    if (std::find(def.holdout_sizes.begin(), def.holdout_sizes.end(), n) != def.holdout_sizes.end()) return "holdout";
    return "adhoc";
}


}  // namespace

namespace {

void fail_slot(TimingSlot& s, const std::string& reason) {
    s.active = false;
    s.record.correctness_status = "FAIL";
    s.record.lifecycle = "CORRECTNESS_FAILED";
    s.record.failure_reason = reason;
    s.record.timing_samples_ms.clear();
}

}  // namespace

bool validate_and_time(std::vector<TimingSlot>& slots, const std::string& benchmark, std::int64_t n,
                       const TimingParams& p, std::uint64_t order_salt, ReferenceCache& refs,
                       std::vector<RawSample>& raw, Json* commands, std::ostream* log, std::string& error) {
    const KernelOutput* ref = refs.get(benchmark, n, p.seed, error);
    if (!ref) return false;
    // Correctness first: the validation run is discarded (it also serves as a first warm-up).
    for (TimingSlot& s : slots) {
        RunRecord& r = s.record;
        if (r.build_status != "OK") {
            r.lifecycle = "BUILD_FAILED";
            s.active = false;
            continue;
        }
        KernelRun k = run_kernel(s.exe, n, p.seed, p.run_timeout_ms, ref, p.affinity_mask, p.high_priority);
        if (commands) {
            Json c = Json::object();
            c.set("phase", Json::string("validate"));
            c.set("candidate_id", Json::string(r.candidate_id));
            c.set("argv", to_json_array({s.exe, i64_to_string(n), u64_to_string(p.seed)}));
            commands->push(c);
        }
        if (!k.ok) {
            fail_slot(s, k.reason);
        } else if (!k.mismatches.empty()) {
            fail_slot(s, "output differs from the reference: " + k.mismatches[0]);
        } else {
            s.active = true;
            r.correctness_status = "PASS";
        }
        if (log) *log << "  validate " << r.candidate_id << " n=" << i64_to_string(n) << ": " << r.correctness_status << "\n";
    }
    // Warm-up runs (discarded).
    for (TimingSlot& s : slots) {
        for (int w = 0; w < p.warmup_runs && s.active; ++w) {
            KernelRun k = run_kernel(s.exe, n, p.seed, p.run_timeout_ms, ref, p.affinity_mask, p.high_priority);
            if (!k.ok) fail_slot(s, "warm-up run failed: " + k.reason);
            else if (!k.mismatches.empty()) fail_slot(s, "warm-up output differs from the reference: " + k.mismatches[0]);
        }
    }
    // Timed rounds: every round runs all still-active variants once, in a seeded random order.
    for (int rep = 1; rep <= p.repetitions; ++rep) {
        std::vector<size_t> order;
        for (size_t vi = 0; vi < slots.size(); ++vi)
            if (slots[vi].active) order.push_back(vi);
        Rng rng(p.order_seed ^ (static_cast<std::uint64_t>(rep) * 0x9E3779B1ULL) ^ (static_cast<std::uint64_t>(n) << 8) ^
                (order_salt * 7919ULL));
        shuffle(order, rng);
        for (size_t pos = 0; pos < order.size(); ++pos) {
            TimingSlot& s = slots[order[pos]];
            KernelRun k = run_kernel(s.exe, n, p.seed, p.run_timeout_ms, ref, p.affinity_mask, p.high_priority);
            if (!k.ok) {
                fail_slot(s, "timed run " + std::to_string(rep) + " failed: " + k.reason);
                continue;
            }
            if (!k.mismatches.empty()) {
                fail_slot(s, "timed run " + std::to_string(rep) + " output differs from the reference: " + k.mismatches[0]);
                continue;
            }
            s.record.timing_samples_ms.push_back(k.output.time_ms);
            RawSample sample;
            sample.experiment_id = p.experiment_id;
            sample.candidate_id = s.record.candidate_id;
            sample.benchmark = benchmark;
            sample.input_size = n;
            sample.repetition = rep;
            sample.order_position = static_cast<int>(pos) + 1;
            sample.time_ms = k.output.time_ms;
            sample.process_wall_ms = k.process_wall_ms;
            raw.push_back(sample);
        }
    }
    for (TimingSlot& s : slots) {
        // Raw samples of a variant that failed mid-way are dropped from the record (and from raw output).
        if (s.record.correctness_status == "FAIL") {
            std::vector<RawSample> kept;
            for (const RawSample& r : raw)
                if (!(r.candidate_id == s.record.candidate_id && r.input_size == n && r.benchmark == benchmark)) kept.push_back(r);
            raw.swap(kept);
        }
        compute_statistics(s.record);
        if (s.record.correctness_status == "PASS") s.record.lifecycle = "BENCHMARKED";
        if (log && s.record.correctness_status == "PASS")
            *log << "  " << s.record.candidate_id << " n=" << i64_to_string(n) << " median " << fmt_ms(s.record.median_ms)
                 << " ms (IQR " << fmt_ms(s.record.iqr_ms) << ", " << s.record.timing_samples_ms.size() << " samples)\n";
    }
    return true;
}

bool run_experiment(const BenchConfig& cfg, ExperimentResult& result, std::string& error, std::ostream* log) {
    if (cfg.experiment_id.empty()) {
        error = "experiment id is required";
        return false;
    }
    if (cfg.out_dir.empty()) {
        error = "output directory is required";
        return false;
    }
    if (cfg.repetitions < 1) {
        error = "repetitions must be >= 1";
        return false;
    }
    if (cfg.warmup_runs < 0) {
        error = "warmup runs must be >= 0";
        return false;
    }
    std::vector<std::string> names = cfg.benchmarks;
    if (names.empty())
        for (const BenchmarkDef& d : registry()) names.push_back(d.name);
    for (const std::string& n : names) {
        if (!find_benchmark(n)) {
            error = "unknown benchmark '" + n + "'";
            return false;
        }
    }
    if (cfg.role != "tuning" && cfg.role != "holdout" && cfg.role != "all") {
        error = "role must be tuning, holdout or all";
        return false;
    }
    if (!make_dirs(cfg.out_dir + "/bin")) {
        error = "cannot create output directory '" + cfg.out_dir + "'";
        return false;
    }

    Clock::time_point t_all = Clock::now();
    Environment env = collect_environment(cfg.compiler);
    if (env.compiler_version.empty()) {
        error = "cannot run compiler '" + cfg.compiler + "' (is it on PATH?)";
        return false;
    }
    std::vector<std::string> missing = missing_environment_fields(env);

    ReferenceCache refs;
    Json commands = Json::array();
    Json bench_info = Json::array();
    std::vector<Variant> variants = baseline_variants(cfg.include_ofast);
    result.records.clear();
    result.raw.clear();

    for (size_t bi = 0; bi < names.size(); ++bi) {
        const BenchmarkDef& def = *find_benchmark(names[bi]);
        std::string hash, herr;
        if (!kernel_source_hash(cfg.kernel_dir, def.name, hash, herr)) {
            error = herr;
            return false;
        }
        std::vector<std::int64_t> sizes = cfg.sizes;
        if (sizes.empty()) {
            if (cfg.role != "holdout") sizes.insert(sizes.end(), def.tuning_sizes.begin(), def.tuning_sizes.end());
            if (cfg.role != "tuning") sizes.insert(sizes.end(), def.holdout_sizes.begin(), def.holdout_sizes.end());
        }
        Json bj = Json::object();
        bj.set("name", Json::string(def.name));
        bj.set("source_hash", Json::string(hash));
        Json sj = Json::array();
        for (std::int64_t n : sizes) {
            Json e = Json::object();
            e.set("size", Json::integer(n));
            e.set("role", Json::string(role_of(def, n)));
            sj.push(e);
        }
        bj.set("sizes", sj);
        bench_info.push(bj);

        // Build every variant once (the binary takes the input size at run time).
        std::vector<BuildOutcome> builds;
        std::vector<std::string> exes;
        for (const Variant& v : variants) {
            std::string id = def.name + "-" + v.opt_level;
            std::string exe = cfg.out_dir + "/bin/" + def.name + "_" + v.opt_level + exe_suffix();
            std::vector<std::string> flags = cfg.fixed_flags;
            flags.insert(flags.end(), v.flags.begin(), v.flags.end());
            if (log) *log << "build " << id << " ..." << std::flush;
            BuildOutcome b = build_kernel(cfg.compiler, flags, cfg.kernel_dir + "/" + def.name + ".cpp", exe,
                                          cfg.build_timeout_ms);
            if (log) *log << (b.ok ? " ok (" : " FAILED (") << fmt_ms(b.seconds) << " s)\n";
            Json c = Json::object();
            c.set("phase", Json::string("build"));
            c.set("candidate_id", Json::string(id));
            c.set("argv", to_json_array(b.command));
            c.set("exit_ok", Json::boolean(b.ok));
            commands.push(c);
            result.build_logs[id] = b.log;
            builds.push_back(b);
            exes.push_back(exe);
        }

        for (std::int64_t n : sizes) {
            std::vector<TimingSlot> slots(variants.size());
            for (size_t vi = 0; vi < variants.size(); ++vi) {
                TimingSlot& s = slots[vi];
                RunRecord& r = s.record;
                const Variant& v = variants[vi];
                std::vector<std::string> flags = cfg.fixed_flags;
                flags.insert(flags.end(), v.flags.begin(), v.flags.end());
                r.experiment_id = cfg.experiment_id;
                r.benchmark = def.name;
                r.source_hash = hash;
                r.compiler_version = env.compiler_version;
                r.flags = join(flags);
                r.opt_level = v.opt_level;
                r.fp_semantics = v.fp_semantics;
                r.input_size = n;
                r.size_role = role_of(def, n);
                r.seed = cfg.seed;
                r.candidate_id = def.name + "-" + v.opt_level;
                r.warmup_runs = cfg.warmup_runs;
                r.repetitions = cfg.repetitions;
                r.search_strategy = "baseline";
                r.binary_size_bytes = builds[vi].binary_size;
                r.build_time_s = builds[vi].seconds;
                s.exe = exes[vi];
                if (!builds[vi].ok) {
                    r.build_status = "FAILED";
                    r.correctness_status = "NOT_RUN";
                    r.failure_reason = builds[vi].reason;
                } else {
                    r.build_status = "OK";
                }
            }
            TimingParams tp;
            tp.experiment_id = cfg.experiment_id;
            tp.repetitions = cfg.repetitions;
            tp.warmup_runs = cfg.warmup_runs;
            tp.seed = cfg.seed;
            tp.order_seed = cfg.order_seed;
            tp.run_timeout_ms = cfg.run_timeout_ms;
            tp.affinity_mask = cfg.affinity_mask;
            tp.high_priority = cfg.high_priority;
            if (!validate_and_time(slots, def.name, n, tp, bi, refs, result.raw, &commands, log, error)) return false;
            for (TimingSlot& s : slots) result.records.push_back(s.record);
        }
    }

    Json prot = Json::object();
    prot.set("repetitions", Json::integer(cfg.repetitions));
    prot.set("warmup_runs", Json::integer(cfg.warmup_runs));
    prot.set("validation_run_discarded", Json::boolean(true));
    prot.set("input_seed", Json::string(u64_to_string(cfg.seed)));
    prot.set("order_seed", Json::string(u64_to_string(cfg.order_seed)));
    prot.set("order", Json::string("interleaved rounds, seeded Fisher-Yates shuffle of variants per round"));
    prot.set("timed_region", Json::string("kernel computation only (in-process QueryPerformanceCounter/steady_clock)"));
    prot.set("relative_tolerance", Json::number(kRelativeTolerance));
    prot.set("tolerance_rule", Json::string("|actual-expected| <= tol*max(1,|expected|) for sum,s0,s1,s2,wsum"));
    prot.set("fixed_flags", to_json_array(cfg.fixed_flags));
    prot.set("build_timeout_ms", Json::integer(cfg.build_timeout_ms));
    prot.set("run_timeout_ms", Json::integer(cfg.run_timeout_ms));
    prot.set("cpu_affinity_mask", Json::string(u64_to_string(cfg.affinity_mask)));
    prot.set("high_priority", Json::boolean(cfg.high_priority));

    Json m = Json::object();
    m.set("experiment_id", Json::string(cfg.experiment_id));
    m.set("created_utc", Json::string(utc_timestamp()));
    m.set("kind", Json::string("baseline"));
    m.set("kernel_dir", Json::string(cfg.kernel_dir));
    m.set("protocol", prot);
    m.set("environment", environment_to_json(env));
    m.set("missing_environment_fields", to_json_array(missing));
    m.set("benchmarks", bench_info);
    Json vj = Json::array();
    for (const Variant& v : variants) {
        Json o = Json::object();
        o.set("opt_level", Json::string(v.opt_level));
        o.set("flags", to_json_array(v.flags));
        o.set("fp_semantics", Json::string(v.fp_semantics));
        vj.push(o);
    }
    m.set("variants", vj);
    m.set("commands", commands);
    m.set("wall_time_s", Json::number(seconds_since(t_all)));
    result.manifest = m;
    return true;
}

bool write_experiment(const BenchConfig& cfg, const ExperimentResult& r, std::string& error) {
    if (!make_dirs(cfg.out_dir + "/logs")) {
        error = "cannot create '" + cfg.out_dir + "/logs'";
        return false;
    }
    Json recs = Json::array();
    std::string csv = csv_header() + "\n";
    for (const RunRecord& rec : r.records) {
        recs.push(record_to_json(rec));
        csv += record_to_csv_row(rec) + "\n";
    }
    Json results = Json::object();
    results.set("experiment_id", Json::string(cfg.experiment_id));
    results.set("records", recs);
    std::string raw = raw_csv_header() + "\n";
    for (const RawSample& s : r.raw) raw += raw_to_csv_row(s) + "\n";
    bool ok = write_file(cfg.out_dir + "/manifest.json", r.manifest.dump(2) + "\n") &&
              write_file(cfg.out_dir + "/results.json", results.dump(2) + "\n") &&
              write_file(cfg.out_dir + "/results.csv", csv) && write_file(cfg.out_dir + "/raw_timings.csv", raw);
    for (const auto& kv : r.build_logs) ok = ok && write_file(cfg.out_dir + "/logs/build_" + kv.first + ".log", kv.second);
    if (!ok) error = "failed to write result files into '" + cfg.out_dir + "'";
    return ok;
}

bool load_experiment(const std::string& dir, Json& manifest, std::vector<RunRecord>& records, std::string& error) {
    std::string text;
    if (!read_file(dir + "/manifest.json", text)) {
        error = "cannot read " + dir + "/manifest.json";
        return false;
    }
    std::string perr;
    if (!Json::parse(text, manifest, perr)) {
        error = "manifest.json: " + perr;
        return false;
    }
    if (!read_file(dir + "/results.json", text)) {
        error = "cannot read " + dir + "/results.json";
        return false;
    }
    Json results;
    if (!Json::parse(text, results, perr)) {
        error = "results.json: " + perr;
        return false;
    }
    const Json* arr = results.find("records");
    if (!arr || arr->type() != Json::Type::Array) {
        error = "results.json has no 'records' array";
        return false;
    }
    records.clear();
    for (size_t i = 0; i < arr->items().size(); ++i) {
        RunRecord rec;
        if (!record_from_json(arr->items()[i], rec, perr)) {
            error = "results.json record " + std::to_string(i) + ": " + perr;
            return false;
        }
        records.push_back(rec);
    }
    return true;
}

std::string format_report(const std::vector<RunRecord>& records) {
    std::ostringstream os;
    std::set<std::pair<std::string, std::int64_t>> groups;
    for (const RunRecord& r : records) groups.insert({r.benchmark, r.input_size});
    for (const auto& g : groups) {
        os << g.first << "  n=" << i64_to_string(g.second) << "\n";
        os << "  candidate           status   median_ms     IQR_ms   IQR/med   min_ms   samples  bytes\n";
        const RunRecord* o2 = nullptr;
        const RunRecord* o3 = nullptr;
        for (const RunRecord& r : records) {
            if (r.benchmark != g.first || r.input_size != g.second) continue;
            std::string label = r.opt_level + (r.fp_semantics == "fast-math" ? " [fast-math]" : "");
            char line[200];
            if (r.correctness_status == "PASS") {
                std::snprintf(line, sizeof line, "  %-19s PASS %12.4f %10.4f %8.1f%% %9.4f %8d  %s\n", label.c_str(),
                              r.median_ms, r.iqr_ms, r.median_ms > 0 ? 100.0 * r.iqr_ms / r.median_ms : 0.0, r.min_ms,
                              static_cast<int>(r.timing_samples_ms.size()), i64_to_string(r.binary_size_bytes).c_str());
            } else {
                std::snprintf(line, sizeof line, "  %-19s %s (build %s): %s\n", label.c_str(),
                              r.correctness_status.c_str(), r.build_status.c_str(), r.failure_reason.c_str());
            }
            os << line;
            if (r.correctness_status == "PASS" && r.fp_semantics == "strict") {
                if (r.opt_level == "O2") o2 = &r;
                if (r.opt_level == "O3") o3 = &r;
            }
        }
        if (o2 && o3) {
            double ratio = o2->median_ms / o3->median_ms;
            bool overlap = !(o3->q3_ms < o2->q1_ms || o2->q3_ms < o3->q1_ms);
            char line[220];
            std::snprintf(line, sizeof line, "  median(O2)/median(O3) = %.3f over %d samples each; interquartile ranges %s -> %s\n",
                          ratio, static_cast<int>(o2->timing_samples_ms.size()), overlap ? "overlap" : "are disjoint",
                          overlap ? "inconclusive" : (ratio > 1 ? "O3 faster in this measurement" : "O2 faster in this measurement"));
            os << line;
        }
        os << "\n";
    }
    return os.str();
}

}  // namespace bench
}  // namespace nova

#include "nova/tune/study.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ostream>
#include <set>
#include <sstream>

#include "nova/bench/env.hpp"
#include "nova/bench/registry.hpp"
#include "nova/bench/stats.hpp"
#include "nova/bench/util.hpp"
#include "nova/tune/features.hpp"

namespace nova {
namespace tune {

using bench::Json;
using bench::RunRecord;

namespace {

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

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// nova table
// ---------------------------------------------------------------------------------------------------------------------

bool run_table(const TableConfig& cfg, const SearchSpace& space, const Whitelist& whitelist, bench::ExperimentResult& result,
               std::string& error, std::ostream* log) {
    if (cfg.experiment_id.empty() || cfg.out_dir.empty()) {
        error = "experiment id and output directory are required";
        return false;
    }
    const bench::BenchmarkDef* def = bench::find_benchmark(cfg.benchmark);
    if (!def) {
        error = "unknown benchmark '" + cfg.benchmark + "'";
        return false;
    }
    if (cfg.repetitions < 1 || cfg.warmup_runs < 0) {
        error = "repetitions must be >= 1 and warm-up runs >= 0";
        return false;
    }
    if (cfg.role != "tuning" && cfg.role != "holdout" && cfg.role != "all") {
        error = "role must be tuning, holdout or all";
        return false;
    }
    std::vector<Candidate> all = space.baselines;
    all.insert(all.end(), space.candidates.begin(), space.candidates.end());
    for (const Candidate& c : all) {
        std::string why;
        if (!validate_flags(c.flags, whitelist, why)) {
            error = "invalid candidate '" + c.id + "': " + why;
            return false;
        }
    }
    std::vector<std::int64_t> sizes = cfg.sizes;
    if (sizes.empty()) {
        if (cfg.role != "holdout") sizes.insert(sizes.end(), def->tuning_sizes.begin(), def->tuning_sizes.end());
        if (cfg.role != "tuning") sizes.insert(sizes.end(), def->holdout_sizes.begin(), def->holdout_sizes.end());
    }
    for (std::int64_t n : sizes)
        if (n <= 0 || (cfg.benchmark == "stencil" && n < 3)) {
            error = "invalid size " + bench::i64_to_string(n);
            return false;
        }
    bench::Environment env = bench::collect_environment(cfg.compiler);
    if (env.compiler_version.empty()) {
        error = "cannot run compiler '" + cfg.compiler + "' (is it on PATH?)";
        return false;
    }
    std::string hash;
    if (!bench::kernel_source_hash(cfg.kernel_dir, cfg.benchmark, hash, error)) return false;
    if (!bench::make_dirs(cfg.out_dir + "/bin")) {
        error = "cannot create output directory '" + cfg.out_dir + "'";
        return false;
    }

    result = bench::ExperimentResult();
    Json commands = Json::array();
    std::vector<bench::BuildOutcome> builds;
    std::vector<std::string> exes;
    for (size_t i = 0; i < all.size(); ++i) {
        std::vector<std::string> flags = cfg.fixed_flags;
        flags.insert(flags.end(), all[i].flags.begin(), all[i].flags.end());
        std::string exe = cfg.out_dir + "/bin/" + cfg.benchmark + "_" + std::to_string(i) + exe_suffix();
        if (log) *log << "build " << all[i].id << " ..." << std::flush;
        bench::BuildOutcome b = bench::build_kernel(cfg.compiler, flags, cfg.kernel_dir + "/" + cfg.benchmark + ".cpp", exe, cfg.build_timeout_ms);
        if (log) *log << (b.ok ? " ok" : " FAILED: " + b.reason) << "\n";
        Json c = Json::object();
        c.set("phase", Json::string("build"));
        c.set("candidate_id", Json::string(all[i].id));
        c.set("argv", to_json_array(b.command));
        c.set("exit_ok", Json::boolean(b.ok));
        commands.push(c);
        result.build_logs[all[i].id] = b.log;
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
    Json size_info = Json::array();
    std::uint64_t salt = 0;
    for (std::int64_t n : sizes) {
        bool is_holdout = std::find(def->holdout_sizes.begin(), def->holdout_sizes.end(), n) != def->holdout_sizes.end();
        bool is_tuning = std::find(def->tuning_sizes.begin(), def->tuning_sizes.end(), n) != def->tuning_sizes.end();
        std::string role = is_holdout ? "holdout" : (is_tuning ? "tuning" : "adhoc");
        Json si = Json::object();
        si.set("size", Json::integer(n));
        si.set("role", Json::string(role));
        size_info.push(si);
        std::vector<bench::TimingSlot> slots(all.size());
        for (size_t i = 0; i < all.size(); ++i) {
            RunRecord& r = slots[i].record;
            std::vector<std::string> flags = cfg.fixed_flags;
            flags.insert(flags.end(), all[i].flags.begin(), all[i].flags.end());
            r.experiment_id = cfg.experiment_id;
            r.benchmark = cfg.benchmark;
            r.source_hash = hash;
            r.compiler_version = env.compiler_version;
            r.flags = bench::join(flags);
            r.opt_level = all[i].flags[0].substr(1);
            r.fp_semantics = "strict";
            r.input_size = n;
            r.size_role = role;
            r.seed = cfg.seed;
            r.candidate_id = all[i].id;
            r.warmup_runs = cfg.warmup_runs;
            r.repetitions = cfg.repetitions;
            r.search_strategy = all[i].baseline ? "baseline" : "table";
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
        if (!bench::validate_and_time(slots, cfg.benchmark, n, tp, salt++, refs, result.raw, &commands, log, error)) return false;
        for (bench::TimingSlot& s : slots) result.records.push_back(s.record);
    }
    Json prot = Json::object();
    prot.set("repetitions", Json::integer(cfg.repetitions));
    prot.set("warmup_runs", Json::integer(cfg.warmup_runs));
    prot.set("input_seed", Json::string(bench::u64_to_string(cfg.seed)));
    prot.set("order_seed", Json::string(bench::u64_to_string(cfg.order_seed)));
    prot.set("order", Json::string("interleaved rounds over ALL configurations, seeded Fisher-Yates shuffle per round"));
    prot.set("cpu_affinity_mask", Json::string(bench::u64_to_string(cfg.affinity_mask)));
    prot.set("high_priority", Json::boolean(cfg.high_priority));
    prot.set("relative_tolerance", Json::number(bench::kRelativeTolerance));
    prot.set("fixed_flags", to_json_array(cfg.fixed_flags));
    Json cflags = Json::object();
    for (const Candidate& c : all) cflags.set(c.id, to_json_array(c.flags));
    Json m = Json::object();
    m.set("experiment_id", Json::string(cfg.experiment_id));
    m.set("created_utc", Json::string(bench::utc_timestamp()));
    m.set("kind", Json::string("table"));
    m.set("benchmark", Json::string(cfg.benchmark));
    m.set("source_hash", Json::string(hash));
    m.set("kernel_dir", Json::string(cfg.kernel_dir));
    m.set("sizes", size_info);
    m.set("candidate_flags", cflags);
    m.set("protocol", prot);
    m.set("environment", bench::environment_to_json(env));
    m.set("missing_environment_fields", to_json_array(bench::missing_environment_fields(env)));
    m.set("commands", commands);
    m.set("note", Json::string("exhaustive measurement of every configuration; nothing is selected here"));
    result.manifest = m;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// loading tables
// ---------------------------------------------------------------------------------------------------------------------

bool load_tables(const std::string& dir, const SearchSpace& space, double max_binary_growth, std::vector<WorkloadTable>& out,
                 std::string& error) {
    Json manifest;
    std::vector<RunRecord> records;
    if (!bench::load_experiment(dir, manifest, records, error)) return false;
    const Json* kind = manifest.find("kind");
    if (!kind || kind->type() != Json::Type::String || kind->as_string() != "table") {
        error = "'" + dir + "' is not a `nova table` result";
        return false;
    }
    std::map<std::pair<std::string, std::int64_t>, WorkloadTable> grouped;
    for (const RunRecord& r : records) {
        std::vector<std::string> problems = bench::validate_record(r);
        if (!problems.empty()) {
            error = dir + ": record " + r.candidate_id + " n=" + bench::i64_to_string(r.input_size) + ": " + problems[0];
            return false;
        }
        WorkloadTable& t = grouped[{r.benchmark, r.input_size}];
        t.benchmark = r.benchmark;
        t.size = r.input_size;
        t.role = r.size_role;
        t.source = r.experiment_id;
        t.source_hash = r.source_hash;
        if (t.entries.count(r.candidate_id)) {
            error = dir + ": duplicate record for " + r.candidate_id + " n=" + bench::i64_to_string(r.input_size);
            return false;
        }
        TableEntry e;
        e.id = r.candidate_id;
        e.lifecycle = r.lifecycle;
        e.failure_reason = r.failure_reason;
        e.median_ms = r.median_ms;
        e.samples_ms = r.timing_samples_ms;
        e.binary_size = r.binary_size_bytes;
        e.eligible = r.correctness_status == "PASS" && static_cast<int>(r.timing_samples_ms.size()) == r.repetitions && r.median_ms > 0;
        t.entries[r.candidate_id] = e;
    }
    out.clear();
    for (auto& kv : grouped) {
        WorkloadTable& t = kv.second;
        std::vector<Candidate> all = space.baselines;
        all.insert(all.end(), space.candidates.begin(), space.candidates.end());
        for (const Candidate& c : all)
            if (!t.entries.count(c.id)) {
                error = dir + ": table for " + t.benchmark + " n=" + bench::i64_to_string(t.size) + " lacks configuration " + c.id;
                return false;
            }
        if (t.role != "tuning" && t.role != "holdout") {
            error = dir + ": table for " + t.benchmark + " n=" + bench::i64_to_string(t.size) + " has role '" + t.role + "' (must be tuning or holdout)";
            return false;
        }
        const TableEntry& o2 = t.entries["O2"];
        for (auto& e : t.entries) {
            if (e.second.eligible && max_binary_growth > 0 && o2.binary_size > 0 &&
                static_cast<double>(e.second.binary_size) > max_binary_growth * static_cast<double>(o2.binary_size)) {
                e.second.eligible = false;
                e.second.lifecycle = "REJECTED_BY_POLICY";
            }
        }
        out.push_back(t);
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// replay study
// ---------------------------------------------------------------------------------------------------------------------

namespace {

std::vector<HistoryEntry> history_from_tables(const std::vector<WorkloadTable>& tables, const WorkloadTable& target) {
    std::vector<HistoryEntry> h;
    for (const WorkloadTable& t : tables) {
        if (t.role != "tuning") continue;                                      // never holdout
        if (t.benchmark == target.benchmark && t.size == target.size) continue;  // never the workload being tuned
        auto o2 = t.entries.find("O2");
        if (o2 == t.entries.end() || !o2->second.eligible) continue;
        for (const auto& kv : t.entries) {
            if (kv.first == "O2" || kv.first == "O3") continue;
            HistoryEntry e;
            e.benchmark = t.benchmark;
            e.size = t.size;
            e.size_role = t.role;
            e.candidate = kv.first;
            e.source = t.source;
            if (!kv.second.eligible) e.failed = true;
            else e.log_gain = std::log(o2->second.median_ms / kv.second.median_ms);
            h.push_back(e);
        }
    }
    return h;
}

struct Replay {
    std::vector<int> picks;  // indices into space.candidates in evaluation order
    int failed = 0;
};

double median_of_draw(const std::vector<double>& samples, int reps, bench::Rng& rng) {
    std::vector<size_t> idx(samples.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    bench::shuffle(idx, rng);
    std::vector<double> draw;
    for (int i = 0; i < reps && static_cast<size_t>(i) < idx.size(); ++i) draw.push_back(samples[idx[static_cast<size_t>(i)]]);
    return bench::summarize(draw).median;
}

bool method_ablation(const std::string& method, Ablation& a) {
    if (method == "adaptive") return parse_ablation("none", a);
    if (method == "adaptive-no-features") return parse_ablation("no-features", a);
    if (method == "adaptive-no-history") return parse_ablation("no-history", a);
    if (method == "adaptive-no-feedback") return parse_ablation("no-feedback", a);
    return false;
}

}  // namespace

bool run_study(const std::vector<WorkloadTable>& tables, const SearchSpace& space, const StudyConfig& cfg, StudyResult& out,
               std::string& error) {
    if (cfg.seeds < 1 || cfg.budgets.empty() || cfg.methods.empty() || cfg.pilot_reps < 1) {
        error = "study needs at least one method, budget and seed, and pilot_reps >= 1";
        return false;
    }
    std::set<std::string> known = {"fixed", "random", "adaptive", "adaptive-no-features", "adaptive-no-history", "adaptive-no-feedback",
                                   "baseline-O2", "baseline-O3"};
    for (const std::string& m : cfg.methods)
        if (!known.count(m)) {
            error = "unknown study method '" + m + "'";
            return false;
        }
    out = StudyResult();
    int tuning_tables = 0;
    for (const WorkloadTable& t : tables) tuning_tables += t.role == "tuning";
    if (tuning_tables == 0) {
        error = "no tuning tables were given";
        return false;
    }
    for (const WorkloadTable& T : tables) {
        if (T.role != "tuning") continue;
        const auto o2 = T.entries.find("O2");
        const auto o3 = T.entries.find("O3");
        if (o2 == T.entries.end() || !o2->second.eligible || o3 == T.entries.end() || !o3->second.eligible) {
            error = "tuning table " + T.benchmark + " n=" + bench::i64_to_string(T.size) + " has an ineligible baseline";
            return false;
        }
        const double best_base_t = std::min(o2->second.median_ms, o3->second.median_ms);
        double oracle_med = best_base_t;
        for (const auto& kv : T.entries)
            if (kv.second.eligible) oracle_med = std::min(oracle_med, kv.second.median_ms);
        std::vector<const WorkloadTable*> holdouts;
        for (const WorkloadTable& H : tables)
            if (H.role == "holdout" && H.benchmark == T.benchmark) holdouts.push_back(&H);
        std::vector<HistoryEntry> hist = cfg.history_across_workloads ? history_from_tables(tables, T) : std::vector<HistoryEntry>();
        WorkloadFeatures wf = extract_features(T.benchmark, T.size);
        const std::uint64_t wl_key = static_cast<std::uint64_t>(T.size) * 1000003ULL + T.benchmark.size() * 7919ULL +
                                     static_cast<std::uint64_t>(T.benchmark[0]);

        for (const std::string& method : cfg.methods) {
            const bool deterministic = method == "fixed" || method.rfind("baseline-", 0) == 0;
            const std::vector<int> budgets = method.rfind("baseline-", 0) == 0 ? std::vector<int>{0} : cfg.budgets;
            for (int budget : budgets) {
                for (int seed = deterministic ? 0 : 1; seed <= (deterministic ? 0 : cfg.seeds); ++seed) {
                    Replay rp;
                    std::vector<std::string> sequence;
                    if (method == "fixed" || method == "random") {
                        for (const Candidate& c : select_sequence(space, method == "fixed" ? Strategy::Fixed : Strategy::Random,
                                                                  static_cast<std::uint64_t>(seed), budget)) {
                            for (size_t i = 0; i < space.candidates.size(); ++i)
                                if (space.candidates[i].id == c.id) rp.picks.push_back(static_cast<int>(i));
                        }
                    } else if (method.rfind("adaptive", 0) == 0) {
                        Ablation ab;
                        method_ablation(method, ab);
                        AdaptivePolicy policy(space, wf, hist, ab, static_cast<std::uint64_t>(seed));
                        for (int step = 0; step < budget; ++step) {
                            int idx = policy.next();
                            if (idx < 0) break;
                            rp.picks.push_back(idx);
                            const TableEntry& e = T.entries.at(space.candidates[static_cast<size_t>(idx)].id);
                            if (!e.eligible) {
                                policy.observe(idx, false, 0.0);
                                ++rp.failed;
                                continue;
                            }
                            bench::Rng rng(static_cast<std::uint64_t>(seed) * 0x9E3779B97F4A7C15ULL + wl_key * 0xD1B54A32D192ED03ULL +
                                           static_cast<std::uint64_t>(step) * 0x2545F4914F6CDD1DULL);
                            double am = median_of_draw(o2->second.samples_ms, cfg.pilot_reps, rng);
                            double cm = median_of_draw(e.samples_ms, cfg.pilot_reps, rng);
                            policy.observe(idx, am > 0 && cm > 0, am > 0 && cm > 0 ? std::log(am / cm) : 0.0);
                        }
                    }
                    // Final choice: lowest full-protocol median among the baselines and the evaluated eligible candidates.
                    std::string selected = o2->second.median_ms <= o3->second.median_ms ? "O2" : "O3";
                    double selected_med = best_base_t;
                    if (method == "baseline-O2") {
                        selected = "O2";
                        selected_med = o2->second.median_ms;
                    } else if (method == "baseline-O3") {
                        selected = "O3";
                        selected_med = o3->second.median_ms;
                    }
                    for (int idx : rp.picks) {
                        const TableEntry& e = T.entries.at(space.candidates[static_cast<size_t>(idx)].id);
                        sequence.push_back(e.id);
                        if (e.eligible && e.median_ms < selected_med) {
                            selected = e.id;
                            selected_med = e.median_ms;
                        }
                    }
                    StudyRow row;
                    row.method = method;
                    row.budget = budget;
                    row.seed = seed;
                    row.benchmark = T.benchmark;
                    row.tuning_size = T.size;
                    row.selected = selected;
                    row.evaluations = static_cast<int>(rp.picks.size());
                    for (int idx : rp.picks)
                        if (!T.entries.at(space.candidates[static_cast<size_t>(idx)].id).eligible) ++row.failed_evaluations;
                    row.tuning_speedup = best_base_t / selected_med;
                    row.oracle_speedup = best_base_t / oracle_med;
                    row.regret = selected_med / oracle_med;
                    row.sequence = sequence;
                    double sum = 0;
                    for (const WorkloadTable* H : holdouts) {
                        const auto ho2 = H->entries.find("O2"), ho3 = H->entries.find("O3");
                        const auto hs = H->entries.find(selected);
                        if (ho2 == H->entries.end() || ho3 == H->entries.end() || hs == H->entries.end()) continue;
                        if (!ho2->second.eligible || !ho3->second.eligible) continue;
                        double hb = std::min(ho2->second.median_ms, ho3->second.median_ms);
                        // An ineligible selected configuration on the holdout size (failed there) counts as no usable result: speedup 0.
                        double v = hs->second.eligible ? hb / hs->second.median_ms : 0.0;
                        row.holdout.push_back({H->size, v});
                        sum += v;
                    }
                    row.holdout_mean = row.holdout.empty() ? 0.0 : sum / static_cast<double>(row.holdout.size());
                    out.rows.push_back(row);
                }
            }
        }
    }

    Json tj = Json::array();
    for (const WorkloadTable& t : tables) {
        Json o = Json::object();
        o.set("benchmark", Json::string(t.benchmark));
        o.set("size", Json::integer(t.size));
        o.set("role", Json::string(t.role));
        o.set("source_experiment", Json::string(t.source));
        o.set("source_hash", Json::string(t.source_hash));
        tj.push(o);
    }
    Json cj = Json::object();
    cj.set("methods", to_json_array(cfg.methods));
    Json bj = Json::array();
    for (int b : cfg.budgets) bj.push(Json::integer(b));
    cj.set("budgets", bj);
    cj.set("seeds", Json::integer(cfg.seeds));
    cj.set("pilot_reps", Json::integer(cfg.pilot_reps));
    cj.set("history_across_workloads", Json::boolean(cfg.history_across_workloads));
    Json m = Json::object();
    m.set("kind", Json::string("study"));
    m.set("created_utc", Json::string(bench::utc_timestamp()));
    m.set("config", cj);
    m.set("tables", tj);
    m.set("rows", Json::integer(static_cast<std::int64_t>(out.rows.size())));
    m.set("method", Json::string("replay of search strategies against saved exhaustive tables; pilot gains re-sample saved raw samples; "
                                 "holdout tables are only used to score the final choice"));
    m.set("leakage_rules", Json::string("history = tuning tables of OTHER workloads only; holdout tables never enter history, policy or selection"));
    out.manifest = m;
    return true;
}

namespace {

std::string f6(double v) {
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.6f", v);
    return buf;
}

}  // namespace

std::string study_runs_csv(const std::vector<StudyRow>& rows) {
    std::string s = "method,budget,seed,benchmark,tuning_size,selected,evaluations,failed_evaluations,tuning_speedup,oracle_speedup,regret,holdout_mean_speedup,holdout_speedups,sequence" + std::string(1, 10);
    for (const StudyRow& r : rows) {
        std::string ho;
        for (size_t i = 0; i < r.holdout.size(); ++i) ho += (i ? ";" : "") + bench::i64_to_string(r.holdout[i].first) + ":" + f6(r.holdout[i].second);
        s += r.method + "," + std::to_string(r.budget) + "," + std::to_string(r.seed) + "," + r.benchmark + "," + bench::i64_to_string(r.tuning_size) + "," +
             r.selected + "," + std::to_string(r.evaluations) + "," + std::to_string(r.failed_evaluations) + "," + f6(r.tuning_speedup) + "," +
             f6(r.oracle_speedup) + "," + f6(r.regret) + "," + f6(r.holdout_mean) + "," + ho + "," + bench::join(r.sequence, ";") + std::string(1, 10);
    }
    return s;
}

std::string study_summary_csv(const std::vector<StudyRow>& rows) {
    struct Acc {
        std::vector<double> tuning, regret, holdout;
        int n = 0;
    };
    std::map<std::tuple<std::string, int, std::string>, Acc> groups;
    for (const StudyRow& r : rows) {
        for (const std::string& b : {r.benchmark, std::string("ALL")}) {
            Acc& a = groups[std::make_tuple(r.method, r.budget, b)];
            a.tuning.push_back(r.tuning_speedup);
            a.regret.push_back(r.regret);
            if (!r.holdout.empty()) a.holdout.push_back(r.holdout_mean);
            ++a.n;
        }
    }
    auto stdev = [](const std::vector<double>& v, double mean) {
        if (v.size() < 2) return 0.0;
        double s = 0;
        for (double x : v) s += (x - mean) * (x - mean);
        return std::sqrt(s / static_cast<double>(v.size() - 1));
    };
    std::string s = "method,budget,benchmark,runs,tuning_mean,tuning_median,tuning_min,tuning_max,tuning_stdev,regret_mean,regret_max,holdout_mean,holdout_median,holdout_min,holdout_max,holdout_stdev" + std::string(1, 10);
    for (const auto& kv : groups) {
        const Acc& a = kv.second;
        bench::Summary t = bench::summarize(a.tuning), g = bench::summarize(a.regret), h = bench::summarize(a.holdout);
        s += std::get<0>(kv.first) + "," + std::to_string(std::get<1>(kv.first)) + "," + std::get<2>(kv.first) + "," + std::to_string(a.n) + "," + f6(t.mean) + "," +
             f6(t.median) + "," + f6(t.min) + "," + f6(t.max) + "," + f6(stdev(a.tuning, t.mean)) + "," + f6(g.mean) + "," + f6(g.max) + "," +
             (h.count ? f6(h.mean) : "") + "," + (h.count ? f6(h.median) : "") + "," + (h.count ? f6(h.min) : "") + "," + (h.count ? f6(h.max) : "") + "," +
             (h.count ? f6(stdev(a.holdout, h.mean)) : "") + std::string(1, 10);
    }
    return s;
}

bool write_study(const std::string& dir, const StudyResult& r, std::string& error) {
    if (!bench::make_dirs(dir)) {
        error = "cannot create '" + dir + "'";
        return false;
    }
    bool ok = bench::write_file(dir + "/manifest.json", r.manifest.dump(2) + std::string(1, 10)) &&
              bench::write_file(dir + "/study_runs.csv", study_runs_csv(r.rows)) &&
              bench::write_file(dir + "/study_summary.csv", study_summary_csv(r.rows));
    if (!ok) error = "failed to write study files into '" + dir + "'";
    return ok;
}

// ---------------------------------------------------------------------------------------------------------------------
// CLI
// ---------------------------------------------------------------------------------------------------------------------

const char* study_usage() {
    return "  table <benchmark> [options]              Measure EVERY configuration (no selection) on sizes: ground truth for studies\n"
           "      --sizes=n1,n2 | --role=tuning|holdout|all (default tuning)  --reps=N (15)  --warmup=N (2)\n"
           "      --affinity=HEXMASK  --high-priority  --compiler=g++  --kernel-dir=DIR  --id=NAME  --out=DIR\n"
           "  study --tables=dir1,dir2,... --out=DIR [options]   Replay search strategies against saved tables\n"
           "      --methods=fixed,random,adaptive,adaptive-no-features,adaptive-no-history,adaptive-no-feedback,baseline-O2,baseline-O3\n"
           "      --budgets=1,2,4,8  --seeds=N (30)  --pilot-reps=N (5)\n"
           "  history --tables=dir1,dir2 --out=FILE    Build a history file from tuning-size tables (holdout tables are ignored)\n";
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

bool parse_ll(const std::string& s, long long& v) {
    if (s.empty()) return false;
    char* end = nullptr;
    v = std::strtoll(s.c_str(), &end, 10);
    return *end == '\0';
}

}  // namespace

int run_table_command(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    TableConfig cfg;
    std::string out_dir;
    auto usage = [&](const std::string& msg) {
        err << "nova table: " << msg << "\nTry 'nova --help' for usage.\n";
        return 2;
    };
    for (const std::string& a : args) {
        if (a.rfind("--", 0) != 0) {
            if (!cfg.benchmark.empty()) return usage("'table' takes exactly one benchmark name");
            cfg.benchmark = a;
            continue;
        }
        size_t eq = a.find('=');
        std::string key = a.substr(0, eq), val = eq == std::string::npos ? "" : a.substr(eq + 1);
        long long n = 0;
        if (key == "--high-priority" && eq == std::string::npos) {
            cfg.high_priority = true;
        } else if (eq == std::string::npos) {
            return usage("unknown option '" + a + "'");
        } else if (key == "--sizes") {
            for (const std::string& tok : split(val, ',')) {
                if (!parse_ll(tok, n) || n <= 0) return usage("bad size '" + tok + "'");
                cfg.sizes.push_back(n);
            }
        } else if (key == "--role") {
            cfg.role = val;
        } else if (key == "--reps") {
            if (!parse_ll(val, n) || n < 1 || n > 10000) return usage("--reps must be 1..10000");
            cfg.repetitions = static_cast<int>(n);
        } else if (key == "--warmup") {
            if (!parse_ll(val, n) || n < 0 || n > 1000) return usage("--warmup must be 0..1000");
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
    if (cfg.benchmark.empty()) return usage("'table' requires a benchmark name");
    if (cfg.experiment_id.empty()) {
        std::string compact;
        for (char c : bench::utc_timestamp())
            if (c >= '0' && c <= '9') compact.push_back(c);
        cfg.experiment_id = "table-" + compact;
    }
    cfg.out_dir = out_dir.empty() ? "benchmarks/results/" + cfg.experiment_id : out_dir;
    bench::ExperimentResult result;
    std::string error;
    out << "table " << cfg.benchmark << " -> " << cfg.out_dir << "\n";
    if (!run_table(cfg, default_space(), Whitelist::production(), result, error, &out)) {
        err << "nova table: " << error << "\n";
        return 1;
    }
    bench::BenchConfig bc;
    bc.out_dir = cfg.out_dir;
    bc.experiment_id = cfg.experiment_id;
    if (!bench::write_experiment(bc, result, error)) {
        err << "nova table: " << error << "\n";
        return 1;
    }
    int bad = 0, failed = 0;
    for (const RunRecord& r : result.records) {
        if (r.correctness_status != "PASS") ++failed;
        for (const std::string& p : bench::validate_record(r)) {
            err << "nova table: record " << r.candidate_id << ": " << p << "\n";
            ++bad;
        }
    }
    out << "wrote " << result.records.size() << " records (" << failed << " not passing) to " << cfg.out_dir << "\n";
    return bad == 0 ? 0 : 1;
}

int run_history_command(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    std::vector<std::string> dirs;
    std::string out_path;
    for (const std::string& a : args) {
        size_t eq = a.find('=');
        std::string key = a.substr(0, eq), val = eq == std::string::npos ? "" : a.substr(eq + 1);
        if (key == "--tables") dirs = split(val, ',');
        else if (key == "--out") out_path = val;
        else {
            err << "nova history: unknown argument '" << a << "'\nTry 'nova --help' for usage.\n";
            return 2;
        }
    }
    if (dirs.empty() || out_path.empty()) {
        err << "nova history: requires --tables=DIRS and --out=FILE\n";
        return 2;
    }
    SearchSpace space = default_space();
    std::vector<HistoryEntry> entries;
    for (const std::string& d : dirs) {
        std::vector<WorkloadTable> tables;
        std::string error;
        if (!load_tables(d, space, 2.0, tables, error)) {
            err << "nova history: " << error << "\n";
            return 1;
        }
        for (const WorkloadTable& t : tables) {
            if (t.role != "tuning") continue;  // holdout tables never enter a history
            const TableEntry& o2 = t.entries.at("O2");
            if (!o2.eligible) continue;
            for (const auto& kv : t.entries) {
                if (kv.first == "O2" || kv.first == "O3") continue;
                HistoryEntry e;
                e.benchmark = t.benchmark;
                e.size = t.size;
                e.size_role = t.role;
                e.candidate = kv.first;
                e.source = t.source;
                if (!kv.second.eligible) e.failed = true;
                else e.log_gain = std::log(o2.median_ms / kv.second.median_ms);
                entries.push_back(e);
            }
        }
    }
    std::string error;
    if (!bench::write_file(out_path, history_to_json(entries).dump(2) + std::string(1, 10))) {
        err << "nova history: cannot write '" << out_path << "'\n";
        return 1;
    }
    out << "wrote " << entries.size() << " tuning-size history entries to " << out_path << "\n";
    return 0;
}

int run_study_command(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    StudyConfig cfg;
    std::vector<std::string> dirs;
    std::string out_dir;
    auto usage = [&](const std::string& msg) {
        err << "nova study: " << msg << "\nTry 'nova --help' for usage.\n";
        return 2;
    };
    for (const std::string& a : args) {
        size_t eq = a.find('=');
        std::string key = a.substr(0, eq), val = eq == std::string::npos ? "" : a.substr(eq + 1);
        long long n = 0;
        if (a.rfind("--", 0) != 0 || eq == std::string::npos) return usage("unknown argument '" + a + "'");
        if (key == "--tables") {
            dirs = split(val, ',');
        } else if (key == "--out") {
            out_dir = val;
        } else if (key == "--methods") {
            cfg.methods = split(val, ',');
        } else if (key == "--budgets") {
            cfg.budgets.clear();
            for (const std::string& tok : split(val, ',')) {
                if (!parse_ll(tok, n) || n < 0 || n > 100000) return usage("bad budget '" + tok + "'");
                cfg.budgets.push_back(static_cast<int>(n));
            }
        } else if (key == "--seeds") {
            if (!parse_ll(val, n) || n < 1 || n > 100000) return usage("--seeds must be 1..100000");
            cfg.seeds = static_cast<int>(n);
        } else if (key == "--pilot-reps") {
            if (!parse_ll(val, n) || n < 1 || n > 1000) return usage("--pilot-reps must be 1..1000");
            cfg.pilot_reps = static_cast<int>(n);
        } else {
            return usage("unknown option '" + key + "'");
        }
    }
    if (dirs.empty() || out_dir.empty()) return usage("'study' requires --tables=DIRS and --out=DIR");
    SearchSpace space = default_space();
    std::vector<WorkloadTable> tables;
    std::string error;
    for (const std::string& d : dirs) {
        std::vector<WorkloadTable> t;
        if (!load_tables(d, space, 2.0, t, error)) {
            err << "nova study: " << error << "\n";
            return 1;
        }
        tables.insert(tables.end(), t.begin(), t.end());
    }
    StudyResult result;
    if (!run_study(tables, space, cfg, result, error)) {
        err << "nova study: " << error << "\n";
        return 1;
    }
    if (!write_study(out_dir, result, error)) {
        err << "nova study: " << error << "\n";
        return 1;
    }
    out << "study: " << tables.size() << " tables, " << result.rows.size() << " runs -> " << out_dir << "\n";
    return 0;
}

}  // namespace tune
}  // namespace nova

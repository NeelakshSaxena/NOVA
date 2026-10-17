#include "nova/tune/tuner.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <sstream>

#include "nova/bench/env.hpp"
#include "nova/bench/registry.hpp"
#include "nova/bench/stats.hpp"
#include "nova/tune/features.hpp"
#include "nova/bench/util.hpp"

namespace nova {
namespace tune {

using bench::Json;
using bench::RunRecord;

namespace {

using Clock = std::chrono::steady_clock;

double seconds_since(Clock::time_point t0) { return std::chrono::duration<double>(Clock::now() - t0).count(); }

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

std::string fmt(const char* format, double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, format, v);
    return buf;
}

}  // namespace

bool run_tune(const TuneConfig& cfg, const SearchSpace& space, const Whitelist& whitelist, TuneResult& result,
              std::string& error, std::ostream* log) {
    Clock::time_point t0 = Clock::now();
    // ---- configuration checks: nothing below starts a process until all of them pass --------------------------
    if (cfg.experiment_id.empty() || cfg.out_dir.empty()) {
        error = "experiment id and output directory are required";
        return false;
    }
    const bench::BenchmarkDef* def = bench::find_benchmark(cfg.benchmark);
    if (!def) {
        error = "unknown benchmark '" + cfg.benchmark + "'";
        return false;
    }
    if (cfg.budget < 0 || cfg.budget > 100000) {
        error = "budget must be between 0 and 100000";
        return false;
    }
    if (cfg.repetitions < 1 || cfg.warmup_runs < 0) {
        error = "repetitions must be >= 1 and warm-up runs >= 0";
        return false;
    }
    std::int64_t size = cfg.size != 0 ? cfg.size : def->tuning_sizes.front();
    if (size <= 0 || (cfg.benchmark == "stencil" && size < 3)) {
        error = "invalid input size " + bench::i64_to_string(size);
        return false;
    }
    if (std::find(def->holdout_sizes.begin(), def->holdout_sizes.end(), size) != def->holdout_sizes.end()) {
        error = "size " + bench::i64_to_string(size) + " is a reserved holdout size of '" + cfg.benchmark +
                "'; tuning may not use holdout sizes";
        return false;
    }
    if (space.baselines.empty()) {
        error = "the search space has no baselines (baselines are always included)";
        return false;
    }
    for (const Candidate& c : space.baselines) {
        std::string why;
        if (!validate_flags(c.flags, whitelist, why)) {
            error = "invalid baseline '" + c.id + "': " + why;
            return false;
        }
    }
    for (const Candidate& c : space.candidates) {
        std::string why;
        if (!validate_flags(c.flags, whitelist, why)) {
            error = "invalid candidate '" + c.id + "': " + why;
            return false;
        }
    }
    std::vector<Candidate> sequence = select_sequence(space, cfg.strategy, cfg.search_seed, cfg.budget);  // empty for adaptive
    if (static_cast<int>(sequence.size()) > cfg.budget) {  // cannot happen; guards the budget invariant
        error = "internal error: candidate sequence exceeds the budget";
        return false;
    }

    // ---- environment + source ----------------------------------------------------------------------------------
    bench::Environment env = bench::collect_environment(cfg.compiler);
    if (env.compiler_version.empty()) {
        error = "cannot run compiler '" + cfg.compiler + "' (is it on PATH?)";
        return false;
    }
    std::string hash, herr;
    if (!bench::kernel_source_hash(cfg.kernel_dir, cfg.benchmark, hash, herr)) {
        error = herr;
        return false;
    }
    if (!bench::make_dirs(cfg.out_dir + "/bin")) {
        error = "cannot create output directory '" + cfg.out_dir + "'";
        return false;
    }

    // ---- build baselines, then the budgeted candidates -------------------------------------------------------------
    std::vector<Candidate> all;                // baselines first, then candidates in evaluation order
    std::vector<bench::TimingSlot> slots;
    Json commands = Json::array();
    result = TuneResult();
    bench::ReferenceCache refs;

    auto add_candidate = [&](const Candidate& c) -> size_t {
        size_t i = all.size();
        all.push_back(c);
        slots.emplace_back();
        std::vector<std::string> flags = cfg.fixed_flags;
        flags.insert(flags.end(), c.flags.begin(), c.flags.end());
        std::string exe = cfg.out_dir + "/bin/" + cfg.benchmark + "_" + std::to_string(i) + exe_suffix();
        if (log) *log << "build " << c.id << " ..." << std::flush;
        bench::BuildOutcome b = bench::build_kernel(cfg.compiler, flags, cfg.kernel_dir + "/" + cfg.benchmark + ".cpp",
                                                    exe, cfg.build_timeout_ms);
        ++result.builds_attempted;
        if (!c.baseline) ++result.evaluations;
        if (log) *log << (b.ok ? " ok" : " FAILED: " + b.reason) << "\n";
        Json cj = Json::object();
        cj.set("phase", Json::string("build"));
        cj.set("candidate_id", Json::string(c.id));
        cj.set("argv", to_json_array(b.command));
        cj.set("exit_ok", Json::boolean(b.ok));
        commands.push(cj);
        result.build_logs[c.id] = b.log;

        bench::TimingSlot& s = slots[i];
        RunRecord& r = s.record;
        r.experiment_id = cfg.experiment_id;
        r.benchmark = cfg.benchmark;
        r.source_hash = hash;
        r.compiler_version = env.compiler_version;
        r.flags = bench::join(flags);
        r.opt_level = c.flags.empty() ? "" : c.flags[0].substr(1);
        r.fp_semantics = "strict";
        r.input_size = size;
        r.size_role = std::find(def->tuning_sizes.begin(), def->tuning_sizes.end(), size) != def->tuning_sizes.end() ? "tuning" : "adhoc";
        r.seed = cfg.seed;
        r.candidate_id = c.id;
        r.warmup_runs = cfg.warmup_runs;
        r.repetitions = cfg.repetitions;
        r.binary_size_bytes = b.binary_size;
        r.build_time_s = b.seconds;
        r.candidate_budget = cfg.budget;
        r.search_strategy = c.baseline ? "baseline" : to_string(cfg.strategy);
        s.exe = exe;
        if (b.ok) {
            r.build_status = "OK";
        } else {
            r.build_status = "FAILED";
            r.correctness_status = "NOT_RUN";
            r.failure_reason = b.reason;
        }
        if (!c.baseline) result.sequence.push_back(c.id);
        return i;
    };

    for (const Candidate& c : space.baselines) add_candidate(c);

    Json pilot_log = Json::array();
    Json features_json = Json::null();
    std::string ablation_label = "n/a";
    if (cfg.strategy != Strategy::Adaptive) {
        for (const Candidate& c : sequence) add_candidate(c);
    } else {
        // ---- adaptive search: choose each next candidate from workload descriptors, history and this run's outcomes
        WorkloadFeatures wf = extract_features(cfg.benchmark, size);
        std::vector<HistoryEntry> loaded;
        if (!cfg.history_path.empty() && !load_history_file(cfg.history_path, loaded, error)) return false;
        result.history_used = filter_history(loaded, cfg.benchmark, size, cfg.history_exclude_same);
        AdaptivePolicy policy(space, wf, result.history_used, cfg.ablation, cfg.search_seed);
        ablation_label = ablation_name(cfg.ablation);
        Json fj = Json::object();
        fj.set("known", Json::boolean(wf.known));
        fj.set("working_set_bytes", Json::number(wf.working_set_bytes));
        fj.set("flops_per_byte", Json::number(wf.flops_per_byte));
        fj.set("loop_nest_depth", Json::integer(wf.loop_nest_depth));
        fj.set("innermost_reduction", Json::boolean(wf.innermost_reduction));
        fj.set("unit_stride_streaming", Json::boolean(wf.unit_stride_streaming));
        fj.set("size_log2", Json::number(wf.size_log2));
        features_json = fj;
        // The anchor for the pilot comparison is the plain -O2 baseline (else the first baseline that built).
        size_t anchor = slots.size();
        for (size_t i = 0; i < space.baselines.size() && anchor == slots.size(); ++i)
            if (slots[i].record.build_status == "OK" && space.baselines[i].id == "O2") anchor = i;
        for (size_t i = 0; i < space.baselines.size() && anchor == slots.size(); ++i)
            if (slots[i].record.build_status == "OK") anchor = i;
        std::string rerr;
        const bench::KernelOutput* ref = refs.get(cfg.benchmark, size, cfg.seed, rerr);
        if (!ref) {
            error = rerr;
            return false;
        }
        for (int step = 0; step < cfg.budget; ++step) {
            int idx = policy.next();
            if (idx < 0) break;  // the whole space has been evaluated
            double prior = policy.prior(idx), hist = policy.history(idx), fb = policy.feedback(idx);
            size_t si = add_candidate(space.candidates[static_cast<size_t>(idx)]);
            Json pj = Json::object();
            pj.set("step", Json::integer(step + 1));
            pj.set("candidate", Json::string(space.candidates[static_cast<size_t>(idx)].id));
            pj.set("prior", Json::number(prior));
            pj.set("history", Json::number(hist));
            pj.set("feedback", Json::number(fb));
            bool ok = slots[si].record.build_status == "OK";
            double log_gain = 0;
            std::vector<double> cand_samples, anchor_samples;
            std::string why;
            if (ok && anchor < slots.size()) {
                // Pilot: interleaved anchor/candidate rounds, every run checked against the oracle.
                bench::Rng rng(cfg.order_seed ^ (static_cast<std::uint64_t>(step) * 0x9E3779B1ULL) ^ 0x51ULL);
                for (int rep = 0; rep < cfg.pilot_reps && ok; ++rep) {
                    std::vector<size_t> order = {anchor, si};
                    bench::shuffle(order, rng);
                    for (size_t which : order) {
                        bench::KernelRun k = bench::run_kernel(slots[which].exe, size, cfg.seed, cfg.run_timeout_ms, ref,
                                                               cfg.affinity_mask, cfg.high_priority);
                        if (!k.ok || !k.mismatches.empty()) {
                            if (which == si) {
                                ok = false;
                                why = !k.ok ? k.reason : "output differs from the reference";
                            }
                            continue;
                        }
                        (which == si ? cand_samples : anchor_samples).push_back(k.output.time_ms);
                    }
                }
                if (ok && !cand_samples.empty() && !anchor_samples.empty()) {
                    double cm = bench::summarize(cand_samples).median, am = bench::summarize(anchor_samples).median;
                    if (cm > 0 && am > 0) log_gain = std::log(am / cm);
                    else ok = false;
                }
            }
            policy.observe(idx, ok, log_gain);
            pj.set("ok", Json::boolean(ok));
            pj.set("log_gain", Json::number(ok ? log_gain : AdaptivePolicy::kFailureLogGain));
            pj.set("failure", Json::string(why));
            Json cs = Json::array(), as = Json::array();
            for (double x : cand_samples) cs.push(Json::number(x));
            for (double x : anchor_samples) as.push(Json::number(x));
            pj.set("candidate_samples_ms", cs);
            pj.set("anchor_samples_ms", as);
            pilot_log.push(pj);
        }
    }

    // ---- correctness gate + timing -------------------------------------------------------------------------------
    bench::TimingParams tp;
    tp.experiment_id = cfg.experiment_id;
    tp.repetitions = cfg.repetitions;
    tp.warmup_runs = cfg.warmup_runs;
    tp.seed = cfg.seed;
    tp.order_seed = cfg.order_seed;
    tp.run_timeout_ms = cfg.run_timeout_ms;
    tp.affinity_mask = cfg.affinity_mask;
    tp.high_priority = cfg.high_priority;
    if (!bench::validate_and_time(slots, cfg.benchmark, size, tp, 0, refs, result.raw, &commands, log, error)) return false;

    // ---- eligibility policy, ranking, winner ----------------------------------------------------------------------
    std::int64_t reference_size = 0;
    for (size_t i = 0; i < space.baselines.size(); ++i)
        if (slots[i].record.build_status == "OK" && space.baselines[i].id == "O2") reference_size = slots[i].record.binary_size_bytes;
    if (reference_size == 0)
        for (size_t i = 0; i < space.baselines.size(); ++i)
            if (slots[i].record.build_status == "OK" && (reference_size == 0 || slots[i].record.binary_size_bytes < reference_size))
                reference_size = slots[i].record.binary_size_bytes;
    std::vector<size_t> eligible;
    for (size_t i = 0; i < slots.size(); ++i) {
        RunRecord& r = slots[i].record;
        if (r.lifecycle != "BENCHMARKED") continue;
        bool complete = static_cast<int>(r.timing_samples_ms.size()) == cfg.repetitions;
        if (!complete) {
            r.lifecycle = "REJECTED_BY_POLICY";
            r.failure_reason = "incomplete timing data (" + std::to_string(r.timing_samples_ms.size()) + " of " +
                               std::to_string(cfg.repetitions) + " samples)";
        } else if (cfg.max_binary_growth > 0 && reference_size > 0 &&
                   static_cast<double>(r.binary_size_bytes) > cfg.max_binary_growth * static_cast<double>(reference_size)) {
            r.lifecycle = "REJECTED_BY_POLICY";
            r.failure_reason = "binary size " + bench::i64_to_string(r.binary_size_bytes) + " bytes exceeds the policy limit of " +
                               fmt("%.2f", cfg.max_binary_growth) + " x the baseline -O2 binary (" +
                               bench::i64_to_string(reference_size) + " bytes)";
        } else {
            r.lifecycle = "ELIGIBLE";
            eligible.push_back(i);
        }
    }
    std::stable_sort(eligible.begin(), eligible.end(),
                     [&](size_t a, size_t b) { return slots[a].record.median_ms < slots[b].record.median_ms; });
    for (size_t k = 0; k < eligible.size(); ++k) slots[eligible[k]].record.rank = static_cast<int>(k) + 1;
    if (!eligible.empty()) {
        result.has_winner = true;
        slots[eligible[0]].record.selected = true;
        result.winner_id = slots[eligible[0]].record.candidate_id;
    }
    // History entries for this run's searched candidates (tuning-size runs only; gains are vs the plain -O2 baseline).
    {
        double o2_median = 0;
        for (const bench::TimingSlot& sl : slots)
            if (sl.record.candidate_id == "O2" && sl.record.correctness_status == "PASS") o2_median = sl.record.median_ms;
        for (size_t i = 0; i < slots.size(); ++i) {
            const RunRecord& r = slots[i].record;
            if (all[i].baseline || r.size_role != "tuning" || o2_median <= 0) continue;
            HistoryEntry h;
            h.benchmark = cfg.benchmark;
            h.size = size;
            h.size_role = r.size_role;
            h.candidate = r.candidate_id;
            h.source = cfg.experiment_id;
            if (r.build_status != "OK" || r.correctness_status != "PASS") {
                h.failed = true;
            } else if (r.median_ms > 0) {
                h.log_gain = std::log(o2_median / r.median_ms);
            } else {
                continue;
            }
            result.new_history.push_back(h);
        }
    }
    result.tuning_wall_time_s = seconds_since(t0);
    for (bench::TimingSlot& s : slots) {
        s.record.tuning_wall_time_s = result.tuning_wall_time_s;
        result.records.push_back(s.record);
    }

    // ---- manifest -----------------------------------------------------------------------------------------------------
    Json prot = Json::object();
    prot.set("repetitions", Json::integer(cfg.repetitions));
    prot.set("warmup_runs", Json::integer(cfg.warmup_runs));
    prot.set("validation_run_discarded", Json::boolean(true));
    prot.set("input_seed", Json::string(bench::u64_to_string(cfg.seed)));
    prot.set("order_seed", Json::string(bench::u64_to_string(cfg.order_seed)));
    prot.set("order", Json::string("interleaved rounds, seeded Fisher-Yates shuffle of candidates per round"));
    prot.set("relative_tolerance", Json::number(bench::kRelativeTolerance));
    prot.set("fixed_flags", to_json_array(cfg.fixed_flags));
    prot.set("build_timeout_ms", Json::integer(cfg.build_timeout_ms));
    prot.set("run_timeout_ms", Json::integer(cfg.run_timeout_ms));
    prot.set("cpu_affinity_mask", Json::string(bench::u64_to_string(cfg.affinity_mask)));
    prot.set("high_priority", Json::boolean(cfg.high_priority));

    Json search = Json::object();
    search.set("strategy", Json::string(to_string(cfg.strategy)));
    search.set("search_seed", Json::string(bench::u64_to_string(cfg.search_seed)));
    search.set("budget", Json::integer(cfg.budget));
    search.set("space_size", Json::integer(static_cast<std::int64_t>(space.candidates.size())));
    search.set("evaluations", Json::integer(result.evaluations));
    search.set("builds_attempted", Json::integer(result.builds_attempted));
    search.set("accounting", Json::string("budget counts search candidates built+validated; the baselines are always "
                                          "evaluated in addition and are not counted; no automatic retries"));
    search.set("sequence", to_json_array(result.sequence));
    Json bl = Json::array();
    for (const Candidate& c : space.baselines) bl.push(Json::string(c.id));
    search.set("baselines", bl);
    search.set("whitelist", to_json_array(std::vector<std::string>(whitelist.flags().begin(), whitelist.flags().end())));
    search.set("max_binary_growth", Json::number(cfg.max_binary_growth));
    search.set("tuning_size", Json::integer(size));
    if (cfg.strategy == Strategy::Adaptive) {
        Json ad = Json::object();
        ad.set("ablation", Json::string(ablation_label));
        ad.set("pilot_reps", Json::integer(cfg.pilot_reps));
        ad.set("history_path", Json::string(cfg.history_path));
        ad.set("history_entries_used", Json::integer(static_cast<std::int64_t>(result.history_used.size())));
        ad.set("history_exclude_same_workload", Json::boolean(cfg.history_exclude_same));
        ad.set("workload_features", features_json);
        ad.set("pilot", pilot_log);
        ad.set("policy", Json::string("score = prior(features) + history + feedback, in log-gain units; see docs/adaptive.md"));
        search.set("adaptive", ad);
    }
    Json cflags = Json::object();
    for (const Candidate& c : all) cflags.set(c.id, to_json_array(c.flags));
    search.set("candidate_flags", cflags);
    search.set("holdout_policy", Json::string("holdout sizes are refused; no holdout measurement influenced this selection"));

    Json m = Json::object();
    m.set("experiment_id", Json::string(cfg.experiment_id));
    m.set("created_utc", Json::string(bench::utc_timestamp()));
    m.set("kind", Json::string("tune"));
    m.set("benchmark", Json::string(cfg.benchmark));
    m.set("source_hash", Json::string(hash));
    m.set("kernel_dir", Json::string(cfg.kernel_dir));
    m.set("search", search);
    m.set("protocol", prot);
    m.set("environment", bench::environment_to_json(env));
    m.set("missing_environment_fields", to_json_array(bench::missing_environment_fields(env)));
    m.set("commands", commands);
    m.set("has_winner", Json::boolean(result.has_winner));
    m.set("winner", Json::string(result.winner_id));
    m.set("tuning_wall_time_s", Json::number(result.tuning_wall_time_s));
    result.manifest = m;
    return true;
}

bool record_tune_history(const TuneConfig& cfg, const TuneResult& result, std::string& error) {
    if (cfg.history_path.empty()) {
        error = "no history path configured";
        return false;
    }
    return append_history_file(cfg.history_path, result.new_history, error);
}

bool write_tune(const TuneConfig& cfg, const TuneResult& r, std::string& error) {
    bench::BenchConfig bc;
    bc.out_dir = cfg.out_dir;
    bc.experiment_id = cfg.experiment_id;
    bench::ExperimentResult er;
    er.manifest = r.manifest;
    er.records = r.records;
    er.raw = r.raw;
    er.build_logs = r.build_logs;
    return bench::write_experiment(bc, er, error);
}

std::string explain_tune(const Json& manifest, const std::vector<RunRecord>& records) {
    std::ostringstream os;
    const Json* search = manifest.find("search");
    auto str = [](const Json* j, const char* key) {
        const Json* v = j ? j->find(key) : nullptr;
        if (!v) return std::string("?");
        if (v->type() == Json::Type::String) return v->as_string();
        if (v->type() == Json::Type::Int) return bench::i64_to_string(v->as_int());
        return std::string("?");
    };
    os << "SmartPass tuning result: " << str(&manifest, "benchmark") << ", input size " << str(search, "tuning_size")
       << ", experiment " << str(&manifest, "experiment_id") << "\n";
    os << "strategy " << str(search, "strategy") << " (search seed " << str(search, "search_seed") << "), budget "
       << str(search, "budget") << " search candidates; evaluated " << str(search, "evaluations") << " + baselines ("
       << str(search, "builds_attempted") << " builds total)\n";
    const Json* wall = manifest.find("tuning_wall_time_s");
    if (wall && wall->is_number()) os << "tuning wall time " << fmt("%.2f", wall->as_double()) << " s\n";
    os << "\n";
    os << "  rank  candidate                       state                 median_ms   IQR_ms  size_bytes  note\n";
    for (const RunRecord& r : records) {
        char line[400];
        std::string rank = r.rank > 0 ? std::to_string(r.rank) : "-";
        std::string note = r.failure_reason;
        if (r.selected) note = "WINNER" + (note.empty() ? std::string() : "; " + note);
        if (r.lifecycle == "ELIGIBLE" || r.lifecycle == "REJECTED_BY_POLICY" || r.lifecycle == "BENCHMARKED") {
            std::snprintf(line, sizeof line, "  %4s  %-30s  %-20s %10.4f %8.4f  %10s  %s\n", rank.c_str(), r.candidate_id.c_str(),
                          r.lifecycle.c_str(), r.median_ms, r.iqr_ms, bench::i64_to_string(r.binary_size_bytes).c_str(), note.c_str());
        } else {
            std::snprintf(line, sizeof line, "  %4s  %-30s  %-20s %10s %8s  %10s  %s\n", rank.c_str(), r.candidate_id.c_str(),
                          r.lifecycle.c_str(), "-", "-", r.binary_size_bytes > 0 ? bench::i64_to_string(r.binary_size_bytes).c_str() : "-",
                          note.c_str());
        }
        os << line;
    }
    const RunRecord* winner = nullptr;
    const RunRecord* best_base = nullptr;
    for (const RunRecord& r : records) {
        if (r.selected) winner = &r;
        if (r.search_strategy == "baseline" && r.lifecycle == "ELIGIBLE" && (!best_base || r.median_ms < best_base->median_ms)) best_base = &r;
    }
    os << "\n";
    if (!winner) {
        os << "NO ELIGIBLE CANDIDATE: every candidate failed to build, failed correctness, or was rejected by policy (see the table).\n";
        return os.str();
    }
    os << "winner: " << winner->candidate_id << " [" << winner->flags << "], median " << fmt("%.4f", winner->median_ms) << " ms over "
       << winner->timing_samples_ms.size() << " samples\n";
    if (best_base && best_base != winner) {
        double ratio = best_base->median_ms / winner->median_ms;
        bool overlap = !(winner->q3_ms < best_base->q1_ms || best_base->q3_ms < winner->q1_ms);
        os << "best baseline: " << best_base->candidate_id << ", median " << fmt("%.4f", best_base->median_ms) << " ms; ratio of medians "
           << fmt("%.3f", ratio) << "; interquartile ranges " << (overlap ? "overlap -> the difference is inconclusive" : "are disjoint")
           << " (selection on this tuning size only; not evidence for other sizes)\n";
    } else if (best_base == winner) {
        os << "the winner is a baseline: no searched candidate was faster than the best baseline in this measurement\n";
    }
    return os.str();
}

}  // namespace tune
}  // namespace nova

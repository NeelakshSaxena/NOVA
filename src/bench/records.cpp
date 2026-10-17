#include "nova/bench/records.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "nova/bench/stats.hpp"
#include "nova/bench/util.hpp"

namespace nova {
namespace bench {

void compute_statistics(RunRecord& r) {
    Summary s = summarize(r.timing_samples_ms);
    r.median_ms = s.median;
    r.q1_ms = s.q1;
    r.q3_ms = s.q3;
    r.iqr_ms = s.iqr;
    r.mad_ms = s.mad;
    r.min_ms = s.min;
    r.max_ms = s.max;
}

namespace {

bool is_hex64(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

bool one_of(const std::string& v, std::initializer_list<const char*> allowed) {
    for (const char* a : allowed)
        if (v == a) return true;
    return false;
}

bool close_enough(double a, double b) { return std::fabs(a - b) <= 1e-9 * std::max(1.0, std::fabs(b)); }

}  // namespace

std::string derive_lifecycle(const RunRecord& r) {
    if (r.build_status == "FAILED") return "BUILD_FAILED";
    if (r.correctness_status == "FAIL") return "CORRECTNESS_FAILED";
    if (r.correctness_status == "PASS") return "BENCHMARKED";
    return "GENERATED";
}

std::vector<std::string> validate_record(const RunRecord& r) {
    std::vector<std::string> p;
    auto need = [&](bool ok, const char* msg) {
        if (!ok) p.push_back(msg);
    };
    need(!r.experiment_id.empty(), "experiment_id is empty");
    need(!r.benchmark.empty(), "benchmark is empty");
    need(is_hex64(r.source_hash), "source_hash is not a 64-digit hex SHA-256");
    need(!r.compiler_version.empty(), "compiler_version is empty");
    need(!r.flags.empty(), "flags is empty");
    need(!r.opt_level.empty(), "opt_level is empty");
    need(one_of(r.fp_semantics, {"strict", "fast-math"}), "fp_semantics must be 'strict' or 'fast-math'");
    need(r.input_size > 0, "input_size must be > 0");
    need(one_of(r.size_role, {"tuning", "holdout", "adhoc"}), "size_role must be tuning/holdout/adhoc");
    need(!r.candidate_id.empty(), "candidate_id is empty");
    need(one_of(r.build_status, {"OK", "FAILED"}), "build_status must be OK or FAILED");
    need(one_of(r.correctness_status, {"PASS", "FAIL", "NOT_RUN"}), "correctness_status must be PASS/FAIL/NOT_RUN");
    need(!r.search_strategy.empty(), "search_strategy is empty");
    need(r.repetitions > 0, "repetitions must be > 0");
    need(r.warmup_runs >= 0, "warmup_runs must be >= 0");
    need(r.binary_size_bytes >= 0, "binary_size_bytes must be >= 0");
    if (r.build_status == "OK") need(r.binary_size_bytes > 0, "built binary has no size recorded");
    if (r.build_status == "FAILED") {
        need(!r.failure_reason.empty(), "build FAILED but failure_reason is empty");
        need(r.correctness_status == "NOT_RUN", "build FAILED but correctness was run");
    }
    if (r.correctness_status == "FAIL") need(!r.failure_reason.empty(), "correctness FAIL but failure_reason is empty");
    need(one_of(r.lifecycle, {"GENERATED", "BUILD_FAILED", "CORRECTNESS_FAILED", "BENCHMARKED", "ELIGIBLE", "REJECTED_BY_POLICY"}),
         "lifecycle is not a valid candidate state");
    if (r.lifecycle == "BUILD_FAILED") need(r.build_status == "FAILED", "lifecycle BUILD_FAILED but the build did not fail");
    if (r.lifecycle == "CORRECTNESS_FAILED") need(r.correctness_status == "FAIL", "lifecycle CORRECTNESS_FAILED but correctness did not fail");
    if (r.lifecycle == "BENCHMARKED" || r.lifecycle == "ELIGIBLE" || r.lifecycle == "REJECTED_BY_POLICY")
        need(r.correctness_status == "PASS", "lifecycle says benchmarked/eligible but correctness did not PASS");
    if (r.rank != 0) need(r.lifecycle == "ELIGIBLE", "only ELIGIBLE candidates may be ranked");
    if (r.selected) need(r.lifecycle == "ELIGIBLE", "a selected candidate must be ELIGIBLE");
    need(r.rank >= 0, "rank must be >= 0");
    if (r.correctness_status == "PASS") {
        need(r.build_status == "OK", "correctness PASS without a successful build");
        need(static_cast<int>(r.timing_samples_ms.size()) == r.repetitions,
             "sample count does not equal the configured repetitions");
        for (double x : r.timing_samples_ms) {
            if (!(std::isfinite(x) && x >= 0)) {
                p.push_back("a timing sample is negative or not finite");
                break;
            }
        }
        if (!r.timing_samples_ms.empty()) {
            Summary s = summarize(r.timing_samples_ms);
            need(close_enough(r.median_ms, s.median), "median_ms does not match the samples");
            need(close_enough(r.iqr_ms, s.iqr), "iqr_ms does not match the samples");
            need(close_enough(r.mad_ms, s.mad), "mad_ms does not match the samples");
            need(close_enough(r.min_ms, s.min) && close_enough(r.max_ms, s.max), "min/max do not match the samples");
        }
    } else {
        need(r.timing_samples_ms.empty(), "timing samples present although correctness did not PASS");
    }
    return p;
}

Json record_to_json(const RunRecord& r) {
    Json j = Json::object();
    j.set("experiment_id", Json::string(r.experiment_id));
    j.set("benchmark", Json::string(r.benchmark));
    j.set("source_hash", Json::string(r.source_hash));
    j.set("compiler_version", Json::string(r.compiler_version));
    j.set("flags", Json::string(r.flags));
    j.set("opt_level", Json::string(r.opt_level));
    j.set("fp_semantics", Json::string(r.fp_semantics));
    j.set("input_size", Json::integer(r.input_size));
    j.set("size_role", Json::string(r.size_role));
    // Seeds are 64-bit unsigned; store as a decimal string so no precision is lost.
    j.set("seed", Json::string(u64_to_string(r.seed)));
    j.set("candidate_id", Json::string(r.candidate_id));
    j.set("build_status", Json::string(r.build_status));
    j.set("correctness_status", Json::string(r.correctness_status));
    j.set("failure_reason", Json::string(r.failure_reason));
    j.set("warmup_runs", Json::integer(r.warmup_runs));
    j.set("repetitions", Json::integer(r.repetitions));
    Json samples = Json::array();
    for (double x : r.timing_samples_ms) samples.push(Json::number(x));
    j.set("timing_samples_ms", samples);
    j.set("median_ms", Json::number(r.median_ms));
    j.set("q1_ms", Json::number(r.q1_ms));
    j.set("q3_ms", Json::number(r.q3_ms));
    j.set("iqr_ms", Json::number(r.iqr_ms));
    j.set("mad_ms", Json::number(r.mad_ms));
    j.set("min_ms", Json::number(r.min_ms));
    j.set("max_ms", Json::number(r.max_ms));
    j.set("binary_size_bytes", Json::integer(r.binary_size_bytes));
    j.set("build_time_s", Json::number(r.build_time_s));
    j.set("candidate_budget", Json::integer(r.candidate_budget));
    j.set("search_strategy", Json::string(r.search_strategy));
    j.set("tuning_wall_time_s", Json::number(r.tuning_wall_time_s));
    j.set("selected", Json::boolean(r.selected));
    j.set("lifecycle", Json::string(r.lifecycle));
    j.set("rank", Json::integer(r.rank));
    return j;
}

bool record_from_json(const Json& j, RunRecord& out, std::string& error) {
    if (j.type() != Json::Type::Object) {
        error = "record is not a JSON object";
        return false;
    }
    RunRecord r;
    auto str = [&](const char* key, std::string& v) {
        const Json* f = j.find(key);
        if (!f || f->type() != Json::Type::String) {
            error = std::string("record: missing or non-string field '") + key + "'";
            return false;
        }
        v = f->as_string();
        return true;
    };
    auto num = [&](const char* key, double& v) {
        const Json* f = j.find(key);
        if (!f || !f->is_number()) {
            error = std::string("record: missing or non-numeric field '") + key + "'";
            return false;
        }
        v = f->as_double();
        return true;
    };
    auto integer = [&](const char* key, std::int64_t& v) {
        const Json* f = j.find(key);
        if (!f || f->type() != Json::Type::Int) {
            error = std::string("record: missing or non-integer field '") + key + "'";
            return false;
        }
        v = f->as_int();
        return true;
    };
    std::string seed_text;
    std::int64_t warm = 0, reps = 0, budget = 0;
    if (!str("experiment_id", r.experiment_id) || !str("benchmark", r.benchmark) || !str("source_hash", r.source_hash) ||
        !str("compiler_version", r.compiler_version) || !str("flags", r.flags) || !str("opt_level", r.opt_level) ||
        !str("fp_semantics", r.fp_semantics) || !integer("input_size", r.input_size) || !str("size_role", r.size_role) ||
        !str("seed", seed_text) || !str("candidate_id", r.candidate_id) || !str("build_status", r.build_status) ||
        !str("correctness_status", r.correctness_status) || !str("failure_reason", r.failure_reason) ||
        !integer("warmup_runs", warm) || !integer("repetitions", reps) || !num("median_ms", r.median_ms) ||
        !num("q1_ms", r.q1_ms) || !num("q3_ms", r.q3_ms) || !num("iqr_ms", r.iqr_ms) || !num("mad_ms", r.mad_ms) ||
        !num("min_ms", r.min_ms) || !num("max_ms", r.max_ms) || !integer("binary_size_bytes", r.binary_size_bytes) ||
        !num("build_time_s", r.build_time_s) || !integer("candidate_budget", budget) ||
        !str("search_strategy", r.search_strategy) || !num("tuning_wall_time_s", r.tuning_wall_time_s))
        return false;
    const Json* sel = j.find("selected");
    if (!sel || sel->type() != Json::Type::Bool) {
        error = "record: missing or non-boolean field 'selected'";
        return false;
    }
    r.selected = sel->as_bool();
    const Json* life = j.find("lifecycle");
    if (life && life->type() == Json::Type::String) {
        r.lifecycle = life->as_string();
    } else if (!life) {
        r.lifecycle = derive_lifecycle(r);  // pre-V0.7 files
    } else {
        error = "record: non-string field 'lifecycle'";
        return false;
    }
    const Json* rk = j.find("rank");
    if (rk && rk->type() == Json::Type::Int) r.rank = static_cast<int>(rk->as_int());
    else if (rk) {
        error = "record: non-integer field 'rank'";
        return false;
    }
    const Json* samples = j.find("timing_samples_ms");
    if (!samples || samples->type() != Json::Type::Array) {
        error = "record: missing or non-array field 'timing_samples_ms'";
        return false;
    }
    for (const Json& s : samples->items()) {
        if (!s.is_number()) {
            error = "record: non-numeric timing sample";
            return false;
        }
        r.timing_samples_ms.push_back(s.as_double());
    }
    char* end = nullptr;
    r.seed = std::strtoull(seed_text.c_str(), &end, 10);
    if (seed_text.empty() || *end != '\0') {
        error = "record: seed is not an unsigned integer";
        return false;
    }
    r.warmup_runs = static_cast<int>(warm);
    r.repetitions = static_cast<int>(reps);
    r.candidate_budget = static_cast<int>(budget);
    out = r;
    return true;
}

std::string csv_escape(const std::string& f) {
    if (f.find_first_of(",\"\n\r") == std::string::npos) return f;
    std::string r = "\"";
    for (char c : f) {
        if (c == '"') r += "\"\"";
        else r.push_back(c);
    }
    r.push_back('"');
    return r;
}

namespace {

std::string fmt(double v) {
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.17g", v);
    return buf;
}

}  // namespace

std::string csv_header() {
    return "experiment_id,benchmark,source_hash,compiler_version,flags,opt_level,fp_semantics,input_size,size_role,seed,"
           "candidate_id,build_status,correctness_status,failure_reason,warmup_runs,repetitions,timing_samples_ms,"
           "median_ms,q1_ms,q3_ms,iqr_ms,mad_ms,min_ms,max_ms,binary_size_bytes,build_time_s,candidate_budget,"
           "search_strategy,tuning_wall_time_s,selected,lifecycle,rank";
}

std::string record_to_csv_row(const RunRecord& r) {
    Json samples = Json::array();
    for (double x : r.timing_samples_ms) samples.push(Json::number(x));
    std::vector<std::string> f = {
        r.experiment_id, r.benchmark, r.source_hash, r.compiler_version, r.flags, r.opt_level, r.fp_semantics,
        i64_to_string(r.input_size), r.size_role, u64_to_string(r.seed),
        r.candidate_id, r.build_status, r.correctness_status, r.failure_reason, std::to_string(r.warmup_runs),
        std::to_string(r.repetitions), samples.dump(-1), fmt(r.median_ms), fmt(r.q1_ms), fmt(r.q3_ms), fmt(r.iqr_ms),
        fmt(r.mad_ms), fmt(r.min_ms), fmt(r.max_ms), i64_to_string(r.binary_size_bytes),
        fmt(r.build_time_s), std::to_string(r.candidate_budget), r.search_strategy, fmt(r.tuning_wall_time_s),
        r.selected ? "true" : "false", r.lifecycle, std::to_string(r.rank)};
    std::string row;
    for (size_t i = 0; i < f.size(); ++i) {
        if (i) row.push_back(',');
        row += csv_escape(f[i]);
    }
    return row;
}

std::string raw_csv_header() {
    return "experiment_id,candidate_id,benchmark,input_size,repetition,order_position,time_ms,process_wall_ms";
}

std::string raw_to_csv_row(const RawSample& s) {
    return csv_escape(s.experiment_id) + "," + csv_escape(s.candidate_id) + "," + csv_escape(s.benchmark) + "," +
           i64_to_string(s.input_size) + "," + std::to_string(s.repetition) + "," +
           std::to_string(s.order_position) + "," + fmt(s.time_ms) + "," + fmt(s.process_wall_ms);
}

}  // namespace bench
}  // namespace nova

#include "nova/tune/adaptive.hpp"

#include <algorithm>
#include <cmath>

#include "nova/bench/util.hpp"

namespace nova {
namespace tune {

using bench::Json;

bool parse_ablation(const std::string& text, Ablation& out) {
    Ablation a;
    if (text == "none") {
    } else if (text == "no-features") {
        a.use_features = false;
    } else if (text == "no-history") {
        a.use_history = false;
    } else if (text == "no-feedback") {
        a.use_feedback = false;
    } else {
        return false;
    }
    out = a;
    return true;
}

std::string ablation_name(const Ablation& a) {
    if (!a.use_features) return "no-features";
    if (!a.use_history) return "no-history";
    if (!a.use_feedback) return "no-feedback";
    return "none";
}

// ---- history ------------------------------------------------------------------------------------------------------

std::vector<HistoryEntry> filter_history(const std::vector<HistoryEntry>& entries, const std::string& benchmark,
                                         std::int64_t size, bool exclude_same_workload) {
    std::vector<HistoryEntry> out;
    for (const HistoryEntry& e : entries) {
        if (e.size_role != "tuning") continue;  // holdout / ad-hoc measurements never influence a selection
        if (exclude_same_workload && e.benchmark == benchmark && e.size == size) continue;
        out.push_back(e);
    }
    return out;
}

Json history_to_json(const std::vector<HistoryEntry>& entries) {
    Json arr = Json::array();
    for (const HistoryEntry& e : entries) {
        Json o = Json::object();
        o.set("benchmark", Json::string(e.benchmark));
        o.set("size", Json::integer(e.size));
        o.set("size_role", Json::string(e.size_role));
        o.set("candidate", Json::string(e.candidate));
        o.set("log_gain", Json::number(e.log_gain));
        o.set("failed", Json::boolean(e.failed));
        o.set("source", Json::string(e.source));
        arr.push(o);
    }
    Json root = Json::object();
    root.set("format", Json::string("nova-history-1"));
    root.set("entries", arr);
    return root;
}

bool history_from_json(const Json& j, std::vector<HistoryEntry>& out, std::string& error) {
    const Json* fmt = j.find("format");
    if (!fmt || fmt->type() != Json::Type::String || fmt->as_string() != "nova-history-1") {
        error = "history file: missing or unsupported 'format'";
        return false;
    }
    const Json* arr = j.find("entries");
    if (!arr || arr->type() != Json::Type::Array) {
        error = "history file: missing 'entries' array";
        return false;
    }
    std::vector<HistoryEntry> result;
    for (size_t i = 0; i < arr->items().size(); ++i) {
        const Json& o = arr->items()[i];
        auto field = [&](const char* key) { return o.find(key); };
        const Json *b = field("benchmark"), *s = field("size"), *r = field("size_role"), *c = field("candidate"),
                   *g = field("log_gain"), *f = field("failed"), *src = field("source");
        if (!b || b->type() != Json::Type::String || !s || s->type() != Json::Type::Int || !r || r->type() != Json::Type::String ||
            !c || c->type() != Json::Type::String || !g || !g->is_number() || !f || f->type() != Json::Type::Bool || !src ||
            src->type() != Json::Type::String) {
            error = "history file: entry " + std::to_string(i) + " is missing a field or has the wrong type";
            return false;
        }
        HistoryEntry e;
        e.benchmark = b->as_string();
        e.size = s->as_int();
        e.size_role = r->as_string();
        e.candidate = c->as_string();
        e.log_gain = g->as_double();
        e.failed = f->as_bool();
        e.source = src->as_string();
        result.push_back(e);
    }
    out = result;
    return true;
}

bool load_history_file(const std::string& path, std::vector<HistoryEntry>& out, std::string& error) {
    out.clear();
    if (!bench::file_exists(path)) return true;  // no history yet
    std::string text;
    if (!bench::read_file(path, text)) {
        error = "cannot read history file '" + path + "'";
        return false;
    }
    Json j;
    std::string perr;
    if (!Json::parse(text, j, perr)) {
        error = "history file '" + path + "': " + perr;
        return false;
    }
    return history_from_json(j, out, error);
}

bool append_history_file(const std::string& path, const std::vector<HistoryEntry>& entries, std::string& error) {
    for (const HistoryEntry& e : entries) {
        if (e.size_role != "tuning") {
            error = "refusing to store a '" + e.size_role + "' measurement in the history (only tuning-size results may be stored)";
            return false;
        }
    }
    std::vector<HistoryEntry> all;
    if (!load_history_file(path, all, error)) return false;
    all.insert(all.end(), entries.begin(), entries.end());
    if (!bench::write_file(path, history_to_json(all).dump(2) + "\n")) {
        error = "cannot write history file '" + path + "'";
        return false;
    }
    return true;
}

// ---- policy -------------------------------------------------------------------------------------------------------

FlagFeatures flag_features(const Candidate& c) {
    FlagFeatures f;
    for (const std::string& flag : c.flags) {
        if (flag == "-O3") f.o3 = true;
        else if (flag == "-funroll-loops") f.unroll = true;
        else if (flag == "-ftree-vectorize") f.vec = true;
        else if (flag == "-fprefetch-loop-arrays") f.prefetch = true;
        else if (flag == "-march=native") f.native = true;
    }
    if (f.o3) f.vec = true;  // vectorization is implied by -O3
    return f;
}

namespace {

int hamming(const FlagFeatures& a, const FlagFeatures& b) {
    return (a.o3 != b.o3) + (a.unroll != b.unroll) + (a.vec != b.vec) + (a.prefetch != b.prefetch) + (a.native != b.native);
}

}  // namespace

AdaptivePolicy::AdaptivePolicy(const SearchSpace& space, const WorkloadFeatures& workload,
                               const std::vector<HistoryEntry>& history, const Ablation& ablation, std::uint64_t seed)
    : space_(space), workload_(workload), history_(history), ablation_(ablation) {
    const size_t n = space.candidates.size();
    evaluated_.assign(n, false);
    outcome_.assign(n, 0.0);
    for (size_t i = 0; i < n; ++i) {
        flags_.push_back(flag_features(space.candidates[i]));
        bench::Rng r(seed * 0x9E3779B97F4A7C15ULL + (i + 1) * 0xD1B54A32D192ED03ULL);
        tie_key_.push_back(r.uniform());
    }
}

// Prior expectations in log-gain units. The multipliers describe how strongly the workload suggests the flag helps;
// every constant is documented in docs/adaptive.md and is a hypothesis, not a measurement.
double AdaptivePolicy::prior(int index) const {
    if (!ablation_.use_features || !workload_.known) return 0.0;
    const FlagFeatures& f = flags_[static_cast<size_t>(index)];
    const bool streaming = workload_.unit_stride_streaming;
    const bool vectorizable = streaming && !workload_.innermost_reduction;
    const double vec_mult = vectorizable ? 1.0 : 0.25;
    const double unroll_mult = streaming ? 1.0 : 0.5;
    const double prefetch_mult = workload_.working_set_bytes > 4.0 * 1024 * 1024 ? 1.0 : 0.3;
    const double native_mult = vectorizable ? 1.0 : 0.4;
    double s = 0.0;
    if (f.o3) s += 0.05;
    if (f.vec) s += 0.20 * vec_mult;
    if (f.unroll) s += 0.10 * unroll_mult;
    if (f.prefetch) s += 0.10 * prefetch_mult;
    if (f.native) s += 0.10 * native_mult;
    return s;
}

double AdaptivePolicy::history(int index) const {
    if (!ablation_.use_history || history_.empty()) return 0.0;
    const std::string& id = space_.candidates[static_cast<size_t>(index)].id;
    double num = 0.0, den = 0.0;
    for (const HistoryEntry& h : history_) {
        if (h.candidate != id || h.size_role != "tuning") continue;
        double w;
        if (workload_.known && h.benchmark == workload_.benchmark) {
            double dist = std::fabs(std::log(static_cast<double>(std::max<std::int64_t>(h.size, 1))) / std::log(2.0) - workload_.size_log2);
            w = std::exp(-dist / 4.0);  // same family: similarity decays with log2 size distance
        } else {
            w = 0.25;  // other family: weak transfer
        }
        num += w * (h.failed ? kFailureLogGain : h.log_gain);
        den += w;
    }
    return num / (den + kShrinkage);
}

double AdaptivePolicy::feedback(int index) const {
    if (!ablation_.use_feedback) return 0.0;
    double num = 0.0, den = 0.0;
    for (size_t e = 0; e < evaluated_.size(); ++e) {
        if (!evaluated_[e]) continue;
        double sim = std::exp(-static_cast<double>(hamming(flags_[static_cast<size_t>(index)], flags_[e])));
        num += sim * outcome_[e];
        den += sim;
    }
    return num / (den + kShrinkage);
}

double AdaptivePolicy::score(int index) const { return prior(index) + history(index) + feedback(index); }

int AdaptivePolicy::next() {
    int best = -1;
    double best_score = 0.0;
    for (size_t i = 0; i < evaluated_.size(); ++i) {
        if (evaluated_[i]) continue;
        double s = score(static_cast<int>(i));
        bool better = best < 0 || s > best_score + 1e-12 ||
                      (std::fabs(s - best_score) <= 1e-12 && tie_key_[i] < tie_key_[static_cast<size_t>(best)]);
        if (better) {
            best = static_cast<int>(i);
            best_score = s;
        }
    }
    if (best >= 0) {
        evaluated_[static_cast<size_t>(best)] = true;
        outcome_[static_cast<size_t>(best)] = 0.0;  // until observe() is called
        order_.push_back(best);
    }
    return best;
}

void AdaptivePolicy::observe(int index, bool ok, double log_gain) {
    outcome_[static_cast<size_t>(index)] = ok ? log_gain : kFailureLogGain;
}

}  // namespace tune
}  // namespace nova

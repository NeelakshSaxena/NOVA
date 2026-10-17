#include <cmath>
#include <string>
#include <vector>

#include "check.hpp"
#include "nova/bench/env.hpp"
#include "nova/bench/json.hpp"
#include "nova/bench/process.hpp"
#include "nova/bench/records.hpp"
#include "nova/bench/registry.hpp"
#include "nova/bench/sha256.hpp"
#include "nova/bench/stats.hpp"
#include "nova/bench/util.hpp"

using namespace nova::bench;

namespace {

void test_sha256() {
    CHECK_EQ(sha256_hex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_EQ(sha256_hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
             std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    CHECK_EQ(sha256_hex(std::string(1000000, 'a')),
             std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    CHECK(sha256_hex("a") != sha256_hex("b"));
}

void test_util() {
    CHECK_EQ(u64_to_string(0), std::string("0"));
    CHECK_EQ(u64_to_string(18446744073709551615ULL), std::string("18446744073709551615"));
    CHECK_EQ(i64_to_string(-9223372036854775807LL - 1), std::string("-9223372036854775808"));
    CHECK_EQ(join({"a", "b", "c"}), std::string("a b c"));
    // Rng is fully specified: fixed first outputs of splitmix64(seed 0) (known reference values).
    Rng r(0);
    CHECK_EQ(r.next(), std::uint64_t(0xE220A8397B1DCDAFULL));
    CHECK_EQ(r.next(), std::uint64_t(0x6E789E6AA1B965F4ULL));
    // shuffle is a permutation and deterministic for a seed
    std::vector<int> a = {1, 2, 3, 4, 5, 6, 7, 8}, b = a;
    Rng r1(42), r2(42);
    shuffle(a, r1);
    shuffle(b, r2);
    CHECK(a == b);
    std::vector<int> sorted = a;
    std::sort(sorted.begin(), sorted.end());
    CHECK((sorted == std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8}));
}

void test_json() {
    Json o = Json::object();
    o.set("s", Json::string("quote\" back\\ nl\n tab\t ctl\x01 utf8 \xC3\xA9"));
    o.set("i", Json::integer(-9223372036854775807LL - 1));
    o.set("d", Json::number(0.1));
    o.set("big", Json::number(1.0000000000000001e300));
    o.set("b", Json::boolean(true));
    o.set("n", Json::null());
    Json arr = Json::array();
    arr.push(Json::integer(1)).push(Json::number(2.5)).push(Json::string("x"));
    o.set("a", arr);
    o.set("o", Json::object());
    o.set("e", Json::array());
    for (int indent : {-1, 0, 2}) {
        Json back;
        std::string err;
        CHECK_MSG(Json::parse(o.dump(indent), back, err), "parse own output (indent " + std::to_string(indent) + "): " + err);
        CHECK_MSG(back == o, "round trip equals original (indent " + std::to_string(indent) + ")");
    }
    // doubles survive exactly
    for (double v : {0.1, 1.0 / 3.0, 1e-300, 123456789.123456789, 5e-324, 1.7976931348623157e308, -0.5}) {
        Json back;
        std::string err;
        CHECK(Json::parse(Json::number(v).dump(-1), back, err));
        CHECK_MSG(back.as_double() == v, "double " + std::to_string(v) + " round trips");
    }
    Json x;
    std::string err;
    CHECK(Json::parse("[1, 2.0, \"\\u00e9\", true, null, {\"k\": []}]", x, err));
    CHECK_EQ(x.items().size(), size_t(6));
    CHECK(x.items()[0].type() == Json::Type::Int);
    CHECK(x.items()[1].type() == Json::Type::Double);
    CHECK_EQ(x.items()[2].as_string(), std::string("\xC3\xA9"));
    // errors
    for (const char* bad : {"", "{", "[1,]", "{\"a\" 1}", "{\"a\":1,}", "tru", "\"unterminated", "[1 2]", "01x", "{\"a\":1} extra",
                            "\"bad\\q\"", "-", "1.", "[", "nul"}) {
        Json y;
        std::string e;
        CHECK_MSG(!Json::parse(bad, y, e), std::string("rejects: ") + bad);
        CHECK_MSG(!e.empty(), std::string("error message for: ") + bad);
    }
    // deep nesting is rejected, not a crash
    std::string deep(1000, '[');
    Json z;
    std::string e2;
    CHECK(!Json::parse(deep, z, e2));
    // find / set replace
    Json p = Json::object();
    p.set("k", Json::integer(1));
    p.set("k", Json::integer(2));
    CHECK_EQ(p.members().size(), size_t(1));
    CHECK_EQ(p.find("k")->as_int(), std::int64_t(2));
    CHECK(p.find("missing") == nullptr);
}

void test_stats() {
    Summary s = summarize({});
    CHECK_EQ(s.count, size_t(0));
    s = summarize({5.0});
    CHECK(s.median == 5.0 && s.iqr == 0.0 && s.mad == 0.0 && s.min == 5.0 && s.max == 5.0);
    s = summarize({4, 1, 3, 2});  // unsorted input
    CHECK(s.median == 2.5);
    CHECK(s.min == 1 && s.max == 4);
    CHECK(std::fabs(s.q1 - 1.75) < 1e-12 && std::fabs(s.q3 - 3.25) < 1e-12);
    CHECK(std::fabs(s.iqr - 1.5) < 1e-12);
    CHECK(std::fabs(s.mad - 1.0) < 1e-12);  // deviations 1.5,0.5,0.5,1.5 -> median 1.0
    CHECK(std::fabs(s.mean - 2.5) < 1e-12);
    s = summarize({1, 2, 3, 4, 100});  // outlier: median/IQR robust, mean is not
    CHECK(s.median == 3 && s.q1 == 2 && s.q3 == 4 && s.iqr == 2);
    CHECK(s.mean > 20);
    std::vector<double> in = {3, 1, 2};
    summarize(in);
    CHECK((in == std::vector<double>{3, 1, 2}));  // input untouched
}

RunRecord good_record() {
    RunRecord r;
    r.experiment_id = "exp";
    r.benchmark = "matmul";
    r.source_hash = sha256_hex("kernel");
    r.compiler_version = "g++ test 1.0";
    r.flags = "-std=c++17 -O2";
    r.opt_level = "O2";
    r.fp_semantics = "strict";
    r.input_size = 256;
    r.size_role = "tuning";
    r.seed = 18446744073709551615ULL;  // exercises 64-bit seed storage
    r.candidate_id = "matmul-O2";
    r.build_status = "OK";
    r.correctness_status = "PASS";
    r.warmup_runs = 1;
    r.repetitions = 5;
    r.timing_samples_ms = {10.5, 10.25, 11.0, 10.75, 10.5};
    r.binary_size_bytes = 82232;
    r.build_time_s = 0.5;
    r.search_strategy = "baseline";
    r.lifecycle = "BENCHMARKED";
    compute_statistics(r);
    return r;
}

void test_records() {
    RunRecord r = good_record();
    CHECK_MSG(validate_record(r).empty(), "good record validates");
    CHECK(r.median_ms == 10.5);

    // JSON round trip is exact (including the 64-bit seed and every raw sample)
    Json j = record_to_json(r);
    Json back;
    std::string err;
    CHECK(Json::parse(j.dump(2), back, err));
    RunRecord r2;
    CHECK_MSG(record_from_json(back, r2, err), err);
    CHECK_EQ(r2.seed, r.seed);
    CHECK(r2.timing_samples_ms == r.timing_samples_ms);
    CHECK(record_to_json(r2) == j);
    CHECK_EQ(r2.flags, r.flags);
    CHECK_EQ(r2.input_size, r.input_size);

    // Missing / mistyped fields are detected on load, naming the field.
    for (const auto& kv : j.members()) {
        if (kv.first == "lifecycle" || kv.first == "rank") continue;  // optional on load (pre-V0.7 files); tested below
        Json broken = Json::object();
        for (const auto& m : j.members())
            if (m.first != kv.first) broken.set(m.first, m.second);
        RunRecord x;
        std::string e;
        CHECK_MSG(!record_from_json(broken, x, e), "load fails without field " + kv.first);
        CHECK_MSG(e.find(kv.first) != std::string::npos, "error names field " + kv.first + ": " + e);
    }
    Json wrong = j;
    wrong.set("input_size", Json::string("256"));
    RunRecord x;
    CHECK(!record_from_json(wrong, x, err));

    // lifecycle/rank: absent in files written before V0.7 -> derived; present but wrongly typed -> error.
    {
        Json old_style = Json::object();
        for (const auto& kv : j.members())
            if (kv.first != "lifecycle" && kv.first != "rank") old_style.set(kv.first, kv.second);
        RunRecord o;
        CHECK_MSG(record_from_json(old_style, o, err), err);
        CHECK_EQ(o.lifecycle, std::string("BENCHMARKED"));
        CHECK_EQ(o.rank, 0);
        Json bad_type = j;
        bad_type.set("lifecycle", Json::integer(3));
        CHECK(!record_from_json(bad_type, o, err));
        bad_type = j;
        bad_type.set("rank", Json::string("1"));
        CHECK(!record_from_json(bad_type, o, err));
    }

    // Semantic validation: each kind of missing/inconsistent metadata is reported.
    auto bad = [&](const char* what, RunRecord m) {
        CHECK_MSG(!validate_record(m).empty(), std::string("validate_record detects: ") + what);
    };
    RunRecord m = r; m.experiment_id.clear(); bad("empty experiment id", m);
    m = r; m.source_hash = "abc"; bad("short source hash", m);
    m = r; m.compiler_version.clear(); bad("missing compiler version", m);
    m = r; m.flags.clear(); bad("missing flags", m);
    m = r; m.input_size = 0; bad("zero input size", m);
    m = r; m.size_role = "other"; bad("unknown size role", m);
    m = r; m.fp_semantics = "loose"; bad("unknown fp semantics", m);
    m = r; m.timing_samples_ms.pop_back(); bad("sample count != repetitions", m);
    m = r; m.median_ms += 1; bad("median inconsistent with samples", m);
    m = r; m.iqr_ms += 1; bad("iqr inconsistent with samples", m);
    m = r; m.timing_samples_ms[0] = -1; compute_statistics(m); bad("negative sample", m);
    m = r; m.binary_size_bytes = 0; bad("built binary without size", m);
    m = r; m.build_status = "FAILED"; bad("build failed without reason", m);
    m = r; m.correctness_status = "FAIL"; bad("correctness fail without reason", m);
    m = r; m.correctness_status = "NOT_RUN"; bad("samples although not run", m);
    m = r; m.search_strategy.clear(); bad("missing strategy", m);
    m = r; m.build_status = "MAYBE"; bad("unknown build status", m);
    m = r; m.lifecycle = "WINNER"; bad("unknown lifecycle", m);
    m = r; m.lifecycle = "BUILD_FAILED"; bad("lifecycle BUILD_FAILED but build OK", m);
    m = r; m.lifecycle = "CORRECTNESS_FAILED"; bad("lifecycle CORRECTNESS_FAILED but PASS", m);
    m = r; m.rank = 1; bad("ranked without being ELIGIBLE", m);
    m = r; m.search_strategy = "random"; m.selected = true; bad("selected but not ELIGIBLE", m);
    m = r; m.lifecycle = "ELIGIBLE"; m.rank = -1; bad("negative rank", m);

    // A legitimately failed record validates (failure is recorded, not hidden)
    RunRecord f = r;
    f.correctness_status = "FAIL";
    f.lifecycle = "CORRECTNESS_FAILED";
    f.failure_reason = "out.sum differs";
    f.timing_samples_ms.clear();
    compute_statistics(f);
    CHECK_MSG(validate_record(f).empty(), "failed-correctness record with reason validates");
    f = r;
    f.build_status = "FAILED";
    f.correctness_status = "NOT_RUN";
    f.lifecycle = "BUILD_FAILED";
    f.failure_reason = "compiler exited with code 1";
    f.timing_samples_ms.clear();
    f.binary_size_bytes = 0;
    compute_statistics(f);
    CHECK_MSG(validate_record(f).empty(), "failed-build record with reason validates");
}

void test_csv() {
    RunRecord r = good_record();
    r.failure_reason = "has, comma and \"quote\"";
    std::string header = csv_header();
    std::string row = record_to_csv_row(r);
    // same number of top-level fields in header and row
    auto count_fields = [](const std::string& s) {
        int n = 1;
        bool inq = false;
        for (char c : s) {
            if (c == '"') inq = !inq;
            else if (c == ',' && !inq) ++n;
        }
        return n;
    };
    CHECK_EQ(count_fields(header), 32);
    CHECK_EQ(count_fields(row), 32);
    CHECK(row.find("\"has, comma and \"\"quote\"\"\"") != std::string::npos);
    CHECK(row.find("[10.5,10.25,11.0,10.75,10.5]") != std::string::npos || row.find("10.25") != std::string::npos);
    CHECK_EQ(csv_escape("plain"), std::string("plain"));
    CHECK_EQ(csv_escape("a\nb"), std::string("\"a\nb\""));
    // every column the benchmark protocol requires is present in the header
    for (const char* col : {"experiment_id", "benchmark", "source_hash", "compiler_version", "flags", "input_size", "seed",
                            "candidate_id", "build_status", "correctness_status", "failure_reason", "timing_samples_ms",
                            "median_ms", "binary_size_bytes", "candidate_budget", "search_strategy", "tuning_wall_time_s",
                            "selected"})
        CHECK_MSG(header.find(col) != std::string::npos, std::string("csv header has ") + col);
}

void test_environment() {
    Environment e;
    CHECK_EQ(missing_environment_fields(e).size(), size_t(8));
    e.os = "os"; e.cpu = "cpu"; e.logical_cpus = 4; e.ram_bytes = 1; e.compiler_command = "g++";
    e.compiler_version = "v"; e.compiler_target = "t"; e.nova_version = "0.1.0";
    CHECK(missing_environment_fields(e).empty());
    Environment back;
    std::string err;
    CHECK_MSG(environment_from_json(environment_to_json(e), back, err), err);
    CHECK_EQ(back.cpu, e.cpu);
    CHECK_EQ(back.ram_bytes, e.ram_bytes);
    Json j = environment_to_json(e);
    Json broken = Json::object();
    for (const auto& m : j.members())
        if (m.first != "cpu") broken.set(m.first, m.second);
    CHECK(!environment_from_json(broken, back, err));
    CHECK(err.find("cpu") != std::string::npos);
}

std::string kernel_text(const std::string& sum = "10", const std::string& extra = "") {
    return "nova-kernel=1\nkernel=matmul\nn=4\nseed=1\nout.len=16\nout.sum=" + sum +
           "\nout.s0=1\nout.s1=2\nout.s2=3\nout.wsum=40\ntime_ms=1.5\n" + extra;
}

void test_kernel_output_and_oracle() {
    KernelOutput k;
    std::string err;
    CHECK_MSG(parse_kernel_output(kernel_text(), k, err), err);
    CHECK(k.sum == 10 && k.wsum == 40 && k.time_ms == 1.5 && k.n == 4 && k.len == 16 && k.seed == 1);
    CHECK_EQ(k.kernel, std::string("matmul"));
    // CRLF line endings and extra lines are tolerated
    std::string crlf;
    for (char c : kernel_text("10", "ignored line\n")) {
        if (c == '\n') crlf += "\r\n";
        else crlf.push_back(c);
    }
    CHECK_MSG(parse_kernel_output(crlf, k, err), err);
    // every field is required
    for (const char* field : {"nova-kernel", "kernel", "n", "seed", "out.len", "out.sum", "out.s0", "out.s1", "out.s2", "out.wsum", "time_ms"}) {
        std::string text;
        std::string prefix = std::string(field) + "=";
        std::string src = kernel_text();
        size_t pos = 0;
        while (pos < src.size()) {
            size_t nl = src.find('\n', pos);
            std::string line = src.substr(pos, nl - pos);
            if (line.rfind(prefix, 0) != 0) text += line + "\n";
            pos = nl + 1;
        }
        KernelOutput x;
        std::string e;
        CHECK_MSG(!parse_kernel_output(text, x, e), std::string("rejects output without ") + field);
        CHECK_MSG(e.find(field) != std::string::npos, std::string("error names ") + field + ": " + e);
    }
    CHECK(!parse_kernel_output("", k, err));
    CHECK(!parse_kernel_output(kernel_text("nan"), k, err));
    CHECK(!parse_kernel_output(kernel_text("inf"), k, err));
    CHECK(!parse_kernel_output(kernel_text("1.5abc"), k, err));
    CHECK(!parse_kernel_output("nova-kernel=2\n", k, err));

    // Oracle: identical output passes; every deliberately wrong statistic is detected.
    KernelOutput good;
    CHECK(parse_kernel_output(kernel_text(), good, err));
    CHECK(compare_to_reference(good, good).empty());
    KernelOutput within = good;
    within.sum += 1e-9 * 10 * 0.5;  // inside the documented tolerance
    CHECK(compare_to_reference(good, within).empty());
    struct Mut { const char* name; void (*apply)(KernelOutput&); };
    const Mut muts[] = {
        {"sum", [](KernelOutput& o) { o.sum += 1e-6; }},
        {"s0", [](KernelOutput& o) { o.s0 += 0.01; }},
        {"s1", [](KernelOutput& o) { o.s1 -= 0.01; }},
        {"s2", [](KernelOutput& o) { o.s2 *= 2; }},
        {"wsum", [](KernelOutput& o) { o.wsum += 1; }},
        {"len", [](KernelOutput& o) { o.len += 1; }},
        {"n", [](KernelOutput& o) { o.n += 1; }},
        {"kernel", [](KernelOutput& o) { o.kernel = "other"; }},
    };
    for (const Mut& m : muts) {
        KernelOutput bad = good;
        m.apply(bad);
        CHECK_MSG(!compare_to_reference(good, bad).empty(), std::string("oracle detects wrong ") + m.name);
    }
    // time_ms is not part of the oracle
    KernelOutput slow = good;
    slow.time_ms = 1e9;
    CHECK(compare_to_reference(good, slow).empty());
}

void test_reference_properties() {
    // Deterministic, and sensitive to size and seed.
    KernelOutput a, b, c, d;
    std::string err;
    for (const char* name : {"matmul", "vector_add", "stencil"}) {
        CHECK_MSG(reference_output(name, 16, 1, a, err), err);
        CHECK(reference_output(name, 16, 1, b, err));
        CHECK(reference_output(name, 17, 1, c, err));
        CHECK(reference_output(name, 16, 2, d, err));
        CHECK_MSG(compare_to_reference(a, b).empty(), std::string(name) + ": reference deterministic");
        CHECK_MSG(!compare_to_reference(a, c).empty(), std::string(name) + ": reference depends on n");
        CHECK_MSG(!compare_to_reference(a, d).empty(), std::string(name) + ": reference depends on seed");
        CHECK(a.len == (std::string(name) == "matmul" ? 256u : 16u));
        CHECK(a.sum > 0);
    }
    CHECK(!reference_output("nope", 4, 1, a, err));
    CHECK(!reference_output("matmul", 0, 1, a, err));
    CHECK(!reference_output("stencil", 2, 1, a, err));
    // Hand-checkable case: vector_add with n=1 is a0 + 16*b0 where a0,b0 are the first two splitmix64(seed) draws.
    CHECK(reference_output("vector_add", 1, 7, a, err));
    Rng r(7);
    double a0 = static_cast<double>(r.next() >> 11) * (1.0 / 9007199254740992.0);
    double b0 = static_cast<double>(r.next() >> 11) * (1.0 / 9007199254740992.0);
    long double expect = static_cast<long double>(a0) + 16.0L * static_cast<long double>(b0);
    CHECK(std::fabs(a.sum - static_cast<double>(expect)) < 1e-12);
    CHECK(a.s0 == a.sum && a.s1 == a.sum && a.s2 == a.sum);
    // Registry sanity: holdout sizes are disjoint from tuning sizes.
    for (const BenchmarkDef& def : registry()) {
        CHECK(!def.tuning_sizes.empty() && !def.holdout_sizes.empty());
        for (std::int64_t h : def.holdout_sizes)
            for (std::int64_t t : def.tuning_sizes) CHECK_MSG(h != t, def.name + ": holdout and tuning sizes are disjoint");
    }
    CHECK(registry().size() >= 3);
    CHECK(find_benchmark("matmul") != nullptr && find_benchmark("zzz") == nullptr);
}

}  // namespace

int main() {
    test_sha256();
    test_util();
    test_json();
    test_stats();
    test_records();
    test_csv();
    test_environment();
    test_kernel_output_and_oracle();
    test_reference_properties();
    return nova_test::finish("bench-unit");
}

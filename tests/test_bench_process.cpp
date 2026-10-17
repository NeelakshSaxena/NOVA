#include <string>
#include <vector>

#include "check.hpp"
#include "nova/bench/process.hpp"
#include "nova/bench/util.hpp"

using namespace nova::bench;

namespace {

const std::string kChild = NOVA_HELPER_CHILD;
const std::string kTmp = NOVA_TEST_TMP_DIR;

void test_arguments_are_not_interpreted() {
    // Each of these must arrive as exactly one argument, unchanged (no shell, no re-splitting).
    const std::vector<std::string> tricky = {
        "plain",           "with space",          "two  spaces",      "\"quoted\"",     "trailing\\",
        "trailing\\\\",    "back\\\"slash-quote", "",                 "*",              "$HOME",
        "x&y",             "a|b",                 ";echo injected",   "&& echo pwned",  "> nova_injected.txt",
        "%PATH%",          "tab\there",           "it's",             "(paren)",        "^caret",
    };
    std::vector<std::string> argv = {kChild, "echo"};
    argv.insert(argv.end(), tricky.begin(), tricky.end());
    ProcessResult r = run_process(argv, 20000);
    CHECK_MSG(r.started, r.error);
    CHECK_EQ(r.exit_code, 0);
    std::string expected;
    for (const std::string& a : tricky) expected += "[" + a + "]\n";
    // Normalize CRLF produced by text-mode stdout on Windows.
    std::string got;
    for (char c : r.out)
        if (c != '\r') got.push_back(c);
    CHECK_EQ(got, expected);
    CHECK_MSG(!file_exists("nova_injected.txt"), "redirection characters were not interpreted by a shell");
}

void test_exit_codes_and_streams() {
    ProcessResult r = run_process({kChild, "exit", "7"}, 20000);
    CHECK(r.started && !r.timed_out);
    CHECK_EQ(r.exit_code, 7);
    r = run_process({kChild, "exit", "0"}, 20000);
    CHECK_EQ(r.exit_code, 0);
    r = run_process({kChild, "err", "to-stderr"}, 20000);
    CHECK(r.started);
    CHECK_EQ(r.err, std::string("to-stderr"));
    CHECK_EQ(r.out, std::string(""));
    CHECK(r.wall_ms >= 0);
}

void test_launch_failure() {
    ProcessResult r = run_process({"definitely-not-a-real-program-nova"}, 5000);
    CHECK(!r.started);
    CHECK(r.error.find("definitely-not-a-real-program-nova") != std::string::npos);
    r = run_process({}, 5000);
    CHECK(!r.started);
    CHECK(!r.error.empty());
    r = run_process({""}, 5000);
    CHECK(!r.started);
}

void test_timeout() {
    ProcessResult slow = run_process({kChild, "sleep", "20000"}, 300);
    CHECK(slow.started);
    CHECK(slow.timed_out);
    CHECK_MSG(slow.wall_ms < 10000, "timed-out process was killed promptly");
    ProcessResult quick = run_process({kChild, "sleep", "30"}, 20000);
    CHECK(quick.started && !quick.timed_out);
    CHECK_EQ(quick.exit_code, 0);
    CHECK(quick.out.find("done") != std::string::npos);
    CHECK_MSG(quick.wall_ms >= 25, "wall time covers the child's runtime");
}

void test_output_cap() {
    // A child producing far more output than the cap must neither deadlock nor grow memory without bound.
    ProcessResult r = run_process({kChild, "flood", "3000000"}, 60000, 1000);
    CHECK(r.started && !r.timed_out);
    CHECK_EQ(r.exit_code, 0);
    CHECK_EQ(r.out.size(), size_t(1000));
    CHECK(r.output_truncated);
    ProcessResult full = run_process({kChild, "flood", "200000"}, 60000);
    CHECK_EQ(full.out.size(), size_t(200000));
    CHECK(!full.output_truncated);
}

}  // namespace

int main() {
    make_dirs(kTmp);
    test_arguments_are_not_interpreted();
    test_exit_codes_and_streams();
    test_launch_failure();
    test_timeout();
    test_output_cap();
    return nova_test::finish("process");
}

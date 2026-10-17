// Minimal dependency-free tests for the CLI front end.
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "nova/cli.hpp"

namespace {

int failures = 0;

struct Result {
    int code;
    std::string out;
    std::string err;
};

Result run(const std::vector<std::string>& args) {
    std::ostringstream out, err;
    int code = nova::run_cli(args, out, err);
    return {code, out.str(), err.str()};
}

void check(bool cond, const char* what) {
    if (!cond) {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    }
}

bool contains(const std::string& s, const std::string& needle) {
    return s.find(needle) != std::string::npos;
}

}  // namespace

int main() {
    {
        Result r = run({"--help"});
        check(r.code == 0, "--help exits 0");
        check(contains(r.out, "Usage: nova"), "--help prints usage on stdout");
        check(r.err.empty(), "--help prints nothing on stderr");
        check(run({"-h"}).code == 0, "-h exits 0");
    }
    {
        Result r = run({"--version"});
        check(r.code == 0, "--version exits 0");
        check(r.out == "nova " + nova::version() + "\n", "--version prints 'nova <version>'");
        check(!nova::version().empty() && nova::version() != "unknown", "version is defined");
    }
    {
        Result r = run({"--bogus"});
        check(r.code != 0, "unknown option exits non-zero");
        check(contains(r.err, "unknown argument '--bogus'"), "unknown option names the argument");
        check(r.out.empty(), "unknown option prints nothing on stdout");
        check(run({"compile"}).code != 0, "unimplemented command is rejected");
        check(run({"--help", "extra"}).code != 0, "extra arguments rejected");
    }
    {
        Result r = run({});
        check(r.code != 0, "no arguments exits non-zero");
        check(contains(r.err, "Usage: nova"), "no arguments prints usage to stderr");
    }
    {
        const std::string dir = std::string(NOVA_SOURCE_DIR) + "/examples/";
        Result r = run({"tokens", dir + "basic.nova"});
        check(r.code == 0, "tokens on basic.nova exits 0");
        check(r.out.rfind("1:1 KW_LET 'let'\n", 0) == 0, "tokens output starts with first token");
        check(contains(r.out, "EOF ''"), "tokens output ends with EOF");
        r = run({"tokens", dir + "invalid_char.nova"});
        check(r.code == 1, "tokens on invalid input exits 1");
        check(contains(r.err, "invalid_char.nova:2:11: error: unexpected character '@'"),
              "tokens reports invalid character with location");
        check(r.out.empty(), "tokens prints no token dump when there are errors");
        check(run({"tokens", dir + "missing.nova"}).code == 1, "tokens on missing file exits 1");
        check(run({"tokens"}).code == 2, "tokens without file is a usage error");
        check(run({"tokens", "a", "b"}).code == 2, "tokens with two files is a usage error");
    }
    {
        const std::string dir = std::string(NOVA_SOURCE_DIR) + "/examples/";
        Result r = run({"ast", dir + "basic.nova"});
        check(r.code == 0, "ast on basic.nova exits 0");
        check(r.out.rfind("Program\n  Let x\n    Integer 10\n", 0) == 0, "ast output starts with Program/Let x");
        r = run({"ast", dir + "invalid_syntax.nova"});
        check(r.code == 1, "ast on syntax error exits 1");
        check(contains(r.err, "invalid_syntax.nova:2:14: error: expected expression, found ';'"),
              "ast reports syntax error location");
        check(r.out.empty(), "ast prints no tree on error");
        r = run({"ast", dir + "invalid_char.nova"});
        check(r.code == 1 && contains(r.err, "invalid_char.nova:2:11"), "ast reports lexical errors");
        check(run({"ast", dir + "missing.nova"}).code == 1, "ast on missing file exits 1");
        check(run({"ast"}).code == 2, "ast without file is a usage error");
        check(run({"--help"}).out.find("ast <file>") != std::string::npos, "help lists ast");
        r = run({"opt", dir + "optimize_demo.nova"});
        check(r.code == 0 && r.out == "print 8\nprint 28\n", "opt prints the optimized TAC");
        r = run({"opt", dir + "optimize_demo.nova", "--passes=dce"});
        check(r.code == 0 && contains(r.out, "$t1 = a * 2"), "opt --passes=dce keeps unrelated code");
        r = run({"opt", dir + "optimize_demo.nova", "--show-all"});
        check(r.code == 0 && r.out.rfind("== original ==\n", 0) == 0 && contains(r.out, "== after fold (iteration 1) ==") &&
                  contains(r.out, "== final (fixed point after 3 iterations) =="),
              "opt --show-all shows each changing pass and the final result");
        check(run({"opt", dir + "optimize_demo.nova", "--passes=bogus"}).code == 2, "opt rejects an unknown pass");
        check(run({"opt", dir + "optimize_demo.nova", "--bogus"}).code == 2, "opt rejects an unknown option");
        check(run({"opt"}).code == 2, "opt without file is a usage error");
        check(run({"opt", dir + "undeclared.nova"}).code == 1, "opt rejects semantically invalid programs");
        r = run({"opt", dir + "divzero.nova"});
        check(r.code == 0 && contains(r.out, "/"), "opt keeps a possibly-failing division");
    }
    if (failures == 0) std::cout << "all cli tests passed\n";
    return failures == 0 ? 0 : 1;
}

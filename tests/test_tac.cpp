#include <string>
#include <vector>

#include "check.hpp"
#include "helpers.hpp"
#include "nova/interp.hpp"

using namespace nova;

namespace {

std::string tac_of(const std::string& src) {
    TacProgram tac;
    if (!nova_test::compile_tac(src, tac)) return "INVALID";
    return print_tac(tac);
}

void test_golden_tac() {
    CHECK_EQ(tac_of(""), std::string(""));
    CHECK_EQ(tac_of("let x = 10; let y = 20; let z = x * 2 + y; print z;"),
             std::string("x = 10\ny = 20\n$t1 = x * 2\nz = $t1 + y\nprint z\n"));
    CHECK_EQ(tac_of("print 1 + 2 * 3;"), std::string("$t1 = 2 * 3\n$t2 = 1 + $t1\nprint $t2\n"));
    CHECK_EQ(tac_of("let a = 1; a = -a;"), std::string("a = 1\na = neg a\n"));
    CHECK_EQ(tac_of("let a = 2; print (a + 1) * (a - 1);"),
             std::string("a = 2\n$t1 = a + 1\n$t2 = a - 1\n$t3 = $t1 * $t2\nprint $t3\n"));
    CHECK_EQ(tac_of("let x = 5; let y = x; y = 7;"), std::string("x = 5\ny = x\ny = 7\n"));
    CHECK_EQ(tac_of("let a = 1; print -(a + 1);"), std::string("a = 1\n$t1 = a + 1\n$t2 = neg $t1\nprint $t2\n"));
    CHECK_EQ(tac_of("let b = 1; b = b + 1;"), std::string("b = 1\nb = b + 1\n"));
    CHECK_EQ(tac_of("print 8 - 3 - 1;"), std::string("$t1 = 8 - 3\n$t2 = $t1 - 1\nprint $t2\n"));
    CHECK_EQ(tac_of("print 7;"), std::string("print 7\n"));
    CHECK_EQ(tac_of("print -5;"), std::string("$t1 = neg 5\nprint $t1\n"));
    // Temporary numbering continues across statements (never reused within a program).
    CHECK_EQ(tac_of("print 1 + 2; print 3 + 4;"),
             std::string("$t1 = 1 + 2\nprint $t1\n$t2 = 3 + 4\nprint $t2\n"));
    // Invalid programs never reach TAC.
    CHECK_EQ(tac_of("print x;"), std::string("INVALID"));
}

void test_deterministic() {
    const std::string src = "let a = 3; let b = a * (a + 1) - 2 / a; print b + -a;";
    std::string first = tac_of(src);
    for (int i = 0; i < 5; ++i) CHECK_EQ(tac_of(src), first);
    CHECK(first.find("$t") != std::string::npos);
}

void test_temp_naming() {
    TacProgram tac;
    CHECK(nova_test::compile_tac("let x = 1; print (x + 1) * 2;", tac));
    int temps = 0;
    for (const Instruction& i : tac.instructions)
        if (i.dst.is_temp()) ++temps;
    CHECK_EQ(temps, 2);
    // '$' cannot start a NOVA identifier, so temps cannot collide with user variables.
    CHECK(!lex("$t1").ok());
}

RunResult run(const std::string& src, bool use_tac = true) {
    auto program = nova_test::parse_program(src);
    CHECK_MSG(program != nullptr, "source must parse: " + src);
    if (!program) return RunResult();
    CHECK_MSG(analyze(*program).ok(), "source must be valid: " + src);
    return use_tac ? run_tac(generate_tac(*program)) : run_ast(*program);
}

std::string out_of(const std::string& src) {
    RunResult t = run(src, true);
    RunResult a = run(src, false);
    // TAC lowering must preserve behaviour exactly, including errors.
    CHECK_MSG(t.output == a.output, "ast and tac output agree: " + src);
    CHECK_MSG(t.ok == a.ok, "ast and tac status agree: " + src);
    if (!t.ok && !a.ok) {
        CHECK_MSG(t.error.location == a.error.location, "ast and tac error location agree: " + src);
        CHECK_MSG(t.error.message == a.error.message, "ast and tac error message agree: " + src);
    }
    return t.ok ? format_output(t.output) : format_output(t.output) + "ERR:" + t.error.message;
}

void test_execution() {
    CHECK_EQ(out_of(""), std::string(""));
    CHECK_EQ(out_of("print 42;"), std::string("42\n"));
    CHECK_EQ(out_of("let x = 10; let y = 20; let z = x * 2 + y; print z;"), std::string("40\n"));
    CHECK_EQ(out_of("print 2 + 3 * 4; print (2 + 3) * 4; print 8 - 3 - 1;"), std::string("14\n20\n4\n"));
    CHECK_EQ(out_of("let x = 1; x = x + 1; x = x * 10; print x;"), std::string("20\n"));
    CHECK_EQ(out_of("let x = 1; let y = x; x = 5; print y; print x;"), std::string("1\n5\n"));
    CHECK_EQ(out_of("print -(-3); print 2 * -3; print 2 - -3;"), std::string("3\n-6\n5\n"));
    CHECK_EQ(out_of("let a = 3; a = a * a - a; print a;"), std::string("6\n"));
}

void test_division_semantics() {
    // truncation toward zero
    CHECK_EQ(out_of("print 7 / 2; print -7 / 2; print 7 / -2; print -7 / -2;"), std::string("3\n-3\n-3\n3\n"));
    CHECK_EQ(out_of("print 0 / 5; print 1 / 2; print -1 / 2;"), std::string("0\n0\n0\n"));
    CHECK_EQ(out_of("print 8 / 4 / 2;"), std::string("1\n"));
    // division by zero is a runtime error, located at the '/' operator; earlier output is kept
    CHECK_EQ(out_of("print 1 / 0;"), std::string("ERR:runtime error: division by zero"));
    CHECK_EQ(out_of("let z = 0; print 5 / z;"), std::string("ERR:runtime error: division by zero"));
    CHECK_EQ(out_of("print 1; print 2 / 0; print 3;"), std::string("1\nERR:runtime error: division by zero"));
    CHECK_EQ(out_of("print 0 / 0;"), std::string("ERR:runtime error: division by zero"));
    CHECK_EQ(out_of("let z = 1 - 1; print 1 / z;"), std::string("ERR:runtime error: division by zero"));
    RunResult r = run("print 1; print 2/0; print 3;");
    CHECK(!r.ok);
    CHECK(r.error.location == (SourceLocation{1, 17}));
    CHECK_EQ(r.output.size(), size_t(1));
    // the first failing division stops execution (left operand evaluated first)
    CHECK_EQ(out_of("print (1 / 0) + (2 / 0);"), std::string("ERR:runtime error: division by zero"));
}

void test_overflow_semantics() {
    const std::string max = "9223372036854775807";
    // two's complement wrap-around
    CHECK_EQ(out_of("print " + max + " + 1;"), std::string("-9223372036854775808\n"));
    CHECK_EQ(out_of("let m = -" + max + " - 1; print m;"), std::string("-9223372036854775808\n"));
    CHECK_EQ(out_of("let m = -" + max + " - 1; print m - 1;"), std::string("9223372036854775807\n"));
    CHECK_EQ(out_of("print " + max + " * 2;"), std::string("-2\n"));
    CHECK_EQ(out_of("let m = -" + max + " - 1; print -m;"), std::string("-9223372036854775808\n"));
    CHECK_EQ(out_of("let m = -" + max + " - 1; print m * -1;"), std::string("-9223372036854775808\n"));
    // INT64_MIN / -1 is defined (wraps) rather than trapping
    CHECK_EQ(out_of("let m = -" + max + " - 1; print m / -1;"), std::string("-9223372036854775808\n"));
    CHECK_EQ(out_of("let m = -" + max + " - 1; print m / 1;"), std::string("-9223372036854775808\n"));
    CHECK_EQ(out_of("let m = -" + max + " - 1; print m / 2;"), std::string("-4611686018427387904\n"));
}

void test_runtime_error_message_format() {
    RunResult r = run("let x = 0;\nprint 1 / x;");
    CHECK(!r.ok);
    CHECK(r.error.location == (SourceLocation{2, 9}));
    CHECK_EQ(format_diagnostic("f.nova", r.error), std::string("f.nova:2:9: error: runtime error: division by zero"));
}

}  // namespace

int main() {
    test_golden_tac();
    test_deterministic();
    test_temp_naming();
    test_execution();
    test_division_semantics();
    test_overflow_semantics();
    test_runtime_error_message_format();
    return nova_test::finish("tac/interpreter");
}

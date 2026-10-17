#include <cstdint>
#include <string>
#include <vector>

#include "check.hpp"
#include "helpers.hpp"
#include "nova/interp.hpp"
#include "nova/optimizer.hpp"

using namespace nova;

namespace {

TacProgram tac_from(const std::string& src) {
    TacProgram tac;
    bool ok = nova_test::compile_tac(src, tac);
    CHECK_MSG(ok, "source must compile: " + src);
    return tac;
}

// Applies one pass once; returns the resulting TAC text and reports `changed`.
std::string apply(PassFn pass, const std::string& src, bool* changed = nullptr) {
    TacProgram tac = tac_from(src);
    bool c = pass(tac);
    if (changed) *changed = c;
    return print_tac(tac);
}

std::string full(const std::string& src) {
    return print_tac(optimize(tac_from(src), all_passes()).program);
}

// ---- constant folding ----------------------------------------------------------------------------------

void test_fold() {
    bool c = false;
    CHECK_EQ(apply(constant_fold, "print 2 * 3;", &c), std::string("$t1 = 6\nprint $t1\n"));
    CHECK(c);
    CHECK_EQ(apply(constant_fold, "print 7 - 10;"), std::string("$t1 = -3\nprint $t1\n"));
    CHECK_EQ(apply(constant_fold, "print 1 + 2;"), std::string("$t1 = 3\nprint $t1\n"));
    CHECK_EQ(apply(constant_fold, "print 7 / 2;"), std::string("$t1 = 3\nprint $t1\n"));
    CHECK_EQ(apply(constant_fold, "print -5;"), std::string("$t1 = -5\nprint $t1\n"));
    // wrapping arithmetic is folded with the same semantics as execution
    CHECK_EQ(apply(constant_fold, "print 9223372036854775807 + 1;"), std::string("$t1 = -9223372036854775808\nprint $t1\n"));
    CHECK_EQ(apply(constant_fold, "print 9223372036854775807 * 2;"), std::string("$t1 = -2\nprint $t1\n"));
    // negative cases: nothing to fold
    CHECK_EQ(apply(constant_fold, "let x = 1; x = x + 2;", &c), std::string("x = 1\nx = x + 2\n"));
    CHECK(!c);
    CHECK_EQ(apply(constant_fold, "let x = 4; print x;", &c), std::string("x = 4\nprint x\n"));
    CHECK(!c);
    // division by zero must NOT be folded (it must still fail at run time)
    CHECK_EQ(apply(constant_fold, "print 1 / 0;", &c), std::string("$t1 = 1 / 0\nprint $t1\n"));
    CHECK(!c);
    CHECK_EQ(apply(constant_fold, "print 0 / 0;", &c), std::string("$t1 = 0 / 0\nprint $t1\n"));
    CHECK(!c);
    // INT64_MIN / -1 folds to the wrapped value (no trap)
    TacProgram t = tac_from("let m = -9223372036854775807 - 1; print m / -1;");
    t = optimize(t, all_passes()).program;
    CHECK_EQ(print_tac(t), std::string("print -9223372036854775808\n"));
    // idempotence of a single pass once folded
    TacProgram t2 = tac_from("print 2 * 3;");
    constant_fold(t2);
    CHECK(!constant_fold(t2));
}

// ---- constant propagation ------------------------------------------------------------------------------

void test_propagate() {
    bool c = false;
    CHECK_EQ(apply(constant_propagate, "let x = 5; print x + 1;", &c), std::string("x = 5\n$t1 = 5 + 1\nprint $t1\n"));
    CHECK(c);
    // reassignment: the new value is used afterwards, never the stale one
    CHECK_EQ(apply(constant_propagate, "let x = 5; x = 6; print x;"), std::string("x = 5\nx = 6\nprint 6\n"));
    // after an assignment of a non-constant, the variable is unknown again
    CHECK_EQ(apply(constant_propagate, "let x = 5; x = x + 1; print x;"), std::string("x = 5\nx = 5 + 1\nprint x\n"));
    CHECK_EQ(apply(constant_propagate, "let x = 5; let y = 2; x = y * 3; print x;"),
             std::string("x = 5\ny = 2\nx = 2 * 3\nprint x\n"));
    // constants flow through copies
    CHECK_EQ(apply(constant_propagate, "let y = 1; let x = y; print x;"), std::string("y = 1\nx = 1\nprint 1\n"));
    // division by a known zero is still a division by zero (just with a literal divisor)
    CHECK_EQ(apply(constant_propagate, "let z = 0; print 1 / z;"), std::string("z = 0\n$t1 = 1 / 0\nprint $t1\n"));
    // negative: nothing known
    CHECK_EQ(apply(constant_propagate, "let x = 1 / 1; print x;", &c), std::string("x = 1 / 1\nprint x\n"));
    CHECK(!c);
    CHECK_EQ(apply(constant_propagate, "print 3;", &c), std::string("print 3\n"));
    CHECK(!c);
    // a temp defined by a constant copy is propagated too
    TacProgram t = tac_from("print 2 * 3;");
    constant_fold(t);
    CHECK(constant_propagate(t));
    CHECK_EQ(print_tac(t), std::string("$t1 = 6\nprint 6\n"));
}

// ---- algebraic simplification --------------------------------------------------------------------------

void test_simplify() {
    bool c = false;
    const std::string pre = "let x = 3; let y = 4; ";
    const std::string base = "x = 3\ny = 4\n";
    CHECK_EQ(apply(algebraic_simplify, pre + "print x + 0;", &c), std::string(base + "$t1 = x\nprint $t1\n"));
    CHECK(c);
    CHECK_EQ(apply(algebraic_simplify, pre + "print 0 + x;"), std::string(base + "$t1 = x\nprint $t1\n"));
    CHECK_EQ(apply(algebraic_simplify, pre + "print x - 0;"), std::string(base + "$t1 = x\nprint $t1\n"));
    CHECK_EQ(apply(algebraic_simplify, pre + "print x * 1;"), std::string(base + "$t1 = x\nprint $t1\n"));
    CHECK_EQ(apply(algebraic_simplify, pre + "print 1 * x;"), std::string(base + "$t1 = x\nprint $t1\n"));
    CHECK_EQ(apply(algebraic_simplify, pre + "print x / 1;"), std::string(base + "$t1 = x\nprint $t1\n"));
    CHECK_EQ(apply(algebraic_simplify, pre + "print x * 0;"), std::string(base + "$t1 = 0\nprint $t1\n"));
    CHECK_EQ(apply(algebraic_simplify, pre + "print 0 * x;"), std::string(base + "$t1 = 0\nprint $t1\n"));
    CHECK_EQ(apply(algebraic_simplify, pre + "print x - x;"), std::string(base + "$t1 = 0\nprint $t1\n"));
    CHECK_EQ(apply(algebraic_simplify, pre + "print 0 - x;"), std::string(base + "$t1 = neg x\nprint $t1\n"));
    // negative cases: these must be left alone
    for (const char* e : {"x + 1", "x - 1", "x * 2", "x / 2", "x - y", "x / x", "0 / x", "1 - x", "x / 0", "x + y"}) {
        std::string src = pre + "print " + e + ";";
        apply(algebraic_simplify, src, &c);
        CHECK_MSG(!c, std::string("must not simplify: ") + e);
    }
    // x / x is NOT simplified (x may be zero => runtime error); 0 / x likewise
    CHECK_EQ(apply(algebraic_simplify, pre + "print x / x;"), std::string(base + "$t1 = x / x\nprint $t1\n"));
    CHECK_EQ(apply(algebraic_simplify, pre + "print 0 / x;"), std::string(base + "$t1 = 0 / x\nprint $t1\n"));
    // x*0 is exact only because the operand was already evaluated; a failing division feeding it stays
    CHECK_EQ(apply(algebraic_simplify, "let z = 0; print (1 / z) * 0;"),
             std::string("z = 0\n$t1 = 1 / z\n$t2 = 0\nprint $t2\n"));
}

// ---- dead-code elimination -----------------------------------------------------------------------------

void test_dce() {
    bool c = false;
    CHECK_EQ(apply(eliminate_dead_code, "let x = 1; let y = 2; print y;", &c), std::string("y = 2\nprint y\n"));
    CHECK(c);
    // chains die in a single backward sweep
    CHECK_EQ(apply(eliminate_dead_code, "let a = 1; let b = a + 1; let c = b * 2; print 3;"), std::string("print 3\n"));
    // overwritten value is dead
    CHECK_EQ(apply(eliminate_dead_code, "let x = 1; x = 2; print x;"), std::string("x = 2\nprint x\n"));
    // used values stay
    CHECK_EQ(apply(eliminate_dead_code, "let x = 1; print x;", &c), std::string("x = 1\nprint x\n"));
    CHECK(!c);
    // print is never removed
    CHECK_EQ(apply(eliminate_dead_code, "print 1; print 2;", &c), std::string("print 1\nprint 2\n"));
    CHECK(!c);
    // read-modify-write keeps earlier definition alive
    CHECK_EQ(apply(eliminate_dead_code, "let x = 1; x = x + 1; print x;", &c), std::string("x = 1\nx = x + 1\nprint x\n"));
    CHECK(!c);
    // a dead division that can fail must stay: removing it would hide a runtime error
    CHECK_EQ(apply(eliminate_dead_code, "let z = 0; let u = 1 / z; print 5;"), std::string("z = 0\nu = 1 / z\nprint 5\n"));
    CHECK_EQ(apply(eliminate_dead_code, "let a = 1; let b = 2; let u = a / b; print 5;"),
             std::string("a = 1\nb = 2\nu = a / b\nprint 5\n"));
    CHECK_EQ(apply(eliminate_dead_code, "let u = 1 / 0; print 5;"), std::string("u = 1 / 0\nprint 5\n"));
    // ...but a division by a non-zero constant cannot fail and is removable
    CHECK_EQ(apply(eliminate_dead_code, "let a = 7; let u = a / 2; print 5;"), std::string("print 5\n"));
    // all-dead program becomes empty
    CHECK_EQ(apply(eliminate_dead_code, "let x = 1; let y = x + 1;"), std::string(""));
}

// ---- pipeline, fixed point, termination, determinism ------------------------------------------------

void test_pipeline() {
    CHECK_EQ(full("let x = 10; let y = 20; let z = x * 2 + y; print z;"), std::string("print 40\n"));
    CHECK_EQ(full("let x = 2; let y = x * 0 + 5; print y + 0;"), std::string("print 5\n"));
    CHECK_EQ(full("let x = 1; x = x + 1; x = x * 3; print x;"), std::string("print 6\n"));
    CHECK_EQ(full(""), std::string(""));
    // the failing division survives with the output printed before it
    CHECK_EQ(full("print 1; let z = 0; print 5 / z; print 2;"), std::string("print 1\n$t1 = 5 / 0\nprint $t1\nprint 2\n"));
    OptimizeResult r = optimize(tac_from("let x = 1 + 2; print x;"), all_passes());
    CHECK(r.converged);
    CHECK(r.iterations >= 2);  // at least one productive sweep plus the confirming sweep
}

void test_fixed_point_and_termination() {
    // Idempotence: optimizing already-optimized code changes nothing.
    const char* srcs[] = {"let a = 1; let b = a + 2; let c = b * b - a; print c / 2; print b;",
                          "let z = 0; print 1; print 3 / z;",
                          "let q = 5; q = q - q; print q + 1 * 4;"};
    for (const char* s : srcs) {
        OptimizeResult once = optimize(tac_from(s), all_passes());
        OptimizeResult twice = optimize(once.program, all_passes());
        CHECK_MSG(once.converged && twice.converged, "converges");
        CHECK_MSG(print_tac(once.program) == print_tac(twice.program), std::string("idempotent: ") + s);
        CHECK_MSG(twice.iterations == 1, "second run needs only the confirming sweep");
    }
    // Long dependency chain needs many sweeps but terminates well under the cap.
    std::string chain = "let v0 = 1;";
    for (int i = 1; i <= 500; ++i) chain += " let v" + std::to_string(i) + " = v" + std::to_string(i - 1) + " + 1;";
    chain += " print v500;";
    OptimizeResult r = optimize(tac_from(chain), all_passes());
    CHECK(r.converged);
    CHECK(r.iterations < kMaxOptimizeIterations);
    CHECK_EQ(print_tac(r.program), std::string("print 501\n"));
    // A deliberately non-terminating "pass" hits the cap and reports converged = false.
    std::vector<PassInfo> bad = {{"flip", [](TacProgram& t) {
                                      t.instructions.push_back(Instruction());
                                      t.instructions.pop_back();
                                      return true;
                                  }}};
    OptimizeResult b = optimize(TacProgram(), bad);
    CHECK(!b.converged);
    CHECK_EQ(b.iterations, kMaxOptimizeIterations);
}

void test_determinism_and_steps() {
    const std::string src = "let a = 3; let b = a * (a + 1) - 2 / a; print b + -a; print a * 1;";
    std::string first = full(src);
    for (int i = 0; i < 5; ++i) CHECK_EQ(full(src), first);
    OptimizeResult r = optimize(tac_from(src), all_passes(), true);
    CHECK_EQ(r.steps.size(), size_t(r.iterations) * all_passes().size());
    CHECK_EQ(r.steps.front().pass, std::string("fold"));
    CHECK_EQ(r.steps.back().after, print_tac(r.program));
    CHECK(find_pass("dce") != nullptr);
    CHECK(find_pass("nonsense") == nullptr);
}

// ---- differential tests ---------------------------------------------------------------------------------

struct Rng {
    std::uint32_t s;
    std::uint32_t next() {
        s = s * 1664525u + 1013904223u;
        return s >> 8;
    }
    int below(int n) { return static_cast<int>(next() % static_cast<std::uint32_t>(n)); }
};

std::string gen_const(Rng& r) {
    static const char* cs[] = {"0", "1", "2", "3", "7", "100", "(-1)", "(-5)", "9223372036854775807", "0", "1"};
    return cs[r.below(11)];
}

std::string gen_expr(Rng& r, const std::vector<std::string>& vars, int depth) {
    int k = r.below(depth <= 0 ? 2 : 6);
    if (k == 0 || depth <= 0) return gen_const(r);
    if (k == 1) return vars.empty() ? gen_const(r) : vars[r.below(static_cast<int>(vars.size()))];
    if (k == 2) return "(-" + gen_expr(r, vars, depth - 1) + ")";
    static const char* ops[] = {"+", "-", "*", "/"};
    return "(" + gen_expr(r, vars, depth - 1) + " " + ops[r.below(4)] + " " + gen_expr(r, vars, depth - 1) + ")";
}

std::string gen_program(Rng& r) {
    std::vector<std::string> vars;
    std::string src;
    int n = 1 + r.below(14);
    for (int i = 0; i < n; ++i) {
        int k = vars.empty() ? 0 : r.below(5);
        if (k <= 1) {
            std::string name = "v" + std::to_string(vars.size());
            src += "let " + name + " = " + gen_expr(r, vars, 3) + ";\n";
            vars.push_back(name);
        } else if (k == 2) {
            src += vars[r.below(static_cast<int>(vars.size()))] + " = " + gen_expr(r, vars, 3) + ";\n";
        } else {
            src += "print " + gen_expr(r, vars, 3) + ";\n";
        }
    }
    return src;
}

int count_prints(const TacProgram& t) {
    int n = 0;
    for (const Instruction& i : t.instructions)
        if (i.op == Opcode::Print) ++n;
    return n;
}

bool same_behaviour(const RunResult& a, const RunResult& b) {
    if (a.output != b.output || a.ok != b.ok) return false;
    if (!a.ok) return a.error.message == b.error.message && a.error.location == b.error.location;
    return true;
}

void test_differential() {
    Rng rng{20261014u};
    int programs = 0, with_errors = 0, shrunk = 0;
    const std::vector<PassInfo>& passes = all_passes();
    std::vector<PassInfo> reversed(passes.rbegin(), passes.rend());
    for (int n = 0; n < 4000; ++n) {
        std::string src = gen_program(rng);
        auto program = nova_test::parse_program(src);
        if (!program) {
            CHECK_MSG(false, "generated program must parse: " + src);
            continue;
        }
        if (!analyze(*program).ok()) {
            CHECK_MSG(false, "generated program must be valid: " + src);
            continue;
        }
        ++programs;
        TacProgram tac = generate_tac(*program);
        RunResult base = run_tac(tac);
        if (!base.ok) ++with_errors;
        CHECK_MSG(same_behaviour(base, run_ast(*program)), "ast/tac agree on:\n" + src);

        // each pass alone, applied once
        for (const PassInfo& p : passes) {
            TacProgram t = tac;
            p.run(t);
            CHECK_MSG(same_behaviour(base, run_tac(t)), std::string("pass ") + p.name + " preserves behaviour on:\n" + src);
            CHECK_MSG(count_prints(t) == count_prints(tac), std::string("pass ") + p.name + " keeps every print on:\n" + src);
        }
        // full pipeline and a different order, to a fixed point
        const std::vector<PassInfo>* orders[] = {&passes, &reversed};
        for (const std::vector<PassInfo>* order : orders) {
            OptimizeResult o = optimize(tac, *order);
            CHECK_MSG(o.converged, "converges on:\n" + src);
            CHECK_MSG(same_behaviour(base, run_tac(o.program)), "pipeline preserves behaviour on:\n" + src);
            CHECK_MSG(count_prints(o.program) == count_prints(tac), "pipeline keeps every print on:\n" + src);
            if (order == &passes && o.program.instructions.size() < tac.instructions.size()) ++shrunk;
        }
    }
    CHECK_EQ(programs, 4000);
    // Sanity on the corpus itself: it must exercise both runtime errors and real optimization.
    CHECK_MSG(with_errors > 100, "corpus contains division-by-zero programs");
    CHECK_MSG(shrunk > 1000, "corpus is actually optimized");
    std::cout << "differential corpus: " << programs << " programs, " << with_errors << " with runtime errors, "
              << shrunk << " shrunk by the pipeline\n";
}

}  // namespace

int main() {
    test_fold();
    test_propagate();
    test_simplify();
    test_dce();
    test_pipeline();
    test_fixed_point_and_termination();
    test_determinism_and_steps();
    test_differential();
    return nova_test::finish("optimizer");
}

#include <string>
#include <vector>

#include "check.hpp"
#include "nova/ast.hpp"
#include "nova/lexer.hpp"
#include "nova/parser.hpp"

using namespace nova;

namespace {

// Structural rendering built by walking the AST fields directly (independent of dump_ast).
std::string sx(const Expr& e) {
    switch (e.kind) {
        case Expr::Kind::Integer: return std::to_string(static_cast<const IntegerExpr&>(e).value);
        case Expr::Kind::Variable: return static_cast<const VariableExpr&>(e).name;
        case Expr::Kind::Unary: {
            const auto& u = static_cast<const UnaryExpr&>(e);
            return std::string("(neg ") + sx(*u.operand) + ")";
        }
        case Expr::Kind::Binary: {
            const auto& b = static_cast<const BinaryExpr&>(e);
            return std::string("(") + op_symbol(b.op) + " " + sx(*b.lhs) + " " + sx(*b.rhs) + ")";
        }
    }
    return "?";
}

std::string sx(const Stmt& s) {
    switch (s.kind) {
        case Stmt::Kind::Let: {
            const auto& l = static_cast<const LetStmt&>(s);
            return "let " + l.name + " " + sx(*l.value);
        }
        case Stmt::Kind::Assign: {
            const auto& a = static_cast<const AssignStmt&>(s);
            return "set " + a.name + " " + sx(*a.value);
        }
        case Stmt::Kind::Print: return "print " + sx(*static_cast<const PrintStmt&>(s).value);
    }
    return "?";
}

ParseResult parse_src(const std::string& src) {
    LexResult l = lex(src);
    CHECK_MSG(l.ok(), "test source must lex cleanly: " + src);
    return parse(l.tokens);
}

// Statements joined by "; ".
std::string tree(const std::string& src) {
    ParseResult r = parse_src(src);
    if (!r.ok()) return "ERROR " + (r.diagnostics.empty() ? std::string("?") : r.diagnostics[0].message);
    std::string out;
    for (const StmtPtr& s : r.program->statements) {
        if (!out.empty()) out += "; ";
        out += sx(*s);
    }
    return out;
}

void expect_error(const std::string& src, int line, int col, const std::string& msg) {
    ParseResult r = parse_src(src);
    CHECK_MSG(!r.ok(), "expected syntax error for: " + src);
    CHECK_MSG(r.program == nullptr, "no program on error: " + src);
    CHECK_MSG(r.diagnostics.size() == 1, "exactly one diagnostic: " + src);
    if (r.diagnostics.size() != 1) return;
    CHECK_MSG(r.diagnostics[0].location == (SourceLocation{line, col}),
              "location for '" + src + "': got " + std::to_string(r.diagnostics[0].location.line) + ":" +
                  std::to_string(r.diagnostics[0].location.column));
    CHECK_MSG(r.diagnostics[0].message == msg, "message for '" + src + "': " + r.diagnostics[0].message);
}

void test_productions() {
    CHECK_EQ(tree(""), std::string(""));
    CHECK_EQ(tree("let x = 1;"), std::string("let x 1"));
    CHECK_EQ(tree("x = 2;"), std::string("set x 2"));
    CHECK_EQ(tree("print x;"), std::string("print x"));
    CHECK_EQ(tree("let x = 1; x = x + 1; print x;"), std::string("let x 1; set x (+ x 1); print x"));
    CHECK_EQ(tree("let x = 1;\nlet y = 2;\nprint x;"), std::string("let x 1; let y 2; print x"));
    // each primary
    CHECK_EQ(tree("print 7;"), std::string("print 7"));
    CHECK_EQ(tree("print abc;"), std::string("print abc"));
    CHECK_EQ(tree("print (7);"), std::string("print 7"));
    // each operator
    CHECK_EQ(tree("print 1+2;"), std::string("print (+ 1 2)"));
    CHECK_EQ(tree("print 1-2;"), std::string("print (- 1 2)"));
    CHECK_EQ(tree("print 1*2;"), std::string("print (* 1 2)"));
    CHECK_EQ(tree("print 1/2;"), std::string("print (/ 1 2)"));
    CHECK_EQ(tree("print -1;"), std::string("print (neg 1)"));
    CHECK_EQ(tree("print -x;"), std::string("print (neg x)"));
    // keyword-prefixed identifiers are ordinary names
    CHECK_EQ(tree("letx = 1; printer = 2;"), std::string("set letx 1; set printer 2"));
}

void test_precedence_and_associativity() {
    CHECK_EQ(tree("print 2 + 3 * 4;"), std::string("print (+ 2 (* 3 4))"));
    CHECK_EQ(tree("print 2 * 3 + 4;"), std::string("print (+ (* 2 3) 4)"));
    CHECK_EQ(tree("print 8 - 3 - 1;"), std::string("print (- (- 8 3) 1)"));
    CHECK_EQ(tree("print 8 / 4 / 2;"), std::string("print (/ (/ 8 4) 2)"));
    CHECK_EQ(tree("print 1 - 2 + 3;"), std::string("print (+ (- 1 2) 3)"));
    CHECK_EQ(tree("print 2 * 3 / 4;"), std::string("print (/ (* 2 3) 4)"));
    CHECK_EQ(tree("print (2 + 3) * 4;"), std::string("print (* (+ 2 3) 4)"));
    CHECK_EQ(tree("print 8 - (3 - 1);"), std::string("print (- 8 (- 3 1))"));
    CHECK_EQ(tree("print ((1));"), std::string("print 1"));
    // unary binds tighter than binary operators
    CHECK_EQ(tree("print -2 * 3;"), std::string("print (* (neg 2) 3)"));
    CHECK_EQ(tree("print 2 * -3;"), std::string("print (* 2 (neg 3))"));
    CHECK_EQ(tree("print 2 - -3;"), std::string("print (- 2 (neg 3))"));
    CHECK_EQ(tree("print -(2 + 3);"), std::string("print (neg (+ 2 3))"));
    CHECK_EQ(tree("print -(-3);"), std::string("print (neg (neg 3))"));
    CHECK_EQ(tree("let z = x * 2 + y;"), std::string("let z (+ (* x 2) y)"));
}

void test_node_details() {
    ParseResult r = parse_src("let x = 10;\n  y = 2;\nprint x;");
    CHECK(r.ok());
    CHECK_EQ(r.program->statements.size(), size_t(3));
    const auto& let = static_cast<const LetStmt&>(*r.program->statements[0]);
    CHECK(let.location == (SourceLocation{1, 1}));
    CHECK(let.name_location == (SourceLocation{1, 5}));
    CHECK(let.value->location == (SourceLocation{1, 9}));
    CHECK(static_cast<const IntegerExpr&>(*let.value).value == 10);
    CHECK(r.program->statements[1]->location == (SourceLocation{2, 3}));
    CHECK(r.program->statements[2]->location == (SourceLocation{3, 1}));
    // Binary node location is the operator.
    r = parse_src("print 1 + 2;");
    const auto& p = static_cast<const PrintStmt&>(*r.program->statements[0]);
    CHECK(p.value->location == (SourceLocation{1, 9}));
    // Largest literal survives conversion.
    r = parse_src("print 9223372036854775807;");
    CHECK_EQ(static_cast<const IntegerExpr&>(*static_cast<const PrintStmt&>(*r.program->statements[0]).value).value,
             std::int64_t(9223372036854775807LL));
}

void test_dump_format() {
    ParseResult r = parse_src("let x = 2 + 3 * 4;\nx = -x;\nprint (x - 1) / 2;\n");
    CHECK(r.ok());
    const std::string expected =
        "Program\n"
        "  Let x\n"
        "    Binary +\n"
        "      Integer 2\n"
        "      Binary *\n"
        "        Integer 3\n"
        "        Integer 4\n"
        "  Assign x\n"
        "    Unary -\n"
        "      Variable x\n"
        "  Print\n"
        "    Binary /\n"
        "      Binary -\n"
        "        Variable x\n"
        "        Integer 1\n"
        "      Integer 2\n";
    CHECK_EQ(dump_ast(*r.program), expected);
    CHECK_EQ(dump_ast(*parse_src("").program), std::string("Program\n"));
    // Deterministic: dumping twice gives the same text.
    CHECK_EQ(dump_ast(*r.program), dump_ast(*r.program));
}

void test_syntax_errors() {
    // missing semicolon
    expect_error("let x = 1", 1, 10, "expected ';', found end of file");
    expect_error("let x = 1\nprint x;", 2, 1, "expected ';', found keyword 'print'");
    expect_error("let x = 1 2;", 1, 11, "expected ';', found integer '2'");
    expect_error("print x", 1, 8, "expected ';', found end of file");
    // missing operand
    expect_error("let x = ;", 1, 9, "expected expression, found ';'");
    expect_error("let x = 2 +;", 1, 12, "expected expression, found ';'");
    expect_error("print * 3;", 1, 7, "expected expression, found '*'");
    expect_error("print 1 + * 2;", 1, 11, "expected expression, found '*'");
    expect_error("print;", 1, 6, "expected expression, found ';'");
    expect_error("print --3;", 1, 8, "expected expression, found '-'");  // grammar: at most one minus
    expect_error("print -;", 1, 8, "expected expression, found ';'");
    // parentheses
    expect_error("print (1 + 2;", 1, 13, "expected ')', found ';'");
    expect_error("print 1);", 1, 8, "expected ';', found ')'");
    expect_error("print ();", 1, 8, "expected expression, found ')'");
    expect_error("print (1 + 2", 1, 13, "expected ')', found end of file");
    expect_error("print ((1);", 1, 11, "expected ')', found ';'");
    // unexpected EOF
    expect_error("let", 1, 4, "expected identifier after 'let', found end of file");
    expect_error("let x", 1, 6, "expected '=' after variable name, found end of file");
    expect_error("let x =", 1, 8, "expected expression, found end of file");
    expect_error("x", 1, 2, "expected '=' after identifier 'x', found end of file");
    expect_error("print", 1, 6, "expected expression, found end of file");
    // statement-level errors
    expect_error("1;", 1, 1, "expected statement, found integer '1'");
    expect_error("(1);", 1, 1, "expected statement, found '('");
    expect_error(";", 1, 1, "expected statement, found ';'");
    expect_error("= 1;", 1, 1, "expected statement, found '='");
    expect_error("let = 1;", 1, 5, "expected identifier after 'let', found '='");
    expect_error("let let = 1;", 1, 5, "expected identifier after 'let', found keyword 'let'");
    expect_error("let 5 = 1;", 1, 5, "expected identifier after 'let', found integer '5'");
    expect_error("let x 1;", 1, 7, "expected '=' after variable name, found integer '1'");
    expect_error("x 1;", 1, 3, "expected '=' after identifier 'x', found integer '1'");
    expect_error("x + 1;", 1, 3, "expected '=' after identifier 'x', found '+'");
    expect_error("print x; y", 1, 11, "expected '=' after identifier 'y', found end of file");
    expect_error("let x = 1;\n\n  let y = ;", 3, 11, "expected expression, found ';'");
}

void test_invalid_token_stream_handling() {
    // The parser is only fed clean token streams (the CLI stops at lexical errors) but must not
    // crash on a degenerate stream.
    ParseResult r = parse(std::vector<Token>{});
    CHECK(!r.ok());
    CHECK_EQ(r.diagnostics.size(), size_t(1));
    std::vector<Token> no_eof = {Token{TokenKind::Integer, "1", {1, 1}, {1, 2}}};
    CHECK(!parse(no_eof).ok());
    // A lexically invalid source produces lexer diagnostics, which the CLI reports instead of parsing.
    CHECK(!lex("let x = 1 @ 2;").ok());
}

void test_depth_limit_and_no_crash() {
    // Deeply nested parentheses: rejected with a diagnostic, not a crash.
    for (int n : {10, 999, 1000, 1001, 100000}) {
        std::string src = "print " + std::string(n, '(') + "1" + std::string(n, ')') + ";";
        ParseResult r = parse_src(src);
        if (n <= kMaxExpressionDepth) {
            CHECK_MSG(r.ok(), "nesting " + std::to_string(n) + " accepted");
        } else {
            CHECK_MSG(!r.ok(), "nesting " + std::to_string(n) + " rejected");
            CHECK_MSG(r.diagnostics.size() == 1 &&
                          r.diagnostics[0].message.find("nested too deeply") != std::string::npos,
                      "too-deep diagnostic");
        }
    }
    // Long operator chain: depth grows with each operator.
    auto chain = [](int terms) {
        std::string s = "print 1";
        for (int i = 1; i < terms; ++i) s += "+1";
        return s + ";";
    };
    CHECK(parse_src(chain(900)).ok());
    CHECK(!parse_src(chain(1500)).ok());
    CHECK(!parse_src(chain(200000)).ok());
    // Many statements are fine.
    std::string many;
    for (int i = 0; i < 20000; ++i) many += "x = 1;\n";
    CHECK(parse_src(many).ok());
    // Unary-minus spam is simply a syntax error.
    CHECK(!parse_src("print " + std::string(100000, '-') + "1;").ok());
}

// Every prefix of a valid program, and many pseudo-random token soups, must terminate with either a
// program or exactly one diagnostic.
void test_malformed_input_robustness() {
    const std::string valid = "let x = (1 + 2) * -3;\nx = x / 2 - 1;\nprint (x);\n";
    for (size_t n = 0; n <= valid.size(); ++n) {
        ParseResult r = parse_src(valid.substr(0, n));
        CHECK_MSG(r.ok() ? r.diagnostics.empty() : (r.diagnostics.size() == 1 && r.program == nullptr),
                  "prefix result is a program or exactly one diagnostic");
    }
    const char* pieces[] = {"let", "print", "x", "y", "1", "42", "=", "+", "-", "*", "/", "(", ")", ";"};
    unsigned state = 12345;
    for (int i = 0; i < 5000; ++i) {
        std::string src;
        state = state * 1664525u + 1013904223u;
        int len = static_cast<int>((state >> 16) % 24);
        for (int j = 0; j < len; ++j) {
            state = state * 1664525u + 1013904223u;
            src += pieces[(state >> 16) % 14];
            src += ' ';
        }
        ParseResult r = parse_src(src);
        CHECK_MSG(r.ok() ? r.diagnostics.empty() : (r.diagnostics.size() == 1 && r.program == nullptr),
                  "random soup result well-formed: " + src);
    }
}

}  // namespace

int main() {
    test_productions();
    test_precedence_and_associativity();
    test_node_details();
    test_dump_format();
    test_syntax_errors();
    test_invalid_token_stream_handling();
    test_depth_limit_and_no_crash();
    test_malformed_input_robustness();
    return nova_test::finish("parser");
}

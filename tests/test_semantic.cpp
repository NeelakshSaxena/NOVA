#include <string>
#include <vector>

#include "check.hpp"
#include "helpers.hpp"
#include "nova/semantic.hpp"

using namespace nova;

namespace {

SemanticResult check_src(const std::string& src) {
    auto program = nova_test::parse_program(src);
    CHECK_MSG(program != nullptr, "test source must parse: " + src);
    if (!program) return SemanticResult();
    return analyze(*program);
}

struct Exp {
    int line, col;
    const char* msg;
};

void expect(const std::string& src, const std::vector<Exp>& exp) {
    SemanticResult r = check_src(src);
    CHECK_MSG(r.diagnostics.size() == exp.size(), "diagnostic count for: " + src);
    for (size_t i = 0; i < exp.size() && i < r.diagnostics.size(); ++i) {
        const Diagnostic& d = r.diagnostics[i];
        CHECK_MSG(d.location == (SourceLocation{exp[i].line, exp[i].col}),
                  "location #" + std::to_string(i) + " for '" + src + "': " + std::to_string(d.location.line) + ":" +
                      std::to_string(d.location.column));
        CHECK_MSG(d.message == exp[i].msg, "message #" + std::to_string(i) + " for '" + src + "': " + d.message);
    }
}

void test_valid() {
    CHECK(check_src("").ok());
    CHECK(check_src("let x = 1;").ok());
    CHECK(check_src("let x = 1; x = 2; print x;").ok());
    CHECK(check_src("let x = 1; let y = x + 1; y = x * y; print (x - y) / 2;").ok());
    CHECK(check_src("let x = 1; x = x + 1;").ok());  // right-hand side may use the target itself
    SemanticResult r = check_src("let a = 1; let b = 2; let c = 3;");
    CHECK(r.ok());
    CHECK_EQ(r.symbols.size(), size_t(3));
    CHECK(r.symbols.contains("b"));
    CHECK(!r.symbols.contains("d"));
    CHECK(r.symbols.declared_at("b") == (SourceLocation{1, 16}));
}

void test_undeclared_use() {
    expect("print x;", {{1, 7, "use of undeclared variable 'x'"}});
    expect("let y = x + 1;", {{1, 9, "use of undeclared variable 'x'"}});
    expect("let x = 1;\nprint y;", {{2, 7, "use of undeclared variable 'y'"}});
    // use before declaration (declared later in the file)
    expect("print x; let x = 1;", {{1, 7, "use of undeclared variable 'x'"}});
    // the initializer is checked before the name is declared
    expect("let x = x;", {{1, 9, "use of undeclared variable 'x'"}});
    expect("let x = -x;", {{1, 10, "use of undeclared variable 'x'"}});
    // each undeclared occurrence is reported
    expect("print a + a;", {{1, 7, "use of undeclared variable 'a'"}, {1, 11, "use of undeclared variable 'a'"}});
}

void test_duplicate_declaration() {
    expect("let x = 1; let x = 2;", {{1, 16, "variable 'x' is already declared (first declared at 1:5)"}});
    expect("let x = 1;\nlet y = 2;\nlet x = 3;",
           {{3, 5, "variable 'x' is already declared (first declared at 1:5)"}});
    // The first declaration is kept, so later uses are fine.
    expect("let x = 1; let x = 2; print x;", {{1, 16, "variable 'x' is already declared (first declared at 1:5)"}});
    // Duplicate with an initializer referencing itself: both problems reported.
    expect("let x = 1; let x = x;", {{1, 16, "variable 'x' is already declared (first declared at 1:5)"}});
}

void test_assignment_rules() {
    expect("y = 1;", {{1, 1, "assignment to undeclared variable 'y'"}});
    expect("let x = 1;\n  z = x;", {{2, 3, "assignment to undeclared variable 'z'"}});
    expect("x = 1; let x = 2;", {{1, 1, "assignment to undeclared variable 'x'"}});
    // an undeclared name on the right-hand side is reported before the assignment target
    expect("a = b;", {{1, 5, "use of undeclared variable 'b'"}, {1, 1, "assignment to undeclared variable 'a'"}});
}

void test_multiple_errors_reported() {
    expect("print a;\nb = 1;\nlet c = d;",
           {{1, 7, "use of undeclared variable 'a'"},
            {2, 1, "assignment to undeclared variable 'b'"},
            {3, 9, "use of undeclared variable 'd'"}});
}

}  // namespace

int main() {
    test_valid();
    test_undeclared_use();
    test_duplicate_declaration();
    test_assignment_rules();
    test_multiple_errors_reported();
    return nova_test::finish("semantic");
}

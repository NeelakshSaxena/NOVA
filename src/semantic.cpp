#include "nova/semantic.hpp"

namespace nova {

bool SymbolTable::declare(const std::string& name, SourceLocation where) {
    return symbols_.emplace(name, where).second;
}

namespace {

std::string at(SourceLocation l) { return std::to_string(l.line) + ":" + std::to_string(l.column); }

class Analyzer {
public:
    SemanticResult run(const Program& program) {
        for (const StmtPtr& s : program.statements) statement(*s);
        return std::move(result_);
    }

private:
    SemanticResult result_;

    void error(SourceLocation where, const std::string& msg) { result_.diagnostics.push_back({where, msg}); }

    void statement(const Stmt& s) {
        switch (s.kind) {
            case Stmt::Kind::Let: {
                const auto& l = static_cast<const LetStmt&>(s);
                // The initializer is checked first, so `let x = x;` uses an undeclared x.
                expression(*l.value);
                if (!result_.symbols.declare(l.name, l.name_location)) {
                    error(l.name_location, "variable '" + l.name + "' is already declared (first declared at " +
                                               at(result_.symbols.declared_at(l.name)) + ")");
                }
                break;
            }
            case Stmt::Kind::Assign: {
                const auto& a = static_cast<const AssignStmt&>(s);
                expression(*a.value);
                if (!result_.symbols.contains(a.name))
                    error(a.location, "assignment to undeclared variable '" + a.name + "'");
                break;
            }
            case Stmt::Kind::Print:
                expression(*static_cast<const PrintStmt&>(s).value);
                break;
        }
    }

    void expression(const Expr& e) {
        switch (e.kind) {
            case Expr::Kind::Integer: break;
            case Expr::Kind::Variable: {
                const auto& v = static_cast<const VariableExpr&>(e);
                if (!result_.symbols.contains(v.name)) error(v.location, "use of undeclared variable '" + v.name + "'");
                break;
            }
            case Expr::Kind::Unary: expression(*static_cast<const UnaryExpr&>(e).operand); break;
            case Expr::Kind::Binary:
                expression(*static_cast<const BinaryExpr&>(e).lhs);
                expression(*static_cast<const BinaryExpr&>(e).rhs);
                break;
        }
    }
};

}  // namespace

SemanticResult analyze(const Program& program) { return Analyzer().run(program); }

}  // namespace nova

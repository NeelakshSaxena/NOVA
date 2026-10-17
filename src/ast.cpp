#include "nova/ast.hpp"

#include <sstream>

namespace nova {

const char* op_symbol(UnaryOp op) {
    switch (op) {
        case UnaryOp::Negate: return "-";
    }
    return "?";
}

const char* op_symbol(BinaryOp op) {
    switch (op) {
        case BinaryOp::Add: return "+";
        case BinaryOp::Sub: return "-";
        case BinaryOp::Mul: return "*";
        case BinaryOp::Div: return "/";
    }
    return "?";
}

namespace {

void indent(std::ostream& os, int level) {
    for (int i = 0; i < level; ++i) os << "  ";
}

void dump_expr(std::ostream& os, const Expr& e, int level) {
    indent(os, level);
    switch (e.kind) {
        case Expr::Kind::Integer:
            os << "Integer " << static_cast<const IntegerExpr&>(e).value << "\n";
            break;
        case Expr::Kind::Variable:
            os << "Variable " << static_cast<const VariableExpr&>(e).name << "\n";
            break;
        case Expr::Kind::Unary: {
            const auto& u = static_cast<const UnaryExpr&>(e);
            os << "Unary " << op_symbol(u.op) << "\n";
            dump_expr(os, *u.operand, level + 1);
            break;
        }
        case Expr::Kind::Binary: {
            const auto& b = static_cast<const BinaryExpr&>(e);
            os << "Binary " << op_symbol(b.op) << "\n";
            dump_expr(os, *b.lhs, level + 1);
            dump_expr(os, *b.rhs, level + 1);
            break;
        }
    }
}

void dump_stmt(std::ostream& os, const Stmt& s, int level) {
    indent(os, level);
    switch (s.kind) {
        case Stmt::Kind::Let: {
            const auto& l = static_cast<const LetStmt&>(s);
            os << "Let " << l.name << "\n";
            dump_expr(os, *l.value, level + 1);
            break;
        }
        case Stmt::Kind::Assign: {
            const auto& a = static_cast<const AssignStmt&>(s);
            os << "Assign " << a.name << "\n";
            dump_expr(os, *a.value, level + 1);
            break;
        }
        case Stmt::Kind::Print:
            os << "Print\n";
            dump_expr(os, *static_cast<const PrintStmt&>(s).value, level + 1);
            break;
    }
}

}  // namespace

std::string dump_ast(const Program& program) {
    std::ostringstream os;
    os << "Program\n";
    for (const StmtPtr& s : program.statements) dump_stmt(os, *s, 1);
    return os.str();
}

}  // namespace nova

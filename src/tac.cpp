#include "nova/tac.hpp"

#include <sstream>

namespace nova {

namespace {

Opcode to_opcode(BinaryOp op) {
    switch (op) {
        case BinaryOp::Add: return Opcode::Add;
        case BinaryOp::Sub: return Opcode::Sub;
        case BinaryOp::Mul: return Opcode::Mul;
        case BinaryOp::Div: return Opcode::Div;
    }
    return Opcode::Add;
}

class Generator {
public:
    TacProgram run(const Program& program) {
        for (const StmtPtr& s : program.statements) statement(*s);
        return std::move(tac_);
    }

private:
    TacProgram tac_;
    int next_temp_ = 1;

    Operand fresh_temp() { return Operand::variable("$t" + std::to_string(next_temp_++)); }

    void emit(Opcode op, Operand dst, Operand a, Operand b, SourceLocation loc) {
        Instruction i;
        i.op = op;
        i.dst = std::move(dst);
        i.a = std::move(a);
        i.b = std::move(b);
        i.loc = loc;
        tac_.instructions.push_back(std::move(i));
    }

    void statement(const Stmt& s) {
        switch (s.kind) {
            case Stmt::Kind::Let: {
                const auto& l = static_cast<const LetStmt&>(s);
                assign(Operand::variable(l.name), *l.value);
                break;
            }
            case Stmt::Kind::Assign: {
                const auto& a = static_cast<const AssignStmt&>(s);
                assign(Operand::variable(a.name), *a.value);
                break;
            }
            case Stmt::Kind::Print: {
                const auto& p = static_cast<const PrintStmt&>(s);
                Operand v = expression(*p.value, nullptr);
                emit(Opcode::Print, Operand(), v, Operand(), p.location);
                break;
            }
        }
    }

    // dst = expr. Operations write directly into dst; a plain operand becomes a Copy.
    void assign(const Operand& dst, const Expr& e) {
        Operand v = expression(e, &dst);
        // Operations already wrote to dst; a leaf (constant/variable) needs an explicit copy.
        if (e.kind == Expr::Kind::Variable || e.kind == Expr::Kind::Integer)
            emit(Opcode::Copy, dst, v, Operand(), e.location);
    }

    // Returns the operand holding the value of `e`. If `dst` is non-null and `e` is an operation, the
    // result is written to *dst; otherwise to a fresh temporary.
    Operand expression(const Expr& e, const Operand* dst) {
        switch (e.kind) {
            case Expr::Kind::Integer: return Operand::constant(static_cast<const IntegerExpr&>(e).value);
            case Expr::Kind::Variable: return Operand::variable(static_cast<const VariableExpr&>(e).name);
            case Expr::Kind::Unary: {
                const auto& u = static_cast<const UnaryExpr&>(e);
                Operand a = expression(*u.operand, nullptr);
                Operand out = dst ? *dst : fresh_temp();
                emit(Opcode::Neg, out, a, Operand(), u.location);
                return out;
            }
            case Expr::Kind::Binary: {
                const auto& b = static_cast<const BinaryExpr&>(e);
                Operand l = expression(*b.lhs, nullptr);
                Operand r = expression(*b.rhs, nullptr);
                Operand out = dst ? *dst : fresh_temp();
                emit(to_opcode(b.op), out, l, r, b.location);
                return out;
            }
        }
        return Operand();
    }
};

const char* binary_symbol(Opcode op) {
    switch (op) {
        case Opcode::Add: return "+";
        case Opcode::Sub: return "-";
        case Opcode::Mul: return "*";
        case Opcode::Div: return "/";
        default: return "?";
    }
}

}  // namespace

TacProgram generate_tac(const Program& program) { return Generator().run(program); }

std::string print_operand(const Operand& o) { return o.is_const() ? std::to_string(o.value) : o.name; }

std::string print_instruction(const Instruction& i) {
    switch (i.op) {
        case Opcode::Copy: return print_operand(i.dst) + " = " + print_operand(i.a);
        case Opcode::Neg: return print_operand(i.dst) + " = neg " + print_operand(i.a);
        case Opcode::Print: return "print " + print_operand(i.a);
        default:
            return print_operand(i.dst) + " = " + print_operand(i.a) + " " + binary_symbol(i.op) + " " +
                   print_operand(i.b);
    }
}

std::string print_tac(const TacProgram& tac) {
    std::ostringstream os;
    for (const Instruction& i : tac.instructions) os << print_instruction(i) << "\n";
    return os.str();
}

}  // namespace nova

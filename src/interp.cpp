#include "nova/interp.hpp"

#include <map>

#include "nova/arith.hpp"

namespace nova {

namespace {

const char* kDivZero = "runtime error: division by zero";

struct Failure {
    Diagnostic diagnostic;
};

class AstEvaluator {
public:
    RunResult run(const Program& program) {
        RunResult r;
        try {
            for (const StmtPtr& s : program.statements) statement(*s, r);
        } catch (const Failure& f) {
            r.ok = false;
            r.error = f.diagnostic;
        }
        return r;
    }

private:
    std::map<std::string, std::int64_t> env_;

    void statement(const Stmt& s, RunResult& r) {
        switch (s.kind) {
            case Stmt::Kind::Let: {
                const auto& l = static_cast<const LetStmt&>(s);
                env_[l.name] = eval(*l.value);
                break;
            }
            case Stmt::Kind::Assign: {
                const auto& a = static_cast<const AssignStmt&>(s);
                env_[a.name] = eval(*a.value);
                break;
            }
            case Stmt::Kind::Print:
                r.output.push_back(eval(*static_cast<const PrintStmt&>(s).value));
                break;
        }
    }

    std::int64_t eval(const Expr& e) {
        switch (e.kind) {
            case Expr::Kind::Integer: return static_cast<const IntegerExpr&>(e).value;
            case Expr::Kind::Variable: {
                auto it = env_.find(static_cast<const VariableExpr&>(e).name);
                if (it == env_.end()) throw Failure{{e.location, "internal error: read of undeclared variable"}};
                return it->second;
            }
            case Expr::Kind::Unary: return wrap_neg(eval(*static_cast<const UnaryExpr&>(e).operand));
            case Expr::Kind::Binary: {
                const auto& b = static_cast<const BinaryExpr&>(e);
                std::int64_t l = eval(*b.lhs);
                std::int64_t r = eval(*b.rhs);
                switch (b.op) {
                    case BinaryOp::Add: return wrap_add(l, r);
                    case BinaryOp::Sub: return wrap_sub(l, r);
                    case BinaryOp::Mul: return wrap_mul(l, r);
                    case BinaryOp::Div: {
                        std::int64_t q = 0;
                        if (!checked_div(l, r, q)) throw Failure{{e.location, kDivZero}};
                        return q;
                    }
                }
            }
        }
        return 0;
    }
};

}  // namespace

RunResult run_ast(const Program& program) { return AstEvaluator().run(program); }

RunResult run_tac(const TacProgram& tac) {
    RunResult r;
    std::map<std::string, std::int64_t> env;
    auto fail = [&](SourceLocation loc, const char* msg) {
        r.ok = false;
        r.error = Diagnostic{loc, msg};
    };
    auto read = [&](const Operand& o, std::int64_t& out, SourceLocation loc) {
        if (o.is_const()) {
            out = o.value;
            return true;
        }
        auto it = env.find(o.name);
        if (it == env.end()) {
            fail(loc, "internal error: read of unset variable");
            return false;
        }
        out = it->second;
        return true;
    };
    for (const Instruction& i : tac.instructions) {
        std::int64_t a = 0, b = 0;
        if (!read(i.a, a, i.loc)) return r;
        if (is_binary(i.op) && !read(i.b, b, i.loc)) return r;
        switch (i.op) {
            case Opcode::Copy: env[i.dst.name] = a; break;
            case Opcode::Neg: env[i.dst.name] = wrap_neg(a); break;
            case Opcode::Add: env[i.dst.name] = wrap_add(a, b); break;
            case Opcode::Sub: env[i.dst.name] = wrap_sub(a, b); break;
            case Opcode::Mul: env[i.dst.name] = wrap_mul(a, b); break;
            case Opcode::Div: {
                std::int64_t q = 0;
                if (!checked_div(a, b, q)) {
                    fail(i.loc, kDivZero);
                    return r;
                }
                env[i.dst.name] = q;
                break;
            }
            case Opcode::Print: r.output.push_back(a); break;
        }
    }
    return r;
}

std::string format_output(const std::vector<std::int64_t>& output) {
    std::string s;
    for (std::int64_t v : output) s += std::to_string(v) + "\n";
    return s;
}

}  // namespace nova

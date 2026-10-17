#include "nova/optimizer.hpp"

#include <map>
#include <set>

#include "nova/arith.hpp"

namespace nova {

namespace {

// Rewrites `i` in place into `dst = a`.
void make_copy(Instruction& i, const Operand& a) {
    i.op = Opcode::Copy;
    i.a = a;
    i.b = Operand();
}

bool is_const_value(const Operand& o, std::int64_t v) { return o.is_const() && o.value == v; }

}  // namespace

bool constant_fold(TacProgram& tac) {
    bool changed = false;
    for (Instruction& i : tac.instructions) {
        if (i.op == Opcode::Neg && i.a.is_const()) {
            make_copy(i, Operand::constant(wrap_neg(i.a.value)));
            changed = true;
        } else if (is_binary(i.op) && i.a.is_const() && i.b.is_const()) {
            std::int64_t r = 0;
            switch (i.op) {
                case Opcode::Add: r = wrap_add(i.a.value, i.b.value); break;
                case Opcode::Sub: r = wrap_sub(i.a.value, i.b.value); break;
                case Opcode::Mul: r = wrap_mul(i.a.value, i.b.value); break;
                case Opcode::Div:
                    if (!checked_div(i.a.value, i.b.value, r)) continue;  // keep: must fail at run time
                    break;
                default: break;
            }
            make_copy(i, Operand::constant(r));
            changed = true;
        }
    }
    return changed;
}

bool constant_propagate(TacProgram& tac) {
    bool changed = false;
    std::map<std::string, std::int64_t> known;  // variable -> constant it currently holds
    auto subst = [&](Operand& o) {
        if (!o.is_var()) return;
        auto it = known.find(o.name);
        if (it == known.end()) return;
        o = Operand::constant(it->second);
        changed = true;
    };
    for (Instruction& i : tac.instructions) {
        // Uses are read before the instruction's own definition takes effect.
        subst(i.a);
        if (is_binary(i.op)) subst(i.b);
        if (i.op == Opcode::Print) continue;
        if (i.op == Opcode::Copy && i.a.is_const())
            known[i.dst.name] = i.a.value;
        else
            known.erase(i.dst.name);
    }
    return changed;
}

bool algebraic_simplify(TacProgram& tac) {
    bool changed = false;
    for (Instruction& i : tac.instructions) {
        if (!is_binary(i.op)) continue;
        const Operand a = i.a, b = i.b;
        switch (i.op) {
            case Opcode::Add:
                if (is_const_value(b, 0)) { make_copy(i, a); changed = true; }
                else if (is_const_value(a, 0)) { make_copy(i, b); changed = true; }
                break;
            case Opcode::Sub:
                if (is_const_value(b, 0)) { make_copy(i, a); changed = true; }
                else if (a.is_var() && a == b) { make_copy(i, Operand::constant(0)); changed = true; }
                else if (is_const_value(a, 0) && b.is_var()) {
                    i.op = Opcode::Neg;
                    i.a = b;
                    i.b = Operand();
                    changed = true;
                }
                break;
            case Opcode::Mul:
                if (is_const_value(b, 1)) { make_copy(i, a); changed = true; }
                else if (is_const_value(a, 1)) { make_copy(i, b); changed = true; }
                else if (is_const_value(a, 0) || is_const_value(b, 0)) {
                    make_copy(i, Operand::constant(0));
                    changed = true;
                }
                break;
            case Opcode::Div:
                if (is_const_value(b, 1)) { make_copy(i, a); changed = true; }
                break;
            default: break;
        }
    }
    return changed;
}

namespace {

bool can_fail(const Instruction& i) {
    return i.op == Opcode::Div && !(i.b.is_const() && i.b.value != 0);
}

}  // namespace

bool eliminate_dead_code(TacProgram& tac) {
    std::vector<Instruction>& ins = tac.instructions;
    std::vector<bool> keep(ins.size(), true);
    std::set<std::string> live;  // variables whose current value may still be read
    bool changed = false;
    for (size_t n = ins.size(); n-- > 0;) {
        const Instruction& i = ins[n];
        if (i.op != Opcode::Print) {
            if (!live.count(i.dst.name) && !can_fail(i)) {
                keep[n] = false;
                changed = true;
                continue;
            }
            live.erase(i.dst.name);
        }
        if (i.a.is_var()) live.insert(i.a.name);
        if (is_binary(i.op) && i.b.is_var()) live.insert(i.b.name);
    }
    if (!changed) return false;
    std::vector<Instruction> out;
    for (size_t n = 0; n < ins.size(); ++n)
        if (keep[n]) out.push_back(ins[n]);
    ins = std::move(out);
    return true;
}

const std::vector<PassInfo>& all_passes() {
    static const std::vector<PassInfo> passes = {
        {"fold", constant_fold},
        {"propagate", constant_propagate},
        {"simplify", algebraic_simplify},
        {"dce", eliminate_dead_code},
    };
    return passes;
}

const PassInfo* find_pass(const std::string& name) {
    for (const PassInfo& p : all_passes())
        if (name == p.name) return &p;
    return nullptr;
}

OptimizeResult optimize(const TacProgram& input, const std::vector<PassInfo>& passes, bool record_steps) {
    OptimizeResult r;
    r.program = input;
    while (r.iterations < kMaxOptimizeIterations) {
        ++r.iterations;
        bool any = false;
        for (const PassInfo& p : passes) {
            bool changed = p.run(r.program);
            any = any || changed;
            if (record_steps) r.steps.push_back({p.name, r.iterations, changed, print_tac(r.program)});
        }
        if (!any) {
            r.converged = true;
            break;
        }
    }
    return r;
}

}  // namespace nova

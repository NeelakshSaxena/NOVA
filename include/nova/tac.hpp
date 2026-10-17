#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "nova/ast.hpp"
#include "nova/source.hpp"

namespace nova {

// Three-address code for NOVA Script: straight-line (the language has no control flow).
//
// Operands are integer constants or named variables. Temporaries are variables whose name starts
// with '$' ("$t1", "$t2", ...); '$' cannot appear in a NOVA Script identifier, so they never clash.

struct Operand {
    enum class Kind { Const, Var };
    Kind kind = Kind::Const;
    std::int64_t value = 0;  // Const
    std::string name;        // Var

    static Operand constant(std::int64_t v) {
        Operand o;
        o.kind = Kind::Const;
        o.value = v;
        return o;
    }
    static Operand variable(std::string n) {
        Operand o;
        o.kind = Kind::Var;
        o.name = std::move(n);
        return o;
    }
    bool is_const() const { return kind == Kind::Const; }
    bool is_var() const { return kind == Kind::Var; }
    bool is_temp() const { return kind == Kind::Var && !name.empty() && name[0] == '$'; }
};

inline bool operator==(const Operand& a, const Operand& b) {
    if (a.kind != b.kind) return false;
    return a.is_const() ? a.value == b.value : a.name == b.name;
}
inline bool operator!=(const Operand& a, const Operand& b) { return !(a == b); }

enum class Opcode {
    Copy,   // dst = a
    Neg,    // dst = neg a
    Add,    // dst = a + b
    Sub,    // dst = a - b
    Mul,    // dst = a * b
    Div,    // dst = a / b   (runtime error if b == 0)
    Print,  // print a       (the only observable effect besides a runtime error)
};

struct Instruction {
    Opcode op = Opcode::Copy;
    Operand dst;  // unused (default) for Print
    Operand a;
    Operand b;    // only for binary opcodes
    SourceLocation loc;  // source position used in runtime-error messages (operator for Neg/binary)
};

inline bool is_binary(Opcode op) {
    return op == Opcode::Add || op == Opcode::Sub || op == Opcode::Mul || op == Opcode::Div;
}

struct TacProgram {
    std::vector<Instruction> instructions;
};

// Lowers a semantically valid program. Precondition: analyze(program).ok().
// Temporaries are numbered $t1, $t2, ... in the order their defining instruction is emitted
// (operands are evaluated left to right, innermost first). The outermost operation of an
// assignment writes straight into the target variable and uses no temporary.
TacProgram generate_tac(const Program& program);

// One instruction per line, e.g. "$t1 = x * 2", "z = $t1 + y", "x = neg y", "print z".
std::string print_operand(const Operand& o);
std::string print_instruction(const Instruction& i);
std::string print_tac(const TacProgram& tac);

}  // namespace nova

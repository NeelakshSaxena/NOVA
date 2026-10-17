#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "nova/source.hpp"

namespace nova {

enum class UnaryOp { Negate };
enum class BinaryOp { Add, Sub, Mul, Div };

// "-" for Negate; "+", "-", "*", "/" for binary operators.
const char* op_symbol(UnaryOp op);
const char* op_symbol(BinaryOp op);

// ---- Expressions ----------------------------------------------------------------------------

struct Expr {
    enum class Kind { Integer, Variable, Unary, Binary };
    Expr(Kind k, SourceLocation l) : kind(k), location(l) {}
    virtual ~Expr() = default;
    Kind kind;
    SourceLocation location;  // first token of the expression (the operator for Unary/Binary)
};
using ExprPtr = std::unique_ptr<Expr>;

struct IntegerExpr : Expr {
    IntegerExpr(SourceLocation l, std::int64_t v) : Expr(Kind::Integer, l), value(v) {}
    std::int64_t value;  // always >= 0; negative numbers are Unary(Negate, Integer)
};

struct VariableExpr : Expr {
    VariableExpr(SourceLocation l, std::string n) : Expr(Kind::Variable, l), name(std::move(n)) {}
    std::string name;
};

struct UnaryExpr : Expr {
    UnaryExpr(SourceLocation l, UnaryOp o, ExprPtr e) : Expr(Kind::Unary, l), op(o), operand(std::move(e)) {}
    UnaryOp op;
    ExprPtr operand;
};

struct BinaryExpr : Expr {
    BinaryExpr(SourceLocation l, BinaryOp o, ExprPtr a, ExprPtr b)
        : Expr(Kind::Binary, l), op(o), lhs(std::move(a)), rhs(std::move(b)) {}
    BinaryOp op;
    ExprPtr lhs, rhs;
};

// ---- Statements -----------------------------------------------------------------------------

struct Stmt {
    enum class Kind { Let, Assign, Print };
    Stmt(Kind k, SourceLocation l) : kind(k), location(l) {}
    virtual ~Stmt() = default;
    Kind kind;
    SourceLocation location;  // first token of the statement
};
using StmtPtr = std::unique_ptr<Stmt>;

// let <name> = <value>;
struct LetStmt : Stmt {
    LetStmt(SourceLocation l, std::string n, SourceLocation nl, ExprPtr v)
        : Stmt(Kind::Let, l), name(std::move(n)), name_location(nl), value(std::move(v)) {}
    std::string name;
    SourceLocation name_location;
    ExprPtr value;
};

// <name> = <value>;
struct AssignStmt : Stmt {
    AssignStmt(SourceLocation l, std::string n, ExprPtr v)
        : Stmt(Kind::Assign, l), name(std::move(n)), value(std::move(v)) {}
    std::string name;  // statement location is the location of the name
    ExprPtr value;
};

// print <value>;
struct PrintStmt : Stmt {
    PrintStmt(SourceLocation l, ExprPtr v) : Stmt(Kind::Print, l), value(std::move(v)) {}
    ExprPtr value;
};

struct Program {
    std::vector<StmtPtr> statements;
};

// Stable, indented text dump of the tree (two spaces per level), e.g.
//   Program
//     Let x
//       Binary +
//         Integer 2
//         Variable y
std::string dump_ast(const Program& program);

}  // namespace nova

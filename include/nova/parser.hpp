#pragma once

#include <memory>
#include <vector>

#include "nova/ast.hpp"
#include "nova/source.hpp"
#include "nova/token.hpp"

namespace nova {

struct ParseResult {
    // Non-null exactly when parsing succeeded (diagnostics is then empty).
    std::unique_ptr<Program> program;
    std::vector<Diagnostic> diagnostics;
    bool ok() const { return program != nullptr && diagnostics.empty(); }
};

// Maximum depth of an expression tree (nested parentheses or a long operator chain). Deeper input is
// rejected with a diagnostic so recursion stays bounded.
constexpr int kMaxExpressionDepth = 1000;

// Parses a token stream produced by lex() (which must have been free of lexical errors).
// Stops at the first syntax error and reports exactly one diagnostic. Never throws on malformed input.
ParseResult parse(const std::vector<Token>& tokens);

}  // namespace nova

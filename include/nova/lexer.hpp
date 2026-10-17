#pragma once

#include <string>
#include <vector>

#include "nova/source.hpp"
#include "nova/token.hpp"

namespace nova {

struct LexResult {
    // Always ends with exactly one EndOfFile token. Invalid input produces no token.
    std::vector<Token> tokens;
    std::vector<Diagnostic> diagnostics;  // in source order
    bool ok() const { return diagnostics.empty(); }
};

// Tokenizes `source`. Never throws on malformed input and never reads outside `source`.
// Whitespace: space, tab, \r, \n (\n starts a new line). There are no comments.
// Integer literals are decimal and must fit in a signed 64-bit integer (max 9223372036854775807);
// negative numbers are written with unary minus, handled by the parser.
LexResult lex(const std::string& source);

// One line per token: "<line>:<col> <KIND> '<lexeme>'".
std::string dump_tokens(const std::vector<Token>& tokens);

}  // namespace nova

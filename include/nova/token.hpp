#pragma once

#include <string>

#include "nova/source.hpp"

namespace nova {

enum class TokenKind {
    Identifier,
    Integer,
    KwLet,
    KwPrint,
    Assign,     // =
    Plus,       // +
    Minus,      // -
    Star,       // *
    Slash,      // /
    LParen,     // (
    RParen,     // )
    Semicolon,  // ;
    EndOfFile,
};

// Stable upper-case name used in token dumps, e.g. "KW_LET".
const char* to_string(TokenKind kind);

struct Token {
    TokenKind kind = TokenKind::EndOfFile;
    std::string lexeme;
    SourceLocation start;  // location of the first character
    SourceLocation end;    // location just past the last character
};

}  // namespace nova

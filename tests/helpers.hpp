#pragma once

// Test helper: compile NOVA Script source through the front end.
#include <memory>
#include <string>

#include "nova/ast.hpp"
#include "nova/lexer.hpp"
#include "nova/parser.hpp"
#include "nova/semantic.hpp"
#include "nova/tac.hpp"

namespace nova_test {

// Parses (and requires a valid parse of) `src`. Returns null if lexing/parsing failed.
inline std::unique_ptr<nova::Program> parse_program(const std::string& src) {
    nova::LexResult l = nova::lex(src);
    if (!l.ok()) return nullptr;
    nova::ParseResult p = nova::parse(l.tokens);
    return std::move(p.program);
}

// Parse + semantic check + TAC. Returns false (and leaves `tac` empty) if the source is invalid.
inline bool compile_tac(const std::string& src, nova::TacProgram& tac) {
    auto program = parse_program(src);
    if (!program || !nova::analyze(*program).ok()) return false;
    tac = nova::generate_tac(*program);
    return true;
}

}  // namespace nova_test

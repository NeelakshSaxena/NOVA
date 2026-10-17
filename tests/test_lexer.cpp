#include <string>
#include <vector>

#include "check.hpp"
#include "nova/lexer.hpp"

using namespace nova;

namespace {

std::string kinds(const std::string& src) {
    std::string s;
    for (const Token& t : lex(src).tokens) {
        if (!s.empty()) s += ' ';
        s += to_string(t.kind);
    }
    return s;
}

void test_empty_and_whitespace() {
    LexResult r = lex("");
    CHECK(r.ok());
    CHECK_EQ(r.tokens.size(), size_t(1));
    CHECK(r.tokens[0].kind == TokenKind::EndOfFile);
    CHECK(r.tokens[0].start == (SourceLocation{1, 1}));

    CHECK_EQ(kinds("   \t\r\n\n  "), std::string("EOF"));
    CHECK_EQ(kinds("let   x\n=\t1 ;"), std::string("KW_LET IDENTIFIER ASSIGN INTEGER SEMICOLON EOF"));
}

void test_every_token_kind() {
    CHECK_EQ(kinds("foo"), std::string("IDENTIFIER EOF"));
    CHECK_EQ(kinds("_a1"), std::string("IDENTIFIER EOF"));
    CHECK_EQ(kinds("42"), std::string("INTEGER EOF"));
    CHECK_EQ(kinds("let"), std::string("KW_LET EOF"));
    CHECK_EQ(kinds("print"), std::string("KW_PRINT EOF"));
    CHECK_EQ(kinds("="), std::string("ASSIGN EOF"));
    CHECK_EQ(kinds("+"), std::string("PLUS EOF"));
    CHECK_EQ(kinds("-"), std::string("MINUS EOF"));
    CHECK_EQ(kinds("*"), std::string("STAR EOF"));
    CHECK_EQ(kinds("/"), std::string("SLASH EOF"));
    CHECK_EQ(kinds("("), std::string("LPAREN EOF"));
    CHECK_EQ(kinds(")"), std::string("RPAREN EOF"));
    CHECK_EQ(kinds(";"), std::string("SEMICOLON EOF"));
}

void test_keywords_vs_identifiers() {
    CHECK_EQ(kinds("letx"), std::string("IDENTIFIER EOF"));
    CHECK_EQ(kinds("xlet"), std::string("IDENTIFIER EOF"));
    CHECK_EQ(kinds("printer"), std::string("IDENTIFIER EOF"));
    CHECK_EQ(kinds("Let"), std::string("IDENTIFIER EOF"));  // keywords are case-sensitive
    CHECK_EQ(kinds("let x"), std::string("KW_LET IDENTIFIER EOF"));
    LexResult r = lex("letx");
    CHECK_EQ(r.tokens[0].lexeme, std::string("letx"));
}

void test_lexemes_and_locations() {
    LexResult r = lex("let x = 10;\n  print x*2;\n");
    CHECK(r.ok());
    struct Exp { TokenKind k; const char* lex; int line, col; };
    const std::vector<Exp> exp = {
        {TokenKind::KwLet, "let", 1, 1},     {TokenKind::Identifier, "x", 1, 5},
        {TokenKind::Assign, "=", 1, 7},      {TokenKind::Integer, "10", 1, 9},
        {TokenKind::Semicolon, ";", 1, 11},  {TokenKind::KwPrint, "print", 2, 3},
        {TokenKind::Identifier, "x", 2, 9},  {TokenKind::Star, "*", 2, 10},
        {TokenKind::Integer, "2", 2, 11},    {TokenKind::Semicolon, ";", 2, 12},
        {TokenKind::EndOfFile, "", 3, 1},
    };
    CHECK_EQ(r.tokens.size(), exp.size());
    for (size_t i = 0; i < exp.size() && i < r.tokens.size(); ++i) {
        const Token& t = r.tokens[i];
        std::string where = "token #" + std::to_string(i);
        CHECK_MSG(t.kind == exp[i].k, where + " kind");
        CHECK_MSG(t.lexeme == exp[i].lex, where + " lexeme");
        CHECK_MSG(t.start == (SourceLocation{exp[i].line, exp[i].col}), where + " start location");
    }
    // End location is just past the lexeme.
    CHECK(r.tokens[3].end == (SourceLocation{1, 11}));
}

void test_tab_policy() {
    // A tab advances the column by exactly one.
    LexResult r = lex("\tx");
    CHECK(r.tokens[0].start == (SourceLocation{1, 2}));
    // CR counts as a column; LF resets it.
    r = lex("\r\nx");
    CHECK(r.tokens[0].start == (SourceLocation{2, 1}));
}

void test_eof_location() {
    CHECK(lex("x").tokens.back().start == (SourceLocation{1, 2}));
    CHECK(lex("x\n").tokens.back().start == (SourceLocation{2, 1}));
    CHECK(lex("\n\n\n").tokens.back().start == (SourceLocation{4, 1}));
}

void test_invalid_characters() {
    LexResult r = lex("let x = 1 @ 2;");
    CHECK(!r.ok());
    CHECK_EQ(r.diagnostics.size(), size_t(1));
    CHECK(r.diagnostics[0].location == (SourceLocation{1, 11}));
    CHECK_EQ(r.diagnostics[0].message, std::string("unexpected character '@'"));
    // The invalid character yields no token; lexing continues.
    CHECK_EQ(kinds("let x = 1 @ 2;"), std::string("KW_LET IDENTIFIER ASSIGN INTEGER INTEGER SEMICOLON EOF"));

    // Multiple errors across lines, reported in order.
    r = lex("a\n  #\n$");
    CHECK_EQ(r.diagnostics.size(), size_t(2));
    CHECK(r.diagnostics[0].location == (SourceLocation{2, 3}));
    CHECK(r.diagnostics[1].location == (SourceLocation{3, 1}));

    // Things that look like other languages' operators are not accepted.
    for (const char* bad : {"#", "{", "}", "[", "]", ",", ".", "<", ">", "!", "%", "&", "|", "\"", "'", ":"})
        CHECK_MSG(!lex(bad).ok(), std::string("rejects ") + bad);

    // Control character and non-ASCII (one diagnostic per UTF-8 character).
    r = lex(std::string("\x01", 1));
    CHECK_EQ(r.diagnostics[0].message, std::string("unexpected control character 0x01"));
    r = lex("\xC3\xA9 x");  // e-acute
    CHECK_EQ(r.diagnostics.size(), size_t(1));
    CHECK_EQ(r.diagnostics[0].message, std::string("unexpected non-ASCII character"));
    CHECK(r.tokens[0].start == (SourceLocation{1, 4}));  // columns count bytes
    // NUL byte inside the source does not truncate lexing.
    r = lex(std::string("x\0y", 3));
    CHECK_EQ(r.diagnostics.size(), size_t(1));
    CHECK(r.diagnostics[0].location == (SourceLocation{1, 2}));
}

void test_multichar_invalid_tokens() {
    LexResult r = lex("12ab + 3");
    CHECK_EQ(r.diagnostics.size(), size_t(1));
    CHECK(r.diagnostics[0].location == (SourceLocation{1, 1}));
    CHECK_EQ(r.diagnostics[0].message, std::string("invalid number '12ab'"));
    CHECK_EQ(kinds("12ab + 3"), std::string("PLUS INTEGER EOF"));
    CHECK(!lex("3.5").ok());  // no floating point literals
}

void test_integer_range() {
    CHECK(lex("9223372036854775807").ok());
    CHECK(lex("0009223372036854775807").ok());
    CHECK(lex("0").ok());
    LexResult r = lex("x = 9223372036854775808;");
    CHECK_EQ(r.diagnostics.size(), size_t(1));
    CHECK(r.diagnostics[0].location == (SourceLocation{1, 5}));
    CHECK(!lex("99999999999999999999999999").ok());
}

void test_long_input() {
    std::string src;
    for (int i = 0; i < 20000; ++i) src += "let x = (1+2)*3;\n";
    LexResult r = lex(src);
    CHECK(r.ok());
    CHECK_EQ(r.tokens.size(), size_t(20000 * 11 + 1));
    CHECK(r.tokens.back().start == (SourceLocation{20001, 1}));
    std::string longid(1000000, 'a');
    r = lex(longid);
    CHECK_EQ(r.tokens.size(), size_t(2));
    CHECK_EQ(r.tokens[0].lexeme.size(), size_t(1000000));
    CHECK_EQ(lex(std::string(500000, '9')).diagnostics.size(), size_t(1));
}

void test_dump_format() {
    CHECK_EQ(dump_tokens(lex("let x = 1;").tokens),
             std::string("1:1 KW_LET 'let'\n1:5 IDENTIFIER 'x'\n1:7 ASSIGN '='\n1:9 INTEGER '1'\n"
                         "1:10 SEMICOLON ';'\n1:11 EOF ''\n"));
    CHECK_EQ(format_diagnostic("f.nova", Diagnostic{{2, 3}, "boom"}), std::string("f.nova:2:3: error: boom"));
}

}  // namespace

int main() {
    test_empty_and_whitespace();
    test_every_token_kind();
    test_keywords_vs_identifiers();
    test_lexemes_and_locations();
    test_tab_policy();
    test_eof_location();
    test_invalid_characters();
    test_multichar_invalid_tokens();
    test_integer_range();
    test_long_input();
    test_dump_format();
    return nova_test::finish("lexer");
}

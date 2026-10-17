#include "nova/lexer.hpp"

#include <cstdio>
#include <sstream>

namespace nova {

const char* to_string(TokenKind kind) {
    switch (kind) {
        case TokenKind::Identifier: return "IDENTIFIER";
        case TokenKind::Integer: return "INTEGER";
        case TokenKind::KwLet: return "KW_LET";
        case TokenKind::KwPrint: return "KW_PRINT";
        case TokenKind::Assign: return "ASSIGN";
        case TokenKind::Plus: return "PLUS";
        case TokenKind::Minus: return "MINUS";
        case TokenKind::Star: return "STAR";
        case TokenKind::Slash: return "SLASH";
        case TokenKind::LParen: return "LPAREN";
        case TokenKind::RParen: return "RPAREN";
        case TokenKind::Semicolon: return "SEMICOLON";
        case TokenKind::EndOfFile: return "EOF";
    }
    return "UNKNOWN";
}

std::string format_diagnostic(const std::string& file, const Diagnostic& d) {
    return file + ":" + std::to_string(d.location.line) + ":" + std::to_string(d.location.column) +
           ": error: " + d.message;
}

namespace {

bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_ident_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool is_ident_part(char c) { return is_ident_start(c) || is_digit(c); }

// True if the decimal digit string fits in a signed 64-bit integer.
bool fits_int64(const std::string& digits) {
    const std::string max = "9223372036854775807";
    size_t first = digits.find_first_not_of('0');
    if (first == std::string::npos) return true;
    std::string s = digits.substr(first);
    if (s.size() != max.size()) return s.size() < max.size();
    return s <= max;
}

class Lexer {
public:
    explicit Lexer(const std::string& src) : src_(src) {}

    LexResult run() {
        while (pos_ < src_.size()) step();
        result_.tokens.push_back({TokenKind::EndOfFile, "", loc_, loc_});
        return std::move(result_);
    }

private:
    const std::string& src_;
    size_t pos_ = 0;
    SourceLocation loc_;
    LexResult result_;

    void advance() {
        if (src_[pos_] == '\n') {
            ++loc_.line;
            loc_.column = 1;
        } else {
            ++loc_.column;
        }
        ++pos_;
    }

    void emit(TokenKind kind, size_t begin, SourceLocation start) {
        result_.tokens.push_back({kind, src_.substr(begin, pos_ - begin), start, loc_});
    }

    void error(SourceLocation at, const std::string& msg) { result_.diagnostics.push_back({at, msg}); }

    void step() {
        char c = src_[pos_];
        SourceLocation start = loc_;
        size_t begin = pos_;

        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else if (is_ident_start(c)) {
            while (pos_ < src_.size() && is_ident_part(src_[pos_])) advance();
            std::string text = src_.substr(begin, pos_ - begin);
            TokenKind kind = TokenKind::Identifier;
            if (text == "let") kind = TokenKind::KwLet;
            else if (text == "print") kind = TokenKind::KwPrint;
            emit(kind, begin, start);
        } else if (is_digit(c)) {
            // Consume the whole alphanumeric run so "12ab" is one malformed token, not "12" "ab".
            bool all_digits = true;
            while (pos_ < src_.size() && is_ident_part(src_[pos_])) {
                if (!is_digit(src_[pos_])) all_digits = false;
                advance();
            }
            std::string text = src_.substr(begin, pos_ - begin);
            if (!all_digits) {
                error(start, "invalid number '" + text + "'");
            } else if (!fits_int64(text)) {
                error(start, "integer literal '" + text + "' is out of range for a 64-bit signed integer");
            } else {
                emit(TokenKind::Integer, begin, start);
            }
        } else if (c == '=' || c == '+' || c == '-' || c == '*' || c == '/' || c == '(' || c == ')' ||
                   c == ';') {
            advance();
            TokenKind kind = TokenKind::Assign;
            switch (c) {
                case '+': kind = TokenKind::Plus; break;
                case '-': kind = TokenKind::Minus; break;
                case '*': kind = TokenKind::Star; break;
                case '/': kind = TokenKind::Slash; break;
                case '(': kind = TokenKind::LParen; break;
                case ')': kind = TokenKind::RParen; break;
                case ';': kind = TokenKind::Semicolon; break;
                default: break;
            }
            emit(kind, begin, start);
        } else {
            unsigned char u = static_cast<unsigned char>(c);
            if (u >= 0x80) {
                // One diagnostic per non-ASCII character (lead byte plus continuation bytes).
                advance();
                while (pos_ < src_.size() && (static_cast<unsigned char>(src_[pos_]) & 0xC0) == 0x80) advance();
                error(start, "unexpected non-ASCII character");
            } else if (u >= 0x20 && u < 0x7F) {
                advance();
                error(start, std::string("unexpected character '") + c + "'");
            } else {
                advance();
                char buf[32];
                std::snprintf(buf, sizeof buf, "0x%02X", u);
                error(start, std::string("unexpected control character ") + buf);
            }
        }
    }
};

}  // namespace

LexResult lex(const std::string& source) { return Lexer(source).run(); }

std::string dump_tokens(const std::vector<Token>& tokens) {
    std::ostringstream os;
    for (const Token& t : tokens)
        os << t.start.line << ':' << t.start.column << ' ' << to_string(t.kind) << " '" << t.lexeme << "'\n";
    return os.str();
}

}  // namespace nova

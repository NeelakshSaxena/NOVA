#include "nova/parser.hpp"

#include <cstdint>
#include <string>

namespace nova {

namespace {

// Thrown internally to unwind to parse() after the first syntax error; never escapes this file.
struct SyntaxError {
    Diagnostic diagnostic;
};

// Wraps an expression with its tree depth so depth can be bounded (keeps dumping and destruction
// recursion bounded too).
struct Parsed {
    ExprPtr expr;
    int depth;
};

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens) : tokens_(tokens) {}

    ParseResult run() {
        ParseResult result;
        try {
            if (tokens_.empty() || tokens_.back().kind != TokenKind::EndOfFile)
                throw SyntaxError{{SourceLocation{}, "internal error: token stream does not end with end of file"}};
            auto program = std::make_unique<Program>();
            while (peek().kind != TokenKind::EndOfFile) program->statements.push_back(statement());
            result.program = std::move(program);
        } catch (const SyntaxError& e) {
            result.program.reset();
            result.diagnostics.push_back(e.diagnostic);
        }
        return result;
    }

private:
    const std::vector<Token>& tokens_;
    size_t pos_ = 0;

    const Token& peek() const { return tokens_[pos_]; }

    // Consumes and returns the current token; never moves past the final EndOfFile token.
    const Token& advance() {
        const Token& t = tokens_[pos_];
        if (pos_ + 1 < tokens_.size()) ++pos_;
        return t;
    }

    bool check(TokenKind k) const { return peek().kind == k; }

    static std::string describe(const Token& t) {
        switch (t.kind) {
            case TokenKind::EndOfFile: return "end of file";
            case TokenKind::Identifier: return "identifier '" + t.lexeme + "'";
            case TokenKind::Integer: return "integer '" + t.lexeme + "'";
            case TokenKind::KwLet:
            case TokenKind::KwPrint: return "keyword '" + t.lexeme + "'";
            default: return "'" + t.lexeme + "'";
        }
    }

    [[noreturn]] void fail(const Token& at, const std::string& expected) {
        throw SyntaxError{{at.start, "expected " + expected + ", found " + describe(at)}};
    }

    const Token& expect(TokenKind k, const char* what) {
        if (!check(k)) fail(peek(), what);
        return advance();
    }

    // statement = "let" identifier "=" expression ";"
    //           | identifier "=" expression ";"
    //           | "print" expression ";" ;
    StmtPtr statement() {
        if (check(TokenKind::KwLet)) {
            const Token& kw = advance();
            const Token& name = expect(TokenKind::Identifier, "identifier after 'let'");
            expect(TokenKind::Assign, "'=' after variable name");
            ExprPtr value = expression();
            expect(TokenKind::Semicolon, "';'");
            return std::make_unique<LetStmt>(kw.start, name.lexeme, name.start, std::move(value));
        }
        if (check(TokenKind::Identifier)) {
            const Token& name = advance();
            if (!check(TokenKind::Assign)) fail(peek(), "'=' after identifier '" + name.lexeme + "'");
            advance();
            ExprPtr value = expression();
            expect(TokenKind::Semicolon, "';'");
            return std::make_unique<AssignStmt>(name.start, name.lexeme, std::move(value));
        }
        if (check(TokenKind::KwPrint)) {
            const Token& kw = advance();
            ExprPtr value = expression();
            expect(TokenKind::Semicolon, "';'");
            return std::make_unique<PrintStmt>(kw.start, std::move(value));
        }
        fail(peek(), "statement");
    }

    ExprPtr expression() { return std::move(additive(0).expr); }

    // expression = term { ("+" | "-") term } ;   (left associative)
    Parsed additive(int nesting) {
        Parsed left = term(nesting);
        while (check(TokenKind::Plus) || check(TokenKind::Minus)) {
            const Token& op = advance();
            Parsed right = term(nesting);
            left = combine(op, op.kind == TokenKind::Plus ? BinaryOp::Add : BinaryOp::Sub, std::move(left),
                           std::move(right));
        }
        return left;
    }

    // term = unary { ("*" | "/") unary } ;   (left associative)
    Parsed term(int nesting) {
        Parsed left = unary(nesting);
        while (check(TokenKind::Star) || check(TokenKind::Slash)) {
            const Token& op = advance();
            Parsed right = unary(nesting);
            left = combine(op, op.kind == TokenKind::Star ? BinaryOp::Mul : BinaryOp::Div, std::move(left),
                           std::move(right));
        }
        return left;
    }

    // unary = [ "-" ] primary ;   (at most one minus)
    Parsed unary(int nesting) {
        if (check(TokenKind::Minus)) {
            const Token& op = advance();
            Parsed operand = primary(nesting);
            return bounded(op, operand.depth + 1,
                           std::make_unique<UnaryExpr>(op.start, UnaryOp::Negate, std::move(operand.expr)));
        }
        return primary(nesting);
    }

    // primary = integer | identifier | "(" expression ")" ;
    Parsed primary(int nesting) {
        const Token& t = peek();
        switch (t.kind) {
            case TokenKind::Integer: {
                advance();
                // The lexer guarantees the literal fits in int64.
                std::int64_t value = 0;
                for (char c : t.lexeme) value = value * 10 + (c - '0');
                return {std::make_unique<IntegerExpr>(t.start, value), 1};
            }
            case TokenKind::Identifier:
                advance();
                return {std::make_unique<VariableExpr>(t.start, t.lexeme), 1};
            case TokenKind::LParen: {
                const Token& open = advance();
                if (nesting + 1 > kMaxExpressionDepth) too_deep(open);
                Parsed inner = additive(nesting + 1);
                expect(TokenKind::RParen, "')'");
                return inner;  // parentheses only group; they have no AST node
            }
            default:
                fail(t, "expression");
        }
    }

    Parsed combine(const Token& op, BinaryOp kind, Parsed left, Parsed right) {
        int depth = (left.depth > right.depth ? left.depth : right.depth) + 1;
        return bounded(op, depth,
                       std::make_unique<BinaryExpr>(op.start, kind, std::move(left.expr), std::move(right.expr)));
    }

    Parsed bounded(const Token& at, int depth, ExprPtr e) {
        if (depth > kMaxExpressionDepth) too_deep(at);
        return {std::move(e), depth};
    }

    [[noreturn]] void too_deep(const Token& at) {
        throw SyntaxError{
            {at.start, "expression is nested too deeply (limit " + std::to_string(kMaxExpressionDepth) + ")"}};
    }
};

}  // namespace

ParseResult parse(const std::vector<Token>& tokens) { return Parser(tokens).run(); }

}  // namespace nova

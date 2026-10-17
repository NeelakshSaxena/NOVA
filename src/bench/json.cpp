#include "nova/bench/json.hpp"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace nova {
namespace bench {

namespace {

std::string i64_to_string(std::int64_t v) {
    if (v == 0) return "0";
    bool neg = v < 0;
    std::uint64_t u = neg ? 0 - static_cast<std::uint64_t>(v) : static_cast<std::uint64_t>(v);
    std::string s;
    while (u != 0) {
        s.insert(s.begin(), static_cast<char>('0' + u % 10));
        u /= 10;
    }
    return neg ? "-" + s : s;
}

void escape_string(std::string& out, const std::string& s) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
}

void newline(std::string& out, int indent, int level) {
    if (indent < 0) return;
    out.push_back('\n');
    out.append(static_cast<size_t>(indent) * static_cast<size_t>(level), ' ');
}

}  // namespace

Json& Json::set(const std::string& key, Json v) {
    type_ = Type::Object;
    for (auto& kv : obj_) {
        if (kv.first == key) {
            kv.second = std::move(v);
            return *this;
        }
    }
    obj_.emplace_back(key, std::move(v));
    return *this;
}

const Json* Json::find(const std::string& key) const {
    if (type_ != Type::Object) return nullptr;
    for (const auto& kv : obj_)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

void Json::dump_to(std::string& out, int indent, int level) const {
    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += b_ ? "true" : "false"; break;
        case Type::Int: out += i64_to_string(i_); break;
        case Type::Double: {
            if (!std::isfinite(d_)) {  // JSON has no NaN/Inf; keep the file valid and the loss visible
                out += "null";
                break;
            }
            char buf[40];
            std::snprintf(buf, sizeof buf, "%.17g", d_);
            std::string t = buf;
            if (t.find_first_of(".eE") == std::string::npos) t += ".0";  // keep it a double on re-parse
            out += t;
            break;
        }
        case Type::String: escape_string(out, s_); break;
        case Type::Array:
            if (arr_.empty()) {
                out += "[]";
                break;
            }
            out.push_back('[');
            for (size_t k = 0; k < arr_.size(); ++k) {
                if (k) out.push_back(',');
                newline(out, indent, level + 1);
                arr_[k].dump_to(out, indent, level + 1);
            }
            newline(out, indent, level);
            out.push_back(']');
            break;
        case Type::Object:
            if (obj_.empty()) {
                out += "{}";
                break;
            }
            out.push_back('{');
            for (size_t k = 0; k < obj_.size(); ++k) {
                if (k) out.push_back(',');
                newline(out, indent, level + 1);
                escape_string(out, obj_[k].first);
                out += indent < 0 ? ":" : ": ";
                obj_[k].second.dump_to(out, indent, level + 1);
            }
            newline(out, indent, level);
            out.push_back('}');
            break;
    }
}

std::string Json::dump(int indent) const {
    std::string out;
    dump_to(out, indent, 0);
    return out;
}

bool Json::operator==(const Json& o) const {
    if (type_ != o.type_) return false;
    switch (type_) {
        case Type::Null: return true;
        case Type::Bool: return b_ == o.b_;
        case Type::Int: return i_ == o.i_;
        case Type::Double: return d_ == o.d_;
        case Type::String: return s_ == o.s_;
        case Type::Array: return arr_ == o.arr_;
        case Type::Object: return obj_ == o.obj_;
    }
    return false;
}

namespace {

class Parser {
public:
    explicit Parser(const std::string& t) : t_(t) {}

    bool run(Json& out, std::string& err) {
        if (!value(out, 0)) {
            err = error_;
            return false;
        }
        skip_ws();
        if (pos_ != t_.size()) {
            fail("trailing characters after the JSON value");
            err = error_;
            return false;
        }
        return true;
    }

private:
    const std::string& t_;
    size_t pos_ = 0;
    std::string error_;

    bool fail(const std::string& msg) {
        int line = 1, col = 1;
        for (size_t k = 0; k < pos_ && k < t_.size(); ++k) {
            if (t_[k] == '\n') {
                ++line;
                col = 1;
            } else {
                ++col;
            }
        }
        if (error_.empty()) error_ = std::to_string(line) + ":" + std::to_string(col) + ": " + msg;
        return false;
    }

    void skip_ws() {
        while (pos_ < t_.size() && (t_[pos_] == ' ' || t_[pos_] == '\n' || t_[pos_] == '\t' || t_[pos_] == '\r')) ++pos_;
    }

    bool literal(const char* word) {
        size_t n = std::char_traits<char>::length(word);
        if (t_.compare(pos_, n, word) != 0) return false;
        pos_ += n;
        return true;
    }

    static void append_utf8(std::string& s, unsigned cp) {
        if (cp < 0x80) {
            s.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    bool string_value(std::string& out) {
        ++pos_;  // opening quote
        while (pos_ < t_.size()) {
            char c = t_[pos_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return fail("control character in string");
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (pos_ >= t_.size()) break;
            char e = t_[pos_++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (pos_ + 4 > t_.size()) return fail("truncated \\u escape");
                    unsigned cp = 0;
                    for (int k = 0; k < 4; ++k) {
                        char h = t_[pos_++];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(h - 'A' + 10);
                        else return fail("bad hex digit in \\u escape");
                    }
                    append_utf8(out, cp);
                    break;
                }
                default: return fail("bad escape sequence");
            }
        }
        return fail("unterminated string");
    }

    bool number_value(Json& out) {
        size_t start = pos_;
        if (t_[pos_] == '-') ++pos_;
        if (pos_ >= t_.size() || t_[pos_] < '0' || t_[pos_] > '9') return fail("bad number");
        bool is_double = false;
        while (pos_ < t_.size() && t_[pos_] >= '0' && t_[pos_] <= '9') ++pos_;
        if (pos_ < t_.size() && t_[pos_] == '.') {
            is_double = true;
            ++pos_;
            if (pos_ >= t_.size() || t_[pos_] < '0' || t_[pos_] > '9') return fail("bad number");
            while (pos_ < t_.size() && t_[pos_] >= '0' && t_[pos_] <= '9') ++pos_;
        }
        if (pos_ < t_.size() && (t_[pos_] == 'e' || t_[pos_] == 'E')) {
            is_double = true;
            ++pos_;
            if (pos_ < t_.size() && (t_[pos_] == '+' || t_[pos_] == '-')) ++pos_;
            if (pos_ >= t_.size() || t_[pos_] < '0' || t_[pos_] > '9') return fail("bad number");
            while (pos_ < t_.size() && t_[pos_] >= '0' && t_[pos_] <= '9') ++pos_;
        }
        std::string text = t_.substr(start, pos_ - start);
        if (!is_double) {
            // Integers that do not fit in int64 fall back to double.
            errno = 0;
            char* end = nullptr;
            long long v = std::strtoll(text.c_str(), &end, 10);
            if (errno == 0) {
                out = Json::integer(v);
                return true;
            }
        }
        out = Json::number(std::strtod(text.c_str(), nullptr));
        return true;
    }

    bool value(Json& out, int depth) {
        if (depth > 200) return fail("nesting too deep");
        skip_ws();
        if (pos_ >= t_.size()) return fail("unexpected end of input");
        char c = t_[pos_];
        if (c == '{') {
            ++pos_;
            out = Json::object();
            skip_ws();
            if (pos_ < t_.size() && t_[pos_] == '}') {
                ++pos_;
                return true;
            }
            while (true) {
                skip_ws();
                if (pos_ >= t_.size() || t_[pos_] != '"') return fail("expected a string key");
                std::string key;
                if (!string_value(key)) return false;
                skip_ws();
                if (pos_ >= t_.size() || t_[pos_] != ':') return fail("expected ':'");
                ++pos_;
                Json v;
                if (!value(v, depth + 1)) return false;
                out.set(key, std::move(v));
                skip_ws();
                if (pos_ < t_.size() && t_[pos_] == ',') {
                    ++pos_;
                    continue;
                }
                if (pos_ < t_.size() && t_[pos_] == '}') {
                    ++pos_;
                    return true;
                }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            ++pos_;
            out = Json::array();
            skip_ws();
            if (pos_ < t_.size() && t_[pos_] == ']') {
                ++pos_;
                return true;
            }
            while (true) {
                Json v;
                if (!value(v, depth + 1)) return false;
                out.push(std::move(v));
                skip_ws();
                if (pos_ < t_.size() && t_[pos_] == ',') {
                    ++pos_;
                    continue;
                }
                if (pos_ < t_.size() && t_[pos_] == ']') {
                    ++pos_;
                    return true;
                }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') {
            std::string s;
            if (!string_value(s)) return false;
            out = Json::string(std::move(s));
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9')) return number_value(out);
        if (literal("true")) { out = Json::boolean(true); return true; }
        if (literal("false")) { out = Json::boolean(false); return true; }
        if (literal("null")) { out = Json::null(); return true; }
        return fail("unexpected character");
    }
};

}  // namespace

bool Json::parse(const std::string& text, Json& out, std::string& error) {
    Parser p(text);
    Json result;
    if (!p.run(result, error)) return false;
    out = std::move(result);
    return true;
}

}  // namespace bench
}  // namespace nova

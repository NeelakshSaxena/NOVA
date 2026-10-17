#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace nova {
namespace bench {

// Minimal JSON value (null, bool, integer, double, string, array, object). Objects keep insertion order so
// output is deterministic. Used for experiment manifests and result files.
class Json {
public:
    enum class Type { Null, Bool, Int, Double, String, Array, Object };

    Json() = default;
    static Json null() { return Json(); }
    static Json boolean(bool v) { Json j; j.type_ = Type::Bool; j.b_ = v; return j; }
    static Json integer(std::int64_t v) { Json j; j.type_ = Type::Int; j.i_ = v; return j; }
    static Json number(double v) { Json j; j.type_ = Type::Double; j.d_ = v; return j; }
    static Json string(std::string v) { Json j; j.type_ = Type::String; j.s_ = std::move(v); return j; }
    static Json array() { Json j; j.type_ = Type::Array; return j; }
    static Json object() { Json j; j.type_ = Type::Object; return j; }

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_number() const { return type_ == Type::Int || type_ == Type::Double; }

    bool as_bool() const { return b_; }
    std::int64_t as_int() const { return type_ == Type::Double ? static_cast<std::int64_t>(d_) : i_; }
    double as_double() const { return type_ == Type::Int ? static_cast<double>(i_) : d_; }
    const std::string& as_string() const { return s_; }
    const std::vector<Json>& items() const { return arr_; }
    const std::vector<std::pair<std::string, Json>>& members() const { return obj_; }

    // Array append / object set (replaces an existing key, otherwise appends).
    Json& push(Json v) { arr_.push_back(std::move(v)); return *this; }
    Json& set(const std::string& key, Json v);
    // Object lookup; nullptr if absent (or not an object).
    const Json* find(const std::string& key) const;

    // Compact when indent < 0, otherwise pretty-printed with `indent` spaces per level.
    std::string dump(int indent = 2) const;

    // Parses `text`. On failure returns false and sets `error` ("line:col: message").
    static bool parse(const std::string& text, Json& out, std::string& error);

    bool operator==(const Json& o) const;
    bool operator!=(const Json& o) const { return !(*this == o); }

private:
    Type type_ = Type::Null;
    bool b_ = false;
    std::int64_t i_ = 0;
    double d_ = 0.0;
    std::string s_;
    std::vector<Json> arr_;
    std::vector<std::pair<std::string, Json>> obj_;

    void dump_to(std::string& out, int indent, int level) const;
};

}  // namespace bench
}  // namespace nova

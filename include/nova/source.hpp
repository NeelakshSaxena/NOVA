#pragma once

#include <string>

namespace nova {

// 1-based line and column. Columns count bytes; a tab counts as one column.
struct SourceLocation {
    int line = 1;
    int column = 1;
};

inline bool operator==(const SourceLocation& a, const SourceLocation& b) {
    return a.line == b.line && a.column == b.column;
}

struct Diagnostic {
    SourceLocation location;
    std::string message;
};

// "<file>:<line>:<column>: error: <message>"
std::string format_diagnostic(const std::string& file, const Diagnostic& d);

}  // namespace nova

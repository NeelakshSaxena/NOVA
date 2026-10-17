#pragma once

#include <map>
#include <string>
#include <vector>

#include "nova/ast.hpp"
#include "nova/source.hpp"

namespace nova {

// Single global scope. Maps a declared variable name to where it was declared.
class SymbolTable {
public:
    // Returns false (and keeps the original) if `name` is already declared.
    bool declare(const std::string& name, SourceLocation where);
    bool contains(const std::string& name) const { return symbols_.count(name) != 0; }
    // Declaration location; only valid if contains(name).
    SourceLocation declared_at(const std::string& name) const { return symbols_.at(name); }
    size_t size() const { return symbols_.size(); }

private:
    std::map<std::string, SourceLocation> symbols_;
};

struct SemanticResult {
    std::vector<Diagnostic> diagnostics;  // in order of detection; empty means the program is valid
    SymbolTable symbols;
    bool ok() const { return diagnostics.empty(); }
};

// Checks declaration rules (see docs/language.md). Reports every error, not only the first.
SemanticResult analyze(const Program& program);

}  // namespace nova

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "nova/ast.hpp"
#include "nova/source.hpp"
#include "nova/tac.hpp"

namespace nova {

struct RunResult {
    std::vector<std::int64_t> output;  // values printed so far (kept even if a runtime error occurs)
    bool ok = true;
    Diagnostic error;  // set when !ok; message is "runtime error: ..."
};

// Reference semantics. Both require a semantically valid program (see analyze()).
// They are independent implementations so tests can check that TAC lowering preserves meaning.
RunResult run_ast(const Program& program);
RunResult run_tac(const TacProgram& tac);

// Output exactly as `nova run` prints it: one decimal integer per line.
std::string format_output(const std::vector<std::int64_t>& output);

}  // namespace nova

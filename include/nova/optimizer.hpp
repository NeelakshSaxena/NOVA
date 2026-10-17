#pragma once

#include <string>
#include <vector>

#include "nova/tac.hpp"

namespace nova {

// Educational TAC optimizer. Every pass preserves observable behaviour: the sequence of printed values
// and whether/where execution stops with a runtime error (see docs/optimizer.md for the per-pass
// preconditions, rewrite rules and caveats). Each pass returns true iff it changed the program.

// Replaces an operation whose operands are all constants by a copy of the result.
// Division is folded only when the divisor is non-zero (a division by zero must still fail at run time).
bool constant_fold(TacProgram& tac);

// Forward pass: replaces uses of a variable by its constant value while the variable is known to hold one.
bool constant_propagate(TacProgram& tac);

// Conservative identities that are exact under NOVA's wrapping arithmetic:
//   x+0, 0+x, x-0, x*1, 1*x, x/1 -> x ;  x*0, 0*x, x-x -> 0 ;  0-x -> neg x.
bool algebraic_simplify(TacProgram& tac);

// Backward liveness pass: removes assignments whose target is never read afterwards, provided the
// instruction cannot fail. Prints are never removed; a division is removable only by a non-zero constant.
bool eliminate_dead_code(TacProgram& tac);

using PassFn = bool (*)(TacProgram&);

struct PassInfo {
    const char* name;  // CLI name
    PassFn run;
};

// The implemented passes in default pipeline order: fold, propagate, simplify, dce.
const std::vector<PassInfo>& all_passes();
// Looks up a pass by CLI name; returns nullptr if unknown.
const PassInfo* find_pass(const std::string& name);

struct OptimizeStep {
    std::string pass;
    int iteration;       // 1-based
    bool changed;
    std::string after;   // TAC text after the pass (also recorded when unchanged)
};

struct OptimizeResult {
    TacProgram program;
    int iterations = 0;          // full sweeps over the pass list (the last one changed nothing if converged)
    bool converged = false;      // false only if the iteration cap was hit
    std::vector<OptimizeStep> steps;
};

// Safety cap on sweeps. Each sweep that changes anything strictly decreases the lexicographic measure
// (#binary instructions, #non-copy instructions, #variable operand uses, #instructions), so the loop
// terminates long before this cap on any finite program; the cap only guards against a pass bug.
constexpr int kMaxOptimizeIterations = 10000;

// Runs `passes` (in the given order) repeatedly until a full sweep changes nothing (a fixed point).
// `record_steps` stores the TAC after every pass application in OptimizeResult::steps.
OptimizeResult optimize(const TacProgram& input, const std::vector<PassInfo>& passes, bool record_steps = false);

}  // namespace nova

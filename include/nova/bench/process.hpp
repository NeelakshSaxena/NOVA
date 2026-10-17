#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nova {
namespace bench {

struct ProcessResult {
    bool started = false;    // false: the program could not be launched (see `error`)
    int exit_code = -1;      // valid when started && !timed_out
    bool timed_out = false;  // the process was killed after exceeding the timeout
    bool output_truncated = false;
    std::string out;         // captured stdout
    std::string err;         // captured stderr
    double wall_ms = 0;      // launch-to-exit wall time
    std::string error;       // launcher error text when !started
};

struct ProcessOptions {
    int timeout_ms = 0;                              // <= 0: no timeout
    size_t max_output_bytes = 16u * 1024u * 1024u;   // per stream
    std::uint64_t cpu_affinity_mask = 0;             // 0: do not change; bit i = logical CPU i (Windows, Linux)
    bool high_priority = false;                      // Windows: HIGH_PRIORITY_CLASS for the child (reduces noise)
};

// Runs argv[0] with the remaining elements as its arguments. The arguments are passed as an array: no shell
// is involved and nothing is re-parsed, so values containing spaces, quotes or shell metacharacters stay
// single arguments. argv[0] is looked up on PATH. stdout and stderr are captured (each capped at
// `max_output_bytes`; excess is discarded and `output_truncated` is set). A timeout_ms <= 0 means no timeout.
//
// Implemented with CreateProcess on Windows (tested) and fork/execvp on POSIX systems (NOT tested in this
// project's current environment; see state/KNOWN_ISSUES.md).
ProcessResult run_process(const std::vector<std::string>& argv, const ProcessOptions& options);
ProcessResult run_process(const std::vector<std::string>& argv, int timeout_ms = 0,
                          size_t max_output_bytes = 16u * 1024u * 1024u);

}  // namespace bench
}  // namespace nova

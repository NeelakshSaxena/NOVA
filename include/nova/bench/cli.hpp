#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace nova {
namespace bench {

// Implements `nova bench ...`. `args` are the arguments after "bench". Returns the process exit code
// (0 success, 1 failure of the run/report, 2 usage error).
int run_bench_command(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

// Text for the `nova bench` section of --help.
const char* bench_usage();

}  // namespace bench
}  // namespace nova

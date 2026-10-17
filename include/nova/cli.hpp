#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace nova {

// Version string, e.g. "0.1.0". Set from the CMake project version.
std::string version();

// Runs the command line. `args` excludes argv[0]. Returns the process exit code.
int run_cli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

}  // namespace nova

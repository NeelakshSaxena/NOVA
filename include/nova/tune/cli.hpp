#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace nova {
namespace tune {

// `nova tune <benchmark> --budget=B ...` (args = everything after "tune"). Exit codes: 0 a winner among eligible
// candidates was found and saved, 1 no eligible candidate / infrastructure error, 2 usage error.
int run_tune_command(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

// `nova explain <results-dir>` (args = everything after "explain"). Exit 0 on success, 1 if the directory cannot be
// read or contains invalid records, 2 usage error.
int run_explain_command(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

const char* tune_usage();

}  // namespace tune
}  // namespace nova

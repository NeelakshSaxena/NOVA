#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "nova/bench/json.hpp"

namespace nova {
namespace bench {

struct Environment {
    std::string os;                  // e.g. "Windows 11 Home Single Language 25H2 (build 26300)"
    std::string cpu;                 // CPU model string
    int logical_cpus = 0;
    std::uint64_t ram_bytes = 0;
    std::string compiler_command;    // as invoked, e.g. "g++"
    std::string compiler_version;    // first line of `<compiler> --version`
    std::string compiler_target;     // output of `<compiler> -dumpmachine`
    std::string nova_version;
};

// Collects the facts above. Anything that cannot be determined is left empty/zero (validate_environment
// reports those instead of silently omitting them).
Environment collect_environment(const std::string& compiler_command);

// Names of fields that are empty/zero.
std::vector<std::string> missing_environment_fields(const Environment& env);

Json environment_to_json(const Environment& env);
bool environment_from_json(const Json& j, Environment& env, std::string& error);

}  // namespace bench
}  // namespace nova

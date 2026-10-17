#pragma once

#include <string>

namespace nova {
namespace bench {

// Lower-case hex SHA-256 of `data` (FIPS 180-4). Used for the benchmark "source hash".
std::string sha256_hex(const std::string& data);

}  // namespace bench
}  // namespace nova

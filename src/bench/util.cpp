#include "nova/bench/util.hpp"

#include <ctime>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <direct.h>
#include <sys/stat.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace nova {
namespace bench {

bool read_file(const std::string& path, std::string& contents) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    contents = ss.str();
    return true;
}

bool write_file(const std::string& path, const std::string& contents) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << contents;
    out.flush();
    return static_cast<bool>(out);
}

bool file_exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

std::int64_t file_size(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return -1;
    return static_cast<std::int64_t>(st.st_size);
}

namespace {

bool make_one_dir(const std::string& path) {
#ifdef _WIN32
    return _mkdir(path.c_str()) == 0;
#else
    return mkdir(path.c_str(), 0777) == 0;
#endif
}

bool is_dir(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFDIR) != 0;
}

}  // namespace

bool make_dirs(const std::string& path) {
    if (path.empty()) return false;
    if (is_dir(path)) return true;
    size_t cut = path.find_last_of("/\\");
    if (cut != std::string::npos && cut > 0) {
        std::string parent = path.substr(0, cut);
        // Do not try to create a bare drive prefix such as "C:".
        if (!(parent.size() == 2 && parent[1] == ':') && !make_dirs(parent)) return false;
    }
    return make_one_dir(path) || is_dir(path);
}

std::string utc_timestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tmv = *std::gmtime(&t);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tmv);
    return buf;
}

std::string join(const std::vector<std::string>& parts, const std::string& sep) {
    std::string r;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) r += sep;
        r += parts[i];
    }
    return r;
}

}  // namespace bench
}  // namespace nova

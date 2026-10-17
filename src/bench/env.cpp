#include "nova/bench/env.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

#include "nova/bench/process.hpp"
#include "nova/cli.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/utsname.h>
#include <unistd.h>
#endif

namespace nova {
namespace bench {

namespace {

std::string first_line(const std::string& s) {
    size_t e = s.find_first_of("\r\n");
    return e == std::string::npos ? s : s.substr(0, e);
}

#ifdef _WIN32
std::string registry_string(const char* subkey, const char* value) {
    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) return "";
    char buf[512];
    DWORD size = sizeof buf - 1, type = 0;
    std::string out;
    if (RegQueryValueExA(key, value, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS &&
        type == REG_SZ) {
        buf[size < sizeof buf ? size : sizeof buf - 1] = '\0';
        out = buf;
    }
    RegCloseKey(key);
    return out;
}
#endif

}  // namespace

Environment collect_environment(const std::string& compiler_command) {
    Environment env;
    env.nova_version = version();
#ifdef _WIN32
    const char* nt = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    std::string product = registry_string(nt, "ProductName");
    std::string display = registry_string(nt, "DisplayVersion");
    std::string build = registry_string(nt, "CurrentBuild");
    if (!product.empty()) {
        // Windows 11 still reports ProductName "Windows 10 ..." in the registry; builds >= 22000 are Windows 11.
        int build_number = std::atoi(build.c_str());
        if (build_number >= 22000 && product.rfind("Windows 10", 0) == 0) product = "Windows 11" + product.substr(10);
        env.os = product;
        if (!display.empty()) env.os += " " + display;
        if (!build.empty()) env.os += " (build " + build + ")";
    }
    env.cpu = registry_string("HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "ProcessorNameString");
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    env.logical_cpus = static_cast<int>(si.dwNumberOfProcessors);
    // GlobalMemoryStatusEx is not declared by old MinGW headers unless _WIN32_WINNT is raised, so use the
    // 32-bit-clamped GlobalMemoryStatus fallback only if needed.
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof ms;
    if (GlobalMemoryStatusEx(&ms)) env.ram_bytes = static_cast<std::uint64_t>(ms.ullTotalPhys);
#else
    struct utsname u;
    if (uname(&u) == 0) env.os = std::string(u.sysname) + " " + u.release;
    std::ifstream cpuinfo("/proc/cpuinfo");
    for (std::string line; std::getline(cpuinfo, line);) {
        if (line.rfind("model name", 0) == 0) {
            size_t c = line.find(':');
            if (c != std::string::npos) env.cpu = line.substr(c + 2);
            break;
        }
    }
    env.logical_cpus = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
    std::ifstream meminfo("/proc/meminfo");
    for (std::string line; std::getline(meminfo, line);) {
        if (line.rfind("MemTotal:", 0) == 0) {
            env.ram_bytes = std::strtoull(line.c_str() + 9, nullptr, 10) * 1024ULL;
            break;
        }
    }
#endif
    env.compiler_command = compiler_command;
    ProcessResult v = run_process({compiler_command, "--version"}, 20000);
    if (v.started && v.exit_code == 0) env.compiler_version = first_line(v.out);
    ProcessResult m = run_process({compiler_command, "-dumpmachine"}, 20000);
    if (m.started && m.exit_code == 0) env.compiler_target = first_line(m.out);
    return env;
}

std::vector<std::string> missing_environment_fields(const Environment& e) {
    std::vector<std::string> missing;
    if (e.os.empty()) missing.push_back("os");
    if (e.cpu.empty()) missing.push_back("cpu");
    if (e.logical_cpus <= 0) missing.push_back("logical_cpus");
    if (e.ram_bytes == 0) missing.push_back("ram_bytes");
    if (e.compiler_command.empty()) missing.push_back("compiler_command");
    if (e.compiler_version.empty()) missing.push_back("compiler_version");
    if (e.compiler_target.empty()) missing.push_back("compiler_target");
    if (e.nova_version.empty()) missing.push_back("nova_version");
    return missing;
}

Json environment_to_json(const Environment& e) {
    Json j = Json::object();
    j.set("os", Json::string(e.os));
    j.set("cpu", Json::string(e.cpu));
    j.set("logical_cpus", Json::integer(e.logical_cpus));
    j.set("ram_bytes", Json::integer(static_cast<std::int64_t>(e.ram_bytes)));
    j.set("compiler_command", Json::string(e.compiler_command));
    j.set("compiler_version", Json::string(e.compiler_version));
    j.set("compiler_target", Json::string(e.compiler_target));
    j.set("nova_version", Json::string(e.nova_version));
    return j;
}

bool environment_from_json(const Json& j, Environment& e, std::string& error) {
    auto str = [&](const char* key, std::string& out) {
        const Json* v = j.find(key);
        if (!v || v->type() != Json::Type::String) {
            error = std::string("environment: missing string field '") + key + "'";
            return false;
        }
        out = v->as_string();
        return true;
    };
    auto num = [&](const char* key, std::int64_t& out) {
        const Json* v = j.find(key);
        if (!v || !v->is_number()) {
            error = std::string("environment: missing number field '") + key + "'";
            return false;
        }
        out = v->as_int();
        return true;
    };
    Environment r;
    std::int64_t cpus = 0, ram = 0;
    if (!str("os", r.os) || !str("cpu", r.cpu) || !num("logical_cpus", cpus) || !num("ram_bytes", ram) ||
        !str("compiler_command", r.compiler_command) || !str("compiler_version", r.compiler_version) ||
        !str("compiler_target", r.compiler_target) || !str("nova_version", r.nova_version))
        return false;
    r.logical_cpus = static_cast<int>(cpus);
    r.ram_bytes = static_cast<std::uint64_t>(ram);
    e = r;
    return true;
}

}  // namespace bench
}  // namespace nova

#include "nova/bench/process.hpp"

#include <chrono>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <sched.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace nova {
namespace bench {

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

void append_capped(std::string& dst, const char* data, size_t n, size_t cap, bool& truncated) {
    if (dst.size() >= cap) {
        truncated = true;
        return;
    }
    size_t room = cap - dst.size();
    if (n > room) {
        n = room;
        truncated = true;
    }
    dst.append(data, n);
}

}  // namespace

#ifdef _WIN32

namespace {

// Inverse of CommandLineToArgvW / the MSVCRT parsing rules.
std::string quote_arg(const std::string& a) {
    if (!a.empty() && a.find_first_of(" \t\n\v\"") == std::string::npos) return a;
    std::string r = "\"";
    for (size_t i = 0;; ++i) {
        size_t backslashes = 0;
        while (i < a.size() && a[i] == '\\') {
            ++i;
            ++backslashes;
        }
        if (i == a.size()) {
            r.append(backslashes * 2, '\\');
            break;
        }
        if (a[i] == '"') {
            r.append(backslashes * 2 + 1, '\\');
            r.push_back('"');
        } else {
            r.append(backslashes, '\\');
            r.push_back(a[i]);
        }
    }
    r.push_back('"');
    return r;
}

}  // namespace

ProcessResult run_process(const std::vector<std::string>& argv, const ProcessOptions& options) {
    const int timeout_ms = options.timeout_ms;
    const size_t max_output_bytes = options.max_output_bytes;
    ProcessResult res;
    if (argv.empty() || argv[0].empty()) {
        res.error = "empty command";
        return res;
    }
    std::string cmdline;
    for (size_t i = 0; i < argv.size(); ++i) {
        if (i) cmdline.push_back(' ');
        std::string arg = argv[i];
        // CreateProcess does not resolve a relative program path that uses '/' separators; normalize argv[0].
        if (i == 0)
            for (char& c : arg)
                if (c == '/') c = '\\';
        cmdline += quote_arg(arg);
    }
    if (cmdline.size() >= 32767) {
        res.error = "command line too long";
        return res;
    }

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof sa;
    sa.lpSecurityDescriptor = nullptr;
    sa.bInheritHandle = TRUE;

    HANDLE out_rd = nullptr, out_wr = nullptr;
    if (!CreatePipe(&out_rd, &out_wr, &sa, 0)) {
        res.error = "CreatePipe failed";
        return res;
    }
    SetHandleInformation(out_rd, HANDLE_FLAG_INHERIT, 0);

    char tmp_dir[MAX_PATH], tmp_file[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp_dir);
    GetTempFileNameA(tmp_dir, "nva", 0, tmp_file);
    HANDLE err_wr = CreateFileA(tmp_file, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_TEMPORARY, nullptr);
    HANDLE nul = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    if (err_wr == INVALID_HANDLE_VALUE || nul == INVALID_HANDLE_VALUE) {
        res.error = "cannot create stderr capture file";
        CloseHandle(out_rd);
        CloseHandle(out_wr);
        if (err_wr != INVALID_HANDLE_VALUE) CloseHandle(err_wr);
        if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
        DeleteFileA(tmp_file);
        return res;
    }

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = out_wr;
    si.hStdError = err_wr;
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof pi);

    std::vector<char> buf(cmdline.begin(), cmdline.end());
    buf.push_back('\0');
    Clock::time_point t0 = Clock::now();
    DWORD flags = CREATE_NO_WINDOW;
    if (options.cpu_affinity_mask != 0) flags |= CREATE_SUSPENDED;  // set affinity before the first instruction runs
    BOOL ok = CreateProcessA(nullptr, buf.data(), nullptr, nullptr, TRUE, flags, nullptr, nullptr, &si, &pi);
    DWORD launch_error = ok ? 0 : GetLastError();
    CloseHandle(out_wr);
    CloseHandle(err_wr);
    CloseHandle(nul);
    if (!ok) {
        res.error = "cannot launch '" + argv[0] + "' (Windows error " + std::to_string(launch_error) + ")";
        CloseHandle(out_rd);
        DeleteFileA(tmp_file);
        return res;
    }
    res.started = true;
    if (options.cpu_affinity_mask != 0) {
        bool set = SetProcessAffinityMask(pi.hProcess, static_cast<DWORD_PTR>(options.cpu_affinity_mask)) != 0;
        if (options.high_priority) SetPriorityClass(pi.hProcess, HIGH_PRIORITY_CLASS);
        ResumeThread(pi.hThread);
        if (!set) res.error = "SetProcessAffinityMask failed (Windows error " + std::to_string(GetLastError()) + ")";
    } else if (options.high_priority) {
        SetPriorityClass(pi.hProcess, HIGH_PRIORITY_CLASS);
    }

    char chunk[65536];
    bool exited = false;
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(out_rd, nullptr, 0, nullptr, &avail, nullptr)) break;  // pipe closed by the child
        if (avail > 0) {
            DWORD got = 0;
            DWORD want = avail < sizeof chunk ? avail : static_cast<DWORD>(sizeof chunk);
            if (!ReadFile(out_rd, chunk, want, &got, nullptr) || got == 0) break;
            append_capped(res.out, chunk, got, max_output_bytes, res.output_truncated);
            continue;
        }
        if (exited) break;  // process gone and the pipe is drained
        if (timeout_ms > 0 && ms_since(t0) > timeout_ms) {
            TerminateProcess(pi.hProcess, 1);
            res.timed_out = true;
            WaitForSingleObject(pi.hProcess, 5000);
            break;
        }
        if (WaitForSingleObject(pi.hProcess, 1) == WAIT_OBJECT_0) exited = true;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    res.wall_ms = ms_since(t0);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    res.exit_code = static_cast<int>(code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(out_rd);

    {
        std::ifstream f(tmp_file, std::ios::binary);
        std::ostringstream ss;
        ss << f.rdbuf();
        std::string e = ss.str();
        append_capped(res.err, e.data(), e.size(), max_output_bytes, res.output_truncated);
    }
    DeleteFileA(tmp_file);
    return res;
}

#else  // POSIX

ProcessResult run_process(const std::vector<std::string>& argv, const ProcessOptions& options) {
    const int timeout_ms = options.timeout_ms;
    const size_t max_output_bytes = options.max_output_bytes;
    ProcessResult res;
    if (argv.empty() || argv[0].empty()) {
        res.error = "empty command";
        return res;
    }
    int out_pipe[2], err_pipe[2], exec_pipe[2];
    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0 || pipe(exec_pipe) != 0) {
        res.error = "pipe failed";
        return res;
    }
    fcntl(exec_pipe[1], F_SETFD, FD_CLOEXEC);
    std::vector<char*> args;
    for (const std::string& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);

    Clock::time_point t0 = Clock::now();
    pid_t pid = fork();
    if (pid < 0) {
        res.error = "fork failed";
        return res;
    }
    if (pid == 0) {
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) dup2(devnull, 0);
        dup2(out_pipe[1], 1);
        dup2(err_pipe[1], 2);
#ifdef __linux__
        if (options.cpu_affinity_mask != 0) {
            cpu_set_t set;
            CPU_ZERO(&set);
            for (int cpu = 0; cpu < 64; ++cpu)
                if (options.cpu_affinity_mask & (1ULL << cpu)) CPU_SET(cpu, &set);
            sched_setaffinity(0, sizeof set, &set);
        }
#endif
        close(out_pipe[0]); close(out_pipe[1]); close(err_pipe[0]); close(err_pipe[1]); close(exec_pipe[0]);
        execvp(args[0], args.data());
        int e = errno;
        ssize_t ignored = write(exec_pipe[1], &e, sizeof e);
        (void)ignored;
        _exit(127);
    }
    close(out_pipe[1]); close(err_pipe[1]); close(exec_pipe[1]);
    int exec_errno = 0;
    if (read(exec_pipe[0], &exec_errno, sizeof exec_errno) == static_cast<ssize_t>(sizeof exec_errno)) {
        waitpid(pid, nullptr, 0);
        close(out_pipe[0]); close(err_pipe[0]); close(exec_pipe[0]);
        res.error = "cannot launch '" + argv[0] + "' (" + strerror(exec_errno) + ")";
        return res;
    }
    close(exec_pipe[0]);
    res.started = true;

    struct pollfd fds[2] = {{out_pipe[0], POLLIN, 0}, {err_pipe[0], POLLIN, 0}};
    bool open_fd[2] = {true, true};
    char chunk[65536];
    while (open_fd[0] || open_fd[1]) {
        int wait_ms = 10;
        if (timeout_ms > 0) {
            double left = timeout_ms - ms_since(t0);
            if (left <= 0) {
                kill(pid, SIGKILL);
                res.timed_out = true;
                break;
            }
            if (left < wait_ms) wait_ms = static_cast<int>(left) + 1;
        }
        int pr = poll(fds, 2, wait_ms);
        if (pr < 0 && errno != EINTR) break;
        for (int k = 0; k < 2; ++k) {
            if (!open_fd[k] || !(fds[k].revents & (POLLIN | POLLHUP))) continue;
            ssize_t n = read(fds[k].fd, chunk, sizeof chunk);
            if (n <= 0) {
                open_fd[k] = false;
                fds[k].fd = -1;
            } else {
                append_capped(k == 0 ? res.out : res.err, chunk, static_cast<size_t>(n), max_output_bytes,
                              res.output_truncated);
            }
        }
    }
    close(out_pipe[0]);
    close(err_pipe[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    res.wall_ms = ms_since(t0);
    if (WIFEXITED(status)) res.exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) res.exit_code = 128 + WTERMSIG(status);
    return res;
}

#endif

ProcessResult run_process(const std::vector<std::string>& argv, int timeout_ms, size_t max_output_bytes) {
    ProcessOptions o;
    o.timeout_ms = timeout_ms;
    o.max_output_bytes = max_output_bytes;
    return run_process(argv, o);
}

}  // namespace bench
}  // namespace nova

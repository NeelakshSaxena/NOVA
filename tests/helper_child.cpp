// Helper program for process-runner tests. Modes (argv[1]):
//   echo <args...>   print each argument on its own line, wrapped as [arg]
//   exit <code>      exit with the given code
//   err <text>       write text to stderr, exit 0
//   sleep <ms>       busy-wait/sleep for ms then print "done"
//   flood <bytes>    write that many bytes of 'x' to stdout
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef __MINGW32__
// Disable MinGW's command-line wildcard expansion so '*' reaches main() unchanged.
int _CRT_glob = 0;
#endif

int main(int argc, char** argv) {
    if (argc < 2) return 64;
    if (std::strcmp(argv[1], "echo") == 0) {
        for (int i = 2; i < argc; ++i) std::printf("[%s]\n", argv[i]);
        return 0;
    }
    if (std::strcmp(argv[1], "exit") == 0 && argc == 3) return std::atoi(argv[2]);
    if (std::strcmp(argv[1], "err") == 0 && argc == 3) {
        std::fprintf(stderr, "%s", argv[2]);
        return 0;
    }
    if (std::strcmp(argv[1], "sleep") == 0 && argc == 3) {
        auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::atoi(argv[2]));
        while (std::chrono::steady_clock::now() < end) {
        }
        std::printf("done\n");
        return 0;
    }
    if (std::strcmp(argv[1], "flood") == 0 && argc == 3) {
        long n = std::atol(argv[2]);
        for (long i = 0; i < n; ++i) std::putchar('x');
        return 0;
    }
    return 65;
}

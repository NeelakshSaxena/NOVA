#include <iostream>
#include <string>
#include <vector>

#include "nova/cli.hpp"

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    return nova::run_cli(args, std::cout, std::cerr);
}

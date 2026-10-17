// Regression: bench::shuffle must never self-move an element. Built with checked standard containers
// (_GLIBCXX_DEBUG), where a self-move assignment aborts the program. Header-only dependencies.
#include <algorithm>
#include <string>
#include <vector>

#include "check.hpp"
#include "nova/bench/util.hpp"

struct Item {  // like tune::Candidate: a struct owning a vector, whose implicit move-assignment aborts on self-move in checked mode
    std::string id;
    std::vector<std::string> flags;
    bool operator<(const Item& o) const { return id < o.id; }
    bool operator==(const Item& o) const { return id == o.id && flags == o.flags; }
};

int main() {
    for (std::uint64_t seed = 0; seed < 300; ++seed) {
        nova::bench::Rng rng(seed);
        std::vector<Item> v;
        for (int i = 0; i < 12; ++i) v.push_back(Item{"candidate-" + std::to_string(i), {"-O2", "-funroll-loops"}});
        std::vector<Item> before = v;
        nova::bench::shuffle(v, rng);
        std::vector<Item> a = v, b = before;
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        CHECK(a == b);  // still a permutation
    }
    return nova_test::finish("shuffle-checked");
}

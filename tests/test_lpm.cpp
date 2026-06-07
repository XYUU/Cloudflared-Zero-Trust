// SPDX-License-Identifier: Apache-2.0
#include "cfd/lpm_trie.hpp"
#include <cassert>
#include <cstdio>

int main() {
    using namespace cfd::net;
    LpmTrie<int> t;
    t.insert(*Cidr::parse("10.0.0.0/8"),     1);
    t.insert(*Cidr::parse("10.99.0.0/16"),   2);
    t.insert(*Cidr::parse("10.99.5.0/24"),   3);

    auto v = t.lookup(*IpAddr::parse("10.99.5.7"));
    assert(v && *v == 3);
    v = t.lookup(*IpAddr::parse("10.99.6.7"));
    assert(v && *v == 2);
    v = t.lookup(*IpAddr::parse("10.50.0.1"));
    assert(v && *v == 1);
    v = t.lookup(*IpAddr::parse("11.0.0.1"));
    assert(!v);

    std::puts("OK");
    return 0;
}

// SPDX-License-Identifier: Apache-2.0
#include "cfd/cidr.hpp"
#include <cassert>
#include <cstdio>

int main() {
    using namespace cfd::net;
    auto c = Cidr::parse("10.99.0.0/16");
    assert(c.has_value());
    assert(c->prefix_len == 16);

    auto a1 = IpAddr::parse("10.99.5.7");
    auto a2 = IpAddr::parse("10.100.0.1");
    assert(a1 && a2);
    assert(c->contains(*a1));
    assert(!c->contains(*a2));

    auto v6 = Cidr::parse("2001:db8::/32");
    assert(v6 && v6->prefix_len == 32);

    assert(!Cidr::parse("garbage"));
    assert(!Cidr::parse("10.0.0.0/33"));

    std::puts("OK");
    return 0;
}

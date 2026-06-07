// SPDX-License-Identifier: Apache-2.0
#include "cfd/registration.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    using namespace cfd::tunnel;

    // UUID round-trip
    std::array<std::uint8_t, 16> u{};
    assert(parse_uuid("550e8400-e29b-41d4-a716-446655440000", u));
    assert(u[0] == 0x55 && u[1] == 0x0e && u[15] == 0x00);
    assert(!parse_uuid("not-a-uuid", u));
    assert(!parse_uuid("550e8400-e29b-41d4-a716-44665544000",  u));  // 35 chars
    assert(!parse_uuid("550e8400_e29b_41d4_a716_446655440000", u));  // wrong sep

    // base64
    std::vector<std::uint8_t> out;
    assert(base64_decode("AAECAwQF", out));
    const std::uint8_t expect[] = {0,1,2,3,4,5};
    assert(out.size() == 6 && std::memcmp(out.data(), expect, 6) == 0);

    out.clear();
    assert(base64_decode("aGVsbG8=", out));
    assert(out.size() == 5 && std::memcmp(out.data(), "hello", 5) == 0);

    out.clear();
    assert(!base64_decode("!!!!", out));        // bad char
    assert(!base64_decode("abc",  out));        // wrong length

    // Padding in wrong position must be rejected (regression: review #3).
    out.clear();
    assert(!base64_decode("====", out));        // all pad
    assert(!base64_decode("=AAA", out));        // leading pad
    assert(!base64_decode("A=AA", out));        // pad in middle
    assert(!base64_decode("AAA=AAA=", out));    // pad mid-input
    // But trailing 1- and 2-char pads are valid.
    out.clear();
    assert(base64_decode("AAAA", out));
    out.clear();
    assert(base64_decode("AA==", out) && out.size() == 1);
    out.clear();
    assert(base64_decode("AAA=", out) && out.size() == 2);

    std::puts("OK");
    return 0;
}

// SPDX-License-Identifier: Apache-2.0
//
// libFuzzer entry for Cidr::parse + LpmTrie::insert+lookup. Catches off-by-one
// bugs in the prefix matching and trie bit-walk.
#include "cfd/cidr.hpp"
#include "cfd/lpm_trie.hpp"
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using namespace cfd::net;

    // Split input: half as CIDR text, half as lookup-address bytes.
    if (size < 8) return 0;
    const std::size_t mid = size / 2;
    std::string_view cidr_text(reinterpret_cast<const char*>(data), mid);
    auto c = Cidr::parse(cidr_text);
    if (!c) return 0;

    LpmTrie<int> t;
    t.insert(*c, 1);

    IpAddr probe{};
    probe.family = (data[mid] & 1) ? Family::V6 : Family::V4;
    const std::size_t copy = (probe.family == Family::V4) ? 4u : 16u;
    const std::size_t avail = size - mid - 1;
    std::memcpy(probe.bytes.data(), data + mid + 1, std::min(copy, avail));

    (void)t.lookup(probe);
    (void)c->contains(probe);
    return 0;
}

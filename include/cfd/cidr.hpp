// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace cfd::net {

// Address family. We carry IPv4 in a v4-mapped style only at the API boundary;
// internally everything is 128-bit so the LPM trie has a single code path.
enum class Family : std::uint8_t { V4, V6 };

struct IpAddr {
    Family family{Family::V4};
    std::array<std::uint8_t, 16> bytes{};  // for V4, first 4 bytes used

    static std::optional<IpAddr> parse(std::string_view s) noexcept;
    std::string to_string() const;
};

struct Cidr {
    IpAddr addr{};
    std::uint8_t prefix_len{0};  // 0..32 for V4, 0..128 for V6

    static std::optional<Cidr> parse(std::string_view s) noexcept;
    std::string to_string() const;
    bool contains(const IpAddr& other) const noexcept;
};

}  // namespace cfd::net

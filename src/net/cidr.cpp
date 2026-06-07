// SPDX-License-Identifier: Apache-2.0
#include "cfd/cidr.hpp"
#include <arpa/inet.h>
#include <cstdio>
#include <cstring>

namespace cfd::net {

std::optional<IpAddr> IpAddr::parse(std::string_view s) noexcept {
    char buf[64];
    if (s.size() >= sizeof(buf)) return std::nullopt;
    std::memcpy(buf, s.data(), s.size());
    buf[s.size()] = '\0';

    IpAddr a{};
    if (::inet_pton(AF_INET, buf, a.bytes.data()) == 1) {
        a.family = Family::V4;
        return a;
    }
    if (::inet_pton(AF_INET6, buf, a.bytes.data()) == 1) {
        a.family = Family::V6;
        return a;
    }
    return std::nullopt;
}

std::string IpAddr::to_string() const {
    char buf[INET6_ADDRSTRLEN] = {};
    const int af = (family == Family::V4) ? AF_INET : AF_INET6;
    if (!::inet_ntop(af, bytes.data(), buf, sizeof(buf))) return {};
    return buf;
}

std::optional<Cidr> Cidr::parse(std::string_view s) noexcept {
    const auto slash = s.find('/');
    if (slash == std::string_view::npos) return std::nullopt;
    auto addr_part = s.substr(0, slash);
    auto plen_part = s.substr(slash + 1);

    auto a = IpAddr::parse(addr_part);
    if (!a) return std::nullopt;

    unsigned plen = 0;
    for (char c : plen_part) {
        if (c < '0' || c > '9') return std::nullopt;
        plen = plen * 10 + static_cast<unsigned>(c - '0');
        if (plen > 128) return std::nullopt;
    }
    const unsigned max = (a->family == Family::V4) ? 32u : 128u;
    if (plen > max) return std::nullopt;

    Cidr c{};
    c.addr = *a;
    c.prefix_len = static_cast<std::uint8_t>(plen);
    return c;
}

std::string Cidr::to_string() const {
    char buf[INET6_ADDRSTRLEN + 8];
    std::snprintf(buf, sizeof(buf), "%s/%u", addr.to_string().c_str(),
                  static_cast<unsigned>(prefix_len));
    return buf;
}

bool Cidr::contains(const IpAddr& other) const noexcept {
    if (other.family != addr.family) return false;
    const std::size_t bytes = prefix_len / 8;
    const std::uint8_t mask = static_cast<std::uint8_t>(
        0xFFu << (8u - (prefix_len % 8u)) & 0xFFu);
    for (std::size_t i = 0; i < bytes; ++i)
        if (addr.bytes[i] != other.bytes[i]) return false;
    if (prefix_len % 8) {
        const std::uint8_t m = mask;
        if ((addr.bytes[bytes] & m) != (other.bytes[bytes] & m)) return false;
    }
    return true;
}

}  // namespace cfd::net

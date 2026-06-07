// SPDX-License-Identifier: Apache-2.0
//
// Thin RTNETLINK client. Used to configure the TUN interface (address, MTU,
// link state) and install routes without shelling out to `ip`.
//
// Why custom code instead of libnl? libnl-3 is ~250 KB on musl/MIPS — bigger
// than this entire binary. Our needs are tiny (RTM_NEWADDR, RTM_NEWROUTE,
// RTM_NEWLINK with IFF_UP), so we hand-roll the messages.
//
// Memory model:
//   - All netlink messages live on the stack in a fixed-size scratch buffer.
//   - The AF_NETLINK socket is held by UniqueFd, closed on destruction.
//   - No allocations on the hot path; failure leaves no residual kernel state
//     beyond what the kernel already cleans up on socket close.
#pragma once

#include "cfd/cidr.hpp"
#include "cfd/unique_fd.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>

namespace cfd::netlink {

class Client {
public:
    Client() = default;
    Client(const Client&)            = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&) noexcept        = default;
    Client& operator=(Client&&) noexcept = default;

    // Open AF_NETLINK / NETLINK_ROUTE. Must be called before any operation.
    [[nodiscard]] std::error_code open();

    // Resolve an interface name to its ifindex.
    [[nodiscard]] std::error_code if_index(std::string_view name, int& out);

    // Bring an interface up (IFF_UP | IFF_RUNNING).
    [[nodiscard]] std::error_code link_up(int ifindex);

    // Set MTU on an interface.
    [[nodiscard]] std::error_code set_mtu(int ifindex, std::uint32_t mtu);

    // Assign an address with prefix length. IPv4 and IPv6 supported.
    [[nodiscard]] std::error_code add_addr(int ifindex, const net::Cidr& addr);

    // Install a route: destination CIDR via this device (no gateway).
    [[nodiscard]] std::error_code add_route(int ifindex, const net::Cidr& dst);

private:
    // Send `req` then read and validate the kernel's ACK. Returns the errno
    // the kernel reported (0 on success).
    [[nodiscard]] std::error_code talk(const void* req, std::size_t len);

    UniqueFd       fd_;
    std::uint32_t  seq_{0};
    std::uint32_t  pid_{0};
};

}  // namespace cfd::netlink

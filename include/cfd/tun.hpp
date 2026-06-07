// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "cfd/unique_fd.hpp"
#include "cfd/cidr.hpp"
#include <string>
#include <string_view>
#include <system_error>

namespace cfd::tun {

class TunDevice {
public:
    TunDevice() = default;
    TunDevice(const TunDevice&)            = delete;
    TunDevice& operator=(const TunDevice&) = delete;
    TunDevice(TunDevice&&) noexcept        = default;
    TunDevice& operator=(TunDevice&&) noexcept = default;

    // Open or create a TUN interface. `name` may be empty -- the kernel picks one.
    // Returns the actual interface name on success.
    [[nodiscard]] std::error_code open(std::string_view requested_name);

    // Bring the device up and assign a v4/v6 address + add a CIDR route over it.
    [[nodiscard]] std::error_code configure(const net::Cidr& local,
                                            const net::Cidr& route);

    // Install an additional CIDR pointing at this TUN. Safe to call after
    // configure(); EEXIST is treated as success.
    [[nodiscard]] std::error_code add_route(const net::Cidr& route);

    int fd() const noexcept { return fd_.get(); }
    const std::string& name() const noexcept { return name_; }

    // Blocking read/write of a single L3 packet.
    ssize_t read_packet(void* buf, std::size_t cap) noexcept;
    ssize_t write_packet(const void* buf, std::size_t len) noexcept;

private:
    UniqueFd     fd_;
    std::string  name_;
};

}  // namespace cfd::tun

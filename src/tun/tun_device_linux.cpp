// SPDX-License-Identifier: Apache-2.0
#include "cfd/tun.hpp"
#include "cfd/log.hpp"
#include "cfd/netlink.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#if defined(__linux__)
#include <net/if.h>        // ifreq — glibc header avoids conflicts with kernel headers
#include <linux/if_tun.h>  // TUNSETIFF, IFF_TUN, IFF_NO_PI
#endif

namespace cfd::tun {

namespace {
std::error_code last_errno() noexcept {
    return std::error_code(errno, std::generic_category());
}
}  // namespace

#if defined(__linux__)

std::error_code TunDevice::open(std::string_view requested_name) {
    UniqueFd fd(::open("/dev/net/tun", O_RDWR | O_CLOEXEC));
    if (!fd) {
        LOG_ERROR("open /dev/net/tun: %s", std::strerror(errno));
        return last_errno();
    }

    ifreq ifr{};
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
    if (!requested_name.empty()) {
        const std::size_t n = std::min(requested_name.size(), sizeof(ifr.ifr_name) - 1);
        std::memcpy(ifr.ifr_name, requested_name.data(), n);
        ifr.ifr_name[n] = '\0';
    }

    if (::ioctl(fd.get(), TUNSETIFF, &ifr) < 0) {
        LOG_ERROR("TUNSETIFF: %s", std::strerror(errno));
        return last_errno();
    }

    fd_   = std::move(fd);
    name_ = ifr.ifr_name;
    LOG_INFO("TUN opened: %s (fd=%d)", name_.c_str(), fd_.get());
    return {};
}

std::error_code TunDevice::configure(const net::Cidr& local, const net::Cidr& route) {
    // Configure entirely via RTNETLINK -- handles both IPv4 and IPv6 with the
    // same code path and removes the runtime dependency on iproute2.
    netlink::Client nl;
    if (auto ec = nl.open(); ec) return ec;

    int idx = 0;
    if (auto ec = nl.if_index(name_, idx); ec) return ec;
    if (auto ec = nl.set_mtu(idx, 1280); ec) {
        // Non-fatal: kernel default (1500) still works for most paths.
        LOG_DEBUG("set_mtu failed (non-fatal): %s", ec.message().c_str());
    }
    if (auto ec = nl.link_up(idx); ec) return ec;
    if (auto ec = nl.add_addr(idx, local); ec) return ec;
    if (auto ec = nl.add_route(idx, route); ec) {
        // EEXIST is fine (e.g. process restart). Anything else propagates.
        if (ec.value() != EEXIST) return ec;
    }

    LOG_INFO("TUN %s configured via netlink: local=%s route=%s ifindex=%d",
             name_.c_str(), local.to_string().c_str(),
             route.to_string().c_str(), idx);
    return {};
}

std::error_code TunDevice::add_route(const net::Cidr& route) {
    netlink::Client nl;
    if (auto ec = nl.open(); ec) return ec;
    int idx = 0;
    if (auto ec = nl.if_index(name_, idx); ec) return ec;
    if (auto ec = nl.add_route(idx, route); ec && ec.value() != EEXIST) return ec;
    LOG_INFO("route %s -> %s installed", route.to_string().c_str(), name_.c_str());
    return {};
}

ssize_t TunDevice::read_packet(void* buf, std::size_t cap) noexcept {
    return ::read(fd_.get(), buf, cap);
}
ssize_t TunDevice::write_packet(const void* buf, std::size_t len) noexcept {
    return ::write(fd_.get(), buf, len);
}

#else  // non-Linux stub

std::error_code TunDevice::open(std::string_view)                      { return std::make_error_code(std::errc::not_supported); }
std::error_code TunDevice::configure(const net::Cidr&, const net::Cidr&) { return std::make_error_code(std::errc::not_supported); }
std::error_code TunDevice::add_route(const net::Cidr&)                  { return std::make_error_code(std::errc::not_supported); }
ssize_t TunDevice::read_packet(void*, std::size_t) noexcept              { errno = ENOSYS; return -1; }
ssize_t TunDevice::write_packet(const void*, std::size_t) noexcept       { errno = ENOSYS; return -1; }

#endif

}  // namespace cfd::tun

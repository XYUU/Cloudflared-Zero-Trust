// SPDX-License-Identifier: Apache-2.0
#include "cfd/netlink.hpp"
#include "cfd/log.hpp"

#if defined(__linux__)
#  include <linux/netlink.h>
#  include <linux/rtnetlink.h>
#  include <net/if.h>
#  include <sys/socket.h>
#endif

#include <cerrno>
#include <cstring>
#include <unistd.h>

namespace cfd::netlink {

namespace {

std::error_code last_errno() noexcept {
    return std::error_code(errno, std::generic_category());
}

#if defined(__linux__)

// Append a single RTA attribute into a netlink message at `nlh`.
// Returns false if the message would exceed `cap` bytes.
bool nla_put(nlmsghdr* nlh, std::size_t cap, std::uint16_t type,
             const void* data, std::size_t len) noexcept {
    const std::size_t aligned = NLMSG_ALIGN(nlh->nlmsg_len);
    const std::size_t need    = aligned + RTA_LENGTH(len);
    if (need > cap) return false;
    auto* rta = reinterpret_cast<rtattr*>(
        reinterpret_cast<char*>(nlh) + aligned);
    rta->rta_type = type;
    rta->rta_len  = static_cast<std::uint16_t>(RTA_LENGTH(len));
    std::memcpy(RTA_DATA(rta), data, len);
    nlh->nlmsg_len = static_cast<std::uint32_t>(need);
    return true;
}

constexpr std::size_t kScratch = 4096;  // plenty for the messages we send

#endif  // __linux__

}  // namespace

#if defined(__linux__)

std::error_code Client::open() {
    UniqueFd s(::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE));
    if (!s) return last_errno();

    sockaddr_nl sa{};
    sa.nl_family = AF_NETLINK;
    sa.nl_pid    = 0;     // let kernel assign
    if (::bind(s.get(), reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) < 0)
        return last_errno();

    socklen_t sl = sizeof(sa);
    if (::getsockname(s.get(), reinterpret_cast<sockaddr*>(&sa), &sl) < 0)
        return last_errno();

    fd_  = std::move(s);
    pid_ = sa.nl_pid;
    return {};
}

std::error_code Client::talk(const void* req, std::size_t len) {
    if (::send(fd_.get(), req, len, 0) < 0) return last_errno();

    // ACK is a NLMSG_ERROR carrying errno=0 on success, non-zero on failure.
    alignas(NLMSG_ALIGNTO) char buf[kScratch];
    const ssize_t n = ::recv(fd_.get(), buf, sizeof(buf), 0);
    if (n < 0) return last_errno();

    auto* nlh = reinterpret_cast<nlmsghdr*>(buf);
    if (!NLMSG_OK(nlh, static_cast<std::uint32_t>(n)))
        return std::make_error_code(std::errc::protocol_error);
    if (nlh->nlmsg_type != NLMSG_ERROR)
        return std::make_error_code(std::errc::protocol_error);

    auto* err = static_cast<nlmsgerr*>(NLMSG_DATA(nlh));
    if (err->error == 0) return {};
    return std::error_code(-err->error, std::generic_category());
}

std::error_code Client::if_index(std::string_view name, int& out) {
    if (name.size() >= IFNAMSIZ) return std::make_error_code(std::errc::invalid_argument);
    char tmp[IFNAMSIZ] = {};
    std::memcpy(tmp, name.data(), name.size());
    const unsigned idx = ::if_nametoindex(tmp);
    if (idx == 0) return last_errno();
    out = static_cast<int>(idx);
    return {};
}

std::error_code Client::link_up(int ifindex) {
    alignas(NLMSG_ALIGNTO) char buf[kScratch] = {};
    auto* nlh = reinterpret_cast<nlmsghdr*>(buf);
    nlh->nlmsg_len   = NLMSG_LENGTH(sizeof(ifinfomsg));
    nlh->nlmsg_type  = RTM_NEWLINK;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    nlh->nlmsg_seq   = ++seq_;
    nlh->nlmsg_pid   = pid_;
    auto* ifi = static_cast<ifinfomsg*>(NLMSG_DATA(nlh));
    ifi->ifi_family = AF_UNSPEC;
    ifi->ifi_index  = ifindex;
    ifi->ifi_flags  = IFF_UP | IFF_RUNNING;
    ifi->ifi_change = IFF_UP | IFF_RUNNING;
    return talk(buf, nlh->nlmsg_len);
}

std::error_code Client::set_mtu(int ifindex, std::uint32_t mtu) {
    alignas(NLMSG_ALIGNTO) char buf[kScratch] = {};
    auto* nlh = reinterpret_cast<nlmsghdr*>(buf);
    nlh->nlmsg_len   = NLMSG_LENGTH(sizeof(ifinfomsg));
    nlh->nlmsg_type  = RTM_NEWLINK;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    nlh->nlmsg_seq   = ++seq_;
    nlh->nlmsg_pid   = pid_;
    auto* ifi = static_cast<ifinfomsg*>(NLMSG_DATA(nlh));
    ifi->ifi_family = AF_UNSPEC;
    ifi->ifi_index  = ifindex;
    if (!nla_put(nlh, sizeof(buf), IFLA_MTU, &mtu, sizeof(mtu)))
        return std::make_error_code(std::errc::message_size);
    return talk(buf, nlh->nlmsg_len);
}

std::error_code Client::add_addr(int ifindex, const net::Cidr& addr) {
    alignas(NLMSG_ALIGNTO) char buf[kScratch] = {};
    auto* nlh = reinterpret_cast<nlmsghdr*>(buf);
    nlh->nlmsg_len   = NLMSG_LENGTH(sizeof(ifaddrmsg));
    nlh->nlmsg_type  = RTM_NEWADDR;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_REPLACE;
    nlh->nlmsg_seq   = ++seq_;
    nlh->nlmsg_pid   = pid_;

    auto* ifa = static_cast<ifaddrmsg*>(NLMSG_DATA(nlh));
    ifa->ifa_family    = (addr.addr.family == net::Family::V4) ? AF_INET : AF_INET6;
    ifa->ifa_prefixlen = addr.prefix_len;
    ifa->ifa_flags     = 0;
    ifa->ifa_scope     = 0;   // RT_SCOPE_UNIVERSE
    ifa->ifa_index     = static_cast<std::uint32_t>(ifindex);

    const std::size_t alen = (addr.addr.family == net::Family::V4) ? 4 : 16;
    if (!nla_put(nlh, sizeof(buf), IFA_LOCAL,   addr.addr.bytes.data(), alen) ||
        !nla_put(nlh, sizeof(buf), IFA_ADDRESS, addr.addr.bytes.data(), alen))
        return std::make_error_code(std::errc::message_size);

    return talk(buf, nlh->nlmsg_len);
}

std::error_code Client::add_route(int ifindex, const net::Cidr& dst) {
    alignas(NLMSG_ALIGNTO) char buf[kScratch] = {};
    auto* nlh = reinterpret_cast<nlmsghdr*>(buf);
    nlh->nlmsg_len   = NLMSG_LENGTH(sizeof(rtmsg));
    nlh->nlmsg_type  = RTM_NEWROUTE;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_REPLACE;
    nlh->nlmsg_seq   = ++seq_;
    nlh->nlmsg_pid   = pid_;

    auto* rtm = static_cast<rtmsg*>(NLMSG_DATA(nlh));
    rtm->rtm_family   = (dst.addr.family == net::Family::V4) ? AF_INET : AF_INET6;
    rtm->rtm_dst_len  = dst.prefix_len;
    rtm->rtm_table    = RT_TABLE_MAIN;
    rtm->rtm_protocol = RTPROT_BOOT;
    rtm->rtm_scope    = RT_SCOPE_LINK;
    rtm->rtm_type     = RTN_UNICAST;

    const std::size_t alen = (dst.addr.family == net::Family::V4) ? 4 : 16;
    const std::uint32_t oif = static_cast<std::uint32_t>(ifindex);
    if (!nla_put(nlh, sizeof(buf), RTA_DST, dst.addr.bytes.data(), alen) ||
        !nla_put(nlh, sizeof(buf), RTA_OIF, &oif, sizeof(oif)))
        return std::make_error_code(std::errc::message_size);

    return talk(buf, nlh->nlmsg_len);
}

#else  // ---------- non-Linux stub ----------

std::error_code Client::open()                                          { return std::make_error_code(std::errc::not_supported); }
std::error_code Client::if_index(std::string_view, int&)               { return std::make_error_code(std::errc::not_supported); }
std::error_code Client::link_up(int)                                    { return std::make_error_code(std::errc::not_supported); }
std::error_code Client::set_mtu(int, std::uint32_t)                    { return std::make_error_code(std::errc::not_supported); }
std::error_code Client::add_addr(int, const net::Cidr&)                { return std::make_error_code(std::errc::not_supported); }
std::error_code Client::add_route(int, const net::Cidr&)               { return std::make_error_code(std::errc::not_supported); }
std::error_code Client::talk(const void*, std::size_t)                 { return std::make_error_code(std::errc::not_supported); }

#endif

}  // namespace cfd::netlink

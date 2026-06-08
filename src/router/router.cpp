// SPDX-License-Identifier: Apache-2.0
#include "cfd/router.hpp"
#include "cfd/buffer.hpp"
#include "cfd/log.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <utility>

namespace cfd::router {

namespace {
constexpr std::size_t kPacketMtu = 1500;

bool dst_from_packet(const std::uint8_t* p, std::size_t len, net::IpAddr& out) noexcept {
    if (len < 1) return false;
    const std::uint8_t version = p[0] >> 4u;
    if (version == 4) {
        if (len < 20) return false;
        out.family = net::Family::V4;
        std::memset(out.bytes.data(), 0, out.bytes.size());
        std::memcpy(out.bytes.data(), p + 16, 4);
        return true;
    }
    if (version == 6) {
        if (len < 40) return false;
        out.family = net::Family::V6;
        std::memcpy(out.bytes.data(), p + 24, 16);
        return true;
    }
    return false;
}
}  // namespace

Router::Router(std::shared_ptr<tun::TunDevice> tun,
               std::shared_ptr<IPacketSink>    tunnel_sink)
    : tun_(std::move(tun)), tunnel_(std::move(tunnel_sink)) {}

Router::~Router() { stop(); }

void Router::add_route(const net::Cidr& c, Action a) {
    routes_.insert(c, a);
    LOG_INFO("route added: %s -> %u", c.to_string().c_str(), static_cast<unsigned>(a));
}

void Router::start() {
    bool was = false;
    if (!running_.compare_exchange_strong(was, true)) return;

    int p[2] = {-1, -1};
#if defined(__linux__)
    if (::pipe2(p, O_CLOEXEC | O_NONBLOCK) < 0) {
        LOG_ERROR("router: pipe2 failed errno=%d", errno);
        running_.store(false);
        return;
    }
#else
    if (::pipe(p) < 0) {
        LOG_ERROR("router: pipe failed errno=%d", errno);
        running_.store(false);
        return;
    }
#endif
    wake_r_.reset(p[0]);
    wake_w_.reset(p[1]);

    reader_ = std::thread([this] { tun_reader_loop(); });
}

void Router::stop() noexcept {
    if (!running_.exchange(false)) return;
    // Wake the reader: write one byte to the pipe; reader's poll() returns
    // and the loop sees running_==false on the next iteration. Closing the
    // TUN fd would be racy because the TunDevice is shared and its dtor owns
    // the close.
    if (wake_w_.valid()) {
        const char b = 1;
        // Best-effort -- if the write fails the reader will eventually exit
        // through its idle timeout (set in poll() below).
        [[maybe_unused]] auto _wr = ::write(wake_w_.get(), &b, 1);
    }
    if (reader_.joinable()) reader_.join();
    wake_w_.reset();
    wake_r_.reset();
}

void Router::tun_reader_loop() noexcept {
    Buffer buf(kPacketMtu);
    while (running_.load(std::memory_order_relaxed)) {
        pollfd pfds[2] = {
            {tun_->fd(),     POLLIN, 0},
            {wake_r_.get(),  POLLIN, 0},
        };
        // 1s timeout is a belt-and-braces backstop in case both fds wedge.
        const int rc = ::poll(pfds, 2, 1'000);
        if (rc < 0) {
            if (errno == EINTR) continue;
            LOG_WARN("router: poll errno=%d", errno);
            break;
        }
        if (rc == 0) continue;                         // timeout, re-check running_
        if (pfds[1].revents & POLLIN) break;            // wake -> exit

        const ssize_t n = tun_->read_packet(buf.data(), buf.capacity());
        if (n <= 0) {
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            LOG_WARN("tun read returned %zd errno=%d", n, errno);
            break;
        }
        net::IpAddr dst{};
        if (!dst_from_packet(buf.data(), static_cast<std::size_t>(n), dst)) continue;

        auto act = routes_.lookup(dst);
        if (!act || *act == Action::Drop) continue;

        if (*act == Action::ToTunnel && tunnel_)
            tunnel_->on_packet(buf.data(), static_cast<std::size_t>(n));
    }
}

void Router::inject_from_tunnel(const std::uint8_t* data, std::size_t len) noexcept {
    if (!tun_) return;
    const ssize_t w = tun_->write_packet(data, len);
    if (w < 0) LOG_WARN("tun write failed errno=%d", errno);
}

}  // namespace cfd::router

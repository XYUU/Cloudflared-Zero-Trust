// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "cfd/cidr.hpp"
#include "cfd/lpm_trie.hpp"
#include "cfd/tun.hpp"
#include "cfd/unique_fd.hpp"
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace cfd::router {

// Policy returned by the LPM trie: where should a packet go?
enum class Action : std::uint8_t {
    Drop,
    ToTunnel,    // forward to Cloudflare edge via QUIC datagram
    ToTun,       // forward to local TUN (peer is on-link)
};

class IPacketSink {
public:
    virtual ~IPacketSink() = default;
    // Take ownership-free view of the packet. Implementations MUST copy if
    // they need to outlive the call.
    virtual void on_packet(const std::uint8_t* data, std::size_t len) noexcept = 0;
};

class Router {
public:
    Router(std::shared_ptr<tun::TunDevice> tun,
           std::shared_ptr<IPacketSink>    tunnel_sink);

    Router(const Router&)            = delete;
    Router& operator=(const Router&) = delete;
    ~Router();

    void add_route(const net::Cidr& cidr, Action action);

    void start();
    void stop() noexcept;

    // Inject a packet received from the tunnel into the TUN device.
    void inject_from_tunnel(const std::uint8_t* data, std::size_t len) noexcept;

private:
    void tun_reader_loop() noexcept;

    std::shared_ptr<tun::TunDevice> tun_;
    std::shared_ptr<IPacketSink>    tunnel_;
    net::LpmTrie<Action>            routes_;

    std::atomic<bool>               running_{false};
    std::thread                     reader_;

    // Wake pipe: stop() writes one byte to wake_w_, the reader's poll() returns
    // immediately, the loop notices running_==false and exits. We do NOT close
    // the TUN fd from stop() because the TUN is shared and the fd's lifetime
    // belongs to TunDevice's RAII -- closing it here would race the dtor.
    UniqueFd                        wake_r_;
    UniqueFd                        wake_w_;
};

}  // namespace cfd::router

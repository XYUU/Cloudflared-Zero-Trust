// SPDX-License-Identifier: Apache-2.0
//
// cfd: lightweight C++ Cloudflare Tunnel client for embedded routers.
//
// Lifecycle:
//   1. Parse config (--config <path>)
//   2. Open and configure TUN device
//   3. Resolve edge IPs; create NUM_HA_CONNS QuicClient instances
//   4. Build router with round-robin multi-sink over all tunnel connections
//   5. Connect + RegisterConnection in parallel for each connection
//   6. Start packet pump (TUN -> Router -> QUIC, QUIC datagram -> TUN)
//   7. Wait for SIGINT/SIGTERM, then orderly teardown

#include "cfd/config.hpp"
#include "cfd/log.hpp"
#include "cfd/quic_client.hpp"
#include "cfd/router.hpp"
#include "cfd/tun.hpp"

#include <arpa/inet.h>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <netdb.h>
#include <string>
#include <thread>
#include <vector>

// Number of parallel HA connections cloudflared maintains per tunnel.
// Cloudflare marks a tunnel HEALTHY only when all NUM_HA_CONNS are active.
static constexpr int kNumHaConns = 5;

namespace {

std::atomic<bool> g_stop{false};

void on_signal(int) noexcept { g_stop.store(true, std::memory_order_relaxed); }

void install_signals() {
    struct sigaction sa{};
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    ::sigaction(SIGINT,  &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);
    std::signal(SIGPIPE, SIG_IGN);
}

void print_usage() {
    std::fputs(
        "usage: cfd --config <path> [--verbose]\n"
        "\n"
        "Required keys in config (KV format):\n"
        "  tunnel_id, account_tag, tunnel_secret_b64\n"
        "  tun_local (e.g. 10.99.0.1/32)\n"
        "  one or more 'route = <CIDR>' lines\n",
        stderr);
}

// Resolve `host` via DNS and return up to `want` numeric address strings
// (IPv4 or IPv6).  If fewer are returned, cycle the list to reach `want`.
// Falls back to [{host}] if DNS fails entirely.
std::vector<std::string> resolve_edge_ips(const std::string& host,
                                          std::uint16_t port,
                                          int want) {
    addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res     = nullptr;
    const std::string svc = std::to_string(port);

    std::vector<std::string> found;
    if (::getaddrinfo(host.c_str(), svc.c_str(), &hints, &res) == 0 && res) {
        for (addrinfo* a = res; a; a = a->ai_next) {
            char buf[INET6_ADDRSTRLEN]{};
            if (a->ai_family == AF_INET)
                ::inet_ntop(AF_INET,
                    &reinterpret_cast<sockaddr_in*>(a->ai_addr)->sin_addr,
                    buf, sizeof buf);
            else if (a->ai_family == AF_INET6)
                ::inet_ntop(AF_INET6,
                    &reinterpret_cast<sockaddr_in6*>(a->ai_addr)->sin6_addr,
                    buf, sizeof buf);
            if (buf[0]) found.push_back(buf);
        }
        ::freeaddrinfo(res);
    }

    if (found.empty()) {
        LOG_WARN("DNS lookup for %s failed; using hostname directly", host.c_str());
        found.push_back(host);
    }

    // Build a result of exactly `want` entries by cycling through what we got.
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(want));
    for (int i = 0; i < want; ++i)
        out.push_back(found[static_cast<std::size_t>(i) % found.size()]);
    return out;
}

// Round-robin sink: distributes outbound packets across N tunnel connections.
// Closed/failed QuicClients drop packets silently in their on_packet() guard,
// so unhealthy connections have no correctness impact — only efficiency.
class MultiSink final : public cfd::router::IPacketSink {
    std::vector<std::shared_ptr<cfd::router::IPacketSink>> sinks_;
    std::atomic<std::size_t>                               next_{0};
public:
    explicit MultiSink(std::vector<std::shared_ptr<cfd::router::IPacketSink>> s)
        : sinks_(std::move(s)) {}

    void on_packet(const std::uint8_t* d, std::size_t n) noexcept override {
        if (sinks_.empty()) return;
        const std::size_t idx =
            next_.fetch_add(1, std::memory_order_relaxed) % sinks_.size();
        sinks_[idx]->on_packet(d, n);
    }
};

}  // namespace

int main(int argc, char** argv) {
    std::string cfg_path;
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--config" && i + 1 < argc) cfg_path = argv[++i];
        else if (a == "--verbose")                 verbose  = true;
        else if (a == "--help" || a == "-h")       { print_usage(); return 0; }
        else                                       { print_usage(); return 2; }
    }
    if (cfg_path.empty()) { print_usage(); return 2; }

    cfd::log::set_level(verbose ? cfd::log::Level::Debug : cfd::log::Level::Info);

    cfd::Config cfg;
    std::string err;
    if (!cfd::Config::load_from_file(cfg_path, cfg, err)) {
        LOG_ERROR("config: %s", err.c_str());
        return 1;
    }
    LOG_INFO("tunnel_id=%s routes=%zu",
             cfg.tunnel_id.c_str(), cfg.routed_cidrs.size());

    install_signals();

    // ---- TUN device ---------------------------------------------------------
    auto tun = std::make_shared<cfd::tun::TunDevice>();
    if (auto ec = tun->open(cfg.tun_name); ec) {
        LOG_ERROR("tun open: %s", ec.message().c_str());
        return 1;
    }
    if (auto ec = tun->configure(cfg.tun_local,
                                 cfg.routed_cidrs.empty()
                                     ? cfg.tun_local
                                     : cfg.routed_cidrs.front());
        ec) {
        LOG_ERROR("tun configure: %s", ec.message().c_str());
        return 1;
    }
    for (std::size_t i = 1; i < cfg.routed_cidrs.size(); ++i) {
        if (auto ec = tun->add_route(cfg.routed_cidrs[i]); ec) {
            LOG_ERROR("add_route %s: %s",
                      cfg.routed_cidrs[i].to_string().c_str(),
                      ec.message().c_str());
            return 1;
        }
    }

    // ---- Resolve edge IPs ---------------------------------------------------
    // Cloudflare expects NUM_HA_CONNS connections, ideally to different PoPs.
    // We resolve all A/AAAA records for edge_host and distribute one IP per
    // connection; if DNS returns fewer than NUM_HA_CONNS we cycle the list.
    const auto edge_ips = resolve_edge_ips(cfg.edge_host, cfg.edge_port,
                                           kNumHaConns);
    LOG_INFO("edge resolved: %zu unique IP(s) for %d connections",
             edge_ips.size(), kNumHaConns);

    // ---- Build RegisterRequest template ------------------------------------
    cfd::tunnel::RegisterRequest rreq_tmpl;
    if (!cfd::tunnel::parse_uuid(cfg.tunnel_id, rreq_tmpl.tunnel_uuid)) {
        LOG_ERROR("bad tunnel_id format: %s", cfg.tunnel_id.c_str());
        return 1;
    }
    rreq_tmpl.auth.account_tag = cfg.account_tag;
    if (!cfd::tunnel::base64_decode(cfg.tunnel_secret_b64,
                                    rreq_tmpl.auth.tunnel_secret)) {
        LOG_ERROR("bad tunnel_secret_b64");
        return 1;
    }
    rreq_tmpl.arch     = "linux";
    rreq_tmpl.features = {"quic-datagram-v3"};

    // ---- Create NUM_HA_CONNS QuicClient instances ---------------------------
    std::vector<std::shared_ptr<cfd::tunnel::QuicClient>> clients;
    clients.reserve(static_cast<std::size_t>(kNumHaConns));
    for (int i = 0; i < kNumHaConns; ++i) {
        cfd::tunnel::QuicConfig qcfg;
        qcfg.edge_host        = edge_ips[static_cast<std::size_t>(i)];
        qcfg.edge_port        = cfg.edge_port;
        qcfg.ca_bundle_path   = cfg.ca_bundle_path;
        qcfg.client_cert_path = cfg.client_cert_path;
        qcfg.client_key_path  = cfg.client_key_path;
        clients.push_back(
            std::make_shared<cfd::tunnel::QuicClient>(std::move(qcfg)));
    }

    // ---- Router with round-robin multi-sink ---------------------------------
    {
        std::vector<std::shared_ptr<cfd::router::IPacketSink>> sinks(
            clients.begin(), clients.end());
        auto sink = std::make_shared<MultiSink>(std::move(sinks));

        auto router = std::make_unique<cfd::router::Router>(tun, sink);
        for (const auto& c : cfg.routed_cidrs)
            router->add_route(c, cfd::router::Action::ToTunnel);

        // Inbound: every client delivers into the same router.
        for (auto& cl : clients) {
            cl->set_inbound_handler(
                [r = router.get()](const std::uint8_t* d,
                                   std::size_t n) noexcept {
                    r->inject_from_tunnel(d, n);
                });
        }

        // ---- Phase 1: connect NUM_HA_CONNS in parallel (QUIC handshake) ------
        // register_connection() runs a KJ event loop per call; creating
        // multiple simultaneous KJ loops from different threads is fragile.
        // We therefore split the work: parallel connect (pure msquic, safe),
        // then sequential register on the main thread (single KJ loop at a time).
        // Use int, not bool: vector<bool> packs bits and concurrent writes to
        // adjacent elements race on the same byte even when indices differ.
        std::vector<int> connected(static_cast<std::size_t>(kNumHaConns), 0);
        {
            std::vector<std::thread> ts;
            ts.reserve(static_cast<std::size_t>(kNumHaConns));
            for (int i = 0; i < kNumHaConns; ++i) {
                ts.emplace_back([&, i]() {
                    if (auto ec = clients[static_cast<std::size_t>(i)]->connect();
                        ec) {
                        LOG_WARN("conn[%d] connect failed: %s",
                                 i, ec.message().c_str());
                    } else {
                        connected[static_cast<std::size_t>(i)] = 1;
                    }
                });
            }
            for (auto& t : ts) t.join();
            // After join(): all writes by each connect thread are visible here
            // (std::thread::join() provides a happens-before guarantee).
        }

        // ---- Phase 2: register sequentially on the main thread --------------
        // Each register_connection() call drives a fresh KJ event loop to
        // completion before the next starts — no concurrent KJ loops.
        std::atomic<int> ok_count{0};
        for (int i = 0; i < kNumHaConns; ++i) {
            if (!connected[static_cast<std::size_t>(i)]) continue;  // 0 = failed
            auto& cl = clients[static_cast<std::size_t>(i)];

            cfd::tunnel::RegisterRequest rreq = rreq_tmpl;
            rreq.conn_index = static_cast<std::uint8_t>(i);

            cfd::tunnel::RegisterResponse rresp;
            if (auto ec = cl->register_connection(rreq, rresp, 15'000); ec) {
                LOG_WARN("conn[%d] register failed: %s",
                         i, ec.message().c_str());
                cl->close();
                continue;
            }

            LOG_INFO("conn[%d] registered at %s",
                     i, rresp.location_name.c_str());
            ok_count.fetch_add(1, std::memory_order_relaxed);
        }

        if (ok_count.load() == 0) {
            LOG_ERROR("all %d connections failed; aborting", kNumHaConns);
            return 1;
        }
        LOG_INFO("tunnel up: %d/%d connections healthy",
                 ok_count.load(), kNumHaConns);

        // ---- Packet pump ----------------------------------------------------
        router->start();
        LOG_INFO("cfd running. Ctrl-C to stop.");

        while (!g_stop.load(std::memory_order_relaxed))
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // ---- Orderly teardown -----------------------------------------------
        LOG_INFO("shutting down");
        router->stop();
        for (auto& cl : clients) cl->close();
        // router destroyed here (UniquePtr dtor), then clients, then tun.
    }
    return 0;
}

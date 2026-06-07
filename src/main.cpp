// SPDX-License-Identifier: Apache-2.0
//
// cfd: lightweight C++ Cloudflare Tunnel client for embedded routers.
//
// Lifecycle:
//   1. Parse config (--config <path>)
//   2. Open and configure TUN device
//   3. Build router and install CIDR routes
//   4. Open QUIC tunnel to Cloudflare edge, register connection
//   5. Start packet pump (TUN -> Router -> QUIC, QUIC datagram -> TUN)
//   6. Wait for SIGINT/SIGTERM, then orderly teardown
//
// Memory: all heap allocations are owned by std::unique_ptr / std::shared_ptr
// and freed deterministically on scope exit. No naked new/delete in app code.

#include "cfd/config.hpp"
#include "cfd/log.hpp"
#include "cfd/quic_client.hpp"
#include "cfd/router.hpp"
#include "cfd/tun.hpp"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_stop{false};

void on_signal(int) noexcept { g_stop.store(true, std::memory_order_relaxed); }

void install_signals() {
    struct sigaction sa{};
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    ::sigaction(SIGINT,  &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);
    // Ignore SIGPIPE — we get EPIPE from writes instead.
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

}  // namespace

int main(int argc, char** argv) {
    std::string cfg_path;
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--config" && i + 1 < argc) cfg_path = argv[++i];
        else if (a == "--verbose")          verbose = true;
        else if (a == "--help" || a == "-h") { print_usage(); return 0; }
        else { print_usage(); return 2; }
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

    // --- TUN ---
    auto tun = std::make_shared<cfd::tun::TunDevice>();
    if (auto ec = tun->open(cfg.tun_name); ec) {
        LOG_ERROR("tun open: %s", ec.message().c_str());
        return 1;
    }
    if (auto ec = tun->configure(cfg.tun_local, cfg.routed_cidrs.empty()
                                                    ? cfg.tun_local
                                                    : cfg.routed_cidrs.front());
        ec) {
        LOG_ERROR("tun configure: %s", ec.message().c_str());
        return 1;
    }
    // Install the remaining CIDRs (configure() installed the first).
    for (std::size_t i = 1; i < cfg.routed_cidrs.size(); ++i) {
        if (auto ec = tun->add_route(cfg.routed_cidrs[i]); ec) {
            LOG_ERROR("add_route %s: %s",
                      cfg.routed_cidrs[i].to_string().c_str(), ec.message().c_str());
            return 1;
        }
    }

    // --- QUIC tunnel ---
    cfd::tunnel::QuicConfig qcfg;
    qcfg.edge_host  = cfg.edge_host;
    qcfg.edge_port  = cfg.edge_port;
    qcfg.tunnel_id  = cfg.tunnel_id;
    qcfg.account_tag = cfg.account_tag;
    // base64 decode of tunnel_secret_b64 elided in skeleton.
    auto qc = std::make_shared<cfd::tunnel::QuicClient>(std::move(qcfg));
    if (auto ec = qc->connect(); ec) {
        LOG_WARN("QUIC connect: %s (continuing in degraded mode)", ec.message().c_str());
    } else {
        // After the QUIC handshake succeeds, register this connection with
        // the edge so it starts steering CIDR traffic to our tunnel.
        cfd::tunnel::RegisterRequest rreq;
        if (!cfd::tunnel::parse_uuid(cfg.tunnel_id, rreq.tunnel_uuid)) {
            LOG_ERROR("bad tunnel_id format: %s", cfg.tunnel_id.c_str());
            return 1;
        }
        rreq.auth.account_tag = cfg.account_tag;
        if (!cfd::tunnel::base64_decode(cfg.tunnel_secret_b64, rreq.auth.tunnel_secret)) {
            LOG_ERROR("bad tunnel_secret_b64");
            return 1;
        }
        rreq.conn_index = 0;
        rreq.arch       = "linux";   // refined per-target by CMake later
        rreq.features   = {"quic-datagram-v3"};

        cfd::tunnel::RegisterResponse rresp;
        if (auto ec = qc->register_connection(rreq, rresp, 10'000); ec) {
            LOG_ERROR("RegisterConnection: %s", ec.message().c_str());
            return 1;
        }
        LOG_INFO("registered at edge location=%s", rresp.location_name.c_str());
    }

    // --- Router ---
    auto router = std::make_unique<cfd::router::Router>(tun, qc);
    for (const auto& c : cfg.routed_cidrs) {
        router->add_route(c, cfd::router::Action::ToTunnel);
    }

    qc->set_inbound_handler(
        [r = router.get()](const std::uint8_t* d, std::size_t n) noexcept {
            r->inject_from_tunnel(d, n);
        });

    router->start();
    LOG_INFO("cfd running. Ctrl-C to stop.");

    while (!g_stop.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    LOG_INFO("shutting down");
    router->stop();
    qc->close();
    // tun closed by shared_ptr / UniqueFd
    return 0;
}

// SPDX-License-Identifier: Apache-2.0
//
// cfd_probe — verify QUIC connectivity to a Cloudflare edge.
//
// What it does:
//   1. MsQuicOpen + RegistrationOpen
//   2. ConfigurationOpen with ALPN "argotunnel" (override with --alpn)
//   3. ConnectionStart to <host>:<port>
//   4. Block until CONNECTED or SHUTDOWN, print outcome + elapsed ms
//   5. Clean teardown (UniqueHandle dtors close everything)
//
// Exit codes:
//   0 — handshake succeeded
//   1 — handshake failed (transport error, ALPN mismatch, etc.)
//   2 — usage / configuration error
//
// Memory: same RAII discipline as QuicClient — no leaks even on early return.

#include "cfd/config.hpp"
#include "cfd/log.hpp"
#include "cfd/quic_client.hpp"
#include "cfd/registration.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

void usage() {
    std::fputs(
        "usage: cfd_probe --host <edge> [--port 7844] [--alpn argotunnel]\n"
        "                 [--server-name quic.cftunnel.com]\n"
        "                 [--cert <pem>] [--key <pem>]\n"
        "                 [--insecure] [--timeout-ms 10000]\n"
        "                 [--register --config <cfd.ini>]\n"
        "\n"
        "Examples:\n"
        "  cfd_probe --host region1.argotunnel.com\n"
        "  cfd_probe --host region1.argotunnel.com --register --config cfd.ini\n",
        stderr);
}

bool eq(const char* a, const char* b) noexcept { return std::strcmp(a, b) == 0; }

}  // namespace

int main(int argc, char** argv) {
    std::string host;
    std::uint16_t port      = 7844;
    std::string alpn        = "argotunnel";
    std::string server_name = "quic.cftunnel.com";  // TLS SNI, per cloudflared
    std::string ca_bundle, cert, key;
    std::string config_path;
    bool insecure           = false;
    bool do_register        = false;
    int  timeout_ms         = 10'000;

    for (int i = 1; i < argc; ++i) {
        if      (eq(argv[i], "--host")        && i + 1 < argc) host = argv[++i];
        else if (eq(argv[i], "--port")        && i + 1 < argc) port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        else if (eq(argv[i], "--alpn")        && i + 1 < argc) alpn = argv[++i];
        else if (eq(argv[i], "--server-name") && i + 1 < argc) server_name = argv[++i];
        else if (eq(argv[i], "--ca-bundle")   && i + 1 < argc) ca_bundle = argv[++i];
        else if (eq(argv[i], "--cert")        && i + 1 < argc) cert = argv[++i];
        else if (eq(argv[i], "--key")         && i + 1 < argc) key  = argv[++i];
        else if (eq(argv[i], "--insecure"))                     insecure = true;
        else if (eq(argv[i], "--register"))                     do_register = true;
        else if (eq(argv[i], "--config")      && i + 1 < argc) config_path = argv[++i];
        else if (eq(argv[i], "--timeout-ms")  && i + 1 < argc) timeout_ms = std::atoi(argv[++i]);
        else if (eq(argv[i], "-h") || eq(argv[i], "--help")) { usage(); return 0; }
        else { usage(); return 2; }
    }
    if (host.empty() && config_path.empty()) { usage(); return 2; }

    // If --config is given, load it and use its edge_host as default.
    cfd::Config ini_cfg;
    if (!config_path.empty()) {
        std::string err;
        if (!cfd::Config::load_from_file(config_path, ini_cfg, err)) {
            std::fprintf(stderr, "error: config: %s\n", err.c_str());
            return 2;
        }
        if (host.empty()) host = ini_cfg.edge_host;
    }
    if (host.empty()) { usage(); return 2; }

    cfd::log::set_level(cfd::log::Level::Debug);

    cfd::tunnel::QuicConfig cfg;
    cfg.edge_host   = host;
    cfg.edge_port   = port;
    cfg.alpn        = alpn;
    cfg.server_name = server_name;
    cfg.ca_bundle_path   = insecure ? "INSECURE" : ca_bundle;
    cfg.client_cert_path = cert;
    cfg.client_key_path  = key;

    cfd::tunnel::QuicClient client(std::move(cfg));

    const auto t0 = std::chrono::steady_clock::now();
    const auto ec = client.connect();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - t0).count();

    if (ec) {
        std::fprintf(stderr, "FAIL  host=%s:%u alpn=%s  %lld ms  %s\n",
                     host.c_str(), port, alpn.c_str(),
                     static_cast<long long>(elapsed_ms), ec.message().c_str());
        client.close();
        return 1;
    }

    std::fprintf(stdout, "OK    host=%s:%u alpn=%s  %lld ms\n",
                 host.c_str(), port, alpn.c_str(),
                 static_cast<long long>(elapsed_ms));

    if (do_register && !config_path.empty()) {
        cfd::tunnel::RegisterRequest req;
        if (!cfd::tunnel::parse_uuid(ini_cfg.tunnel_id, req.tunnel_uuid)) {
            std::fprintf(stderr, "error: bad tunnel_id: %s\n", ini_cfg.tunnel_id.c_str());
            client.close();
            return 1;
        }
        req.auth.account_tag = ini_cfg.account_tag;
        if (!cfd::tunnel::base64_decode(ini_cfg.tunnel_secret_b64, req.auth.tunnel_secret)) {
            std::fprintf(stderr, "error: bad tunnel_secret_b64\n");
            client.close();
            return 1;
        }
        req.conn_index = 0;
        req.version    = "cfd-cpp/0.1.0";
        req.arch       = "linux_amd64";
        req.features   = {"quic-datagram-v3"};

        cfd::tunnel::RegisterResponse resp;
        const auto ec2 = client.register_connection(req, resp, timeout_ms);
        if (ec2) {
            std::fprintf(stderr, "REGISTER FAIL  %s  cause=%s retry=%d\n",
                         ec2.message().c_str(),
                         resp.error_cause.c_str(), resp.should_retry);
            client.close();
            return 1;
        }
        std::fprintf(stdout, "REGISTERED  location=%s\n", resp.location_name.c_str());
    }

    client.close();
    (void)timeout_ms;
    return 0;
}

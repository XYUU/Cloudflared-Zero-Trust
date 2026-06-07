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

#include "cfd/log.hpp"
#include "cfd/quic_client.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

void usage() {
    std::fputs(
        "usage: cfd_probe --host <edge> [--port 7844] [--alpn argotunnel]\n"
        "                 [--insecure] [--timeout-ms 10000]\n"
        "\n"
        "Examples:\n"
        "  cfd_probe --host region1.argotunnel.com\n"
        "  cfd_probe --host 1.1.1.1 --port 443 --alpn h3 --insecure\n",
        stderr);
}

bool eq(const char* a, const char* b) noexcept { return std::strcmp(a, b) == 0; }

}  // namespace

int main(int argc, char** argv) {
    std::string host;
    std::uint16_t port = 7844;
    std::string alpn   = "argotunnel";
    bool insecure      = false;
    int  timeout_ms    = 10'000;

    for (int i = 1; i < argc; ++i) {
        if (eq(argv[i], "--host")    && i + 1 < argc) host     = argv[++i];
        else if (eq(argv[i], "--port")    && i + 1 < argc) port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        else if (eq(argv[i], "--alpn")    && i + 1 < argc) alpn = argv[++i];
        else if (eq(argv[i], "--insecure"))                 insecure = true;
        else if (eq(argv[i], "--timeout-ms") && i + 1 < argc) timeout_ms = std::atoi(argv[++i]);
        else if (eq(argv[i], "-h") || eq(argv[i], "--help")) { usage(); return 0; }
        else { usage(); return 2; }
    }
    if (host.empty()) { usage(); return 2; }

    cfd::log::set_level(cfd::log::Level::Debug);

    cfd::tunnel::QuicConfig cfg;
    cfg.edge_host = host;
    cfg.edge_port = port;
    cfg.alpn      = alpn;
    // Empty -> use the platform trust store. `INSECURE` is the explicit
    // opt-in to skip validation (used for testing against private edges).
    cfg.ca_bundle_path = insecure ? "INSECURE" : "";

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
    client.close();
    (void)timeout_ms;  // currently bounded inside QuicClient; flag reserved
    return 0;
}

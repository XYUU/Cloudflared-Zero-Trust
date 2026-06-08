// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "cfd/cidr.hpp"
#include <string>
#include <vector>

namespace cfd {

struct Config {
    // From cloudflared credentials.json
    std::string tunnel_id;
    std::string account_tag;
    std::string tunnel_secret_b64;

    std::string edge_host{"region1.argotunnel.com"};
    std::uint16_t edge_port{7844};

    std::string tun_name{"cfd0"};
    net::Cidr   tun_local{};   // e.g. 10.99.0.1/32
    std::vector<net::Cidr> routed_cidrs;  // CIDRs that WARP can reach via us

    // Optional TLS overrides passed through to QuicConfig.
    // Set ca_bundle_path = "INSECURE" to skip server certificate validation
    // (debug only). For Named Tunnels, client cert is not normally required.
    std::string ca_bundle_path;
    std::string client_cert_path;
    std::string client_key_path;

    // Parse from a small INI/KV file. Returns false on syntax error.
    static bool load_from_file(const std::string& path, Config& out, std::string& err);
};

}  // namespace cfd

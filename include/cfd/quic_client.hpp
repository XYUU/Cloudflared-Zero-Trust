// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "cfd/registration.hpp"
#include "cfd/router.hpp"
#include <functional>
#include <memory>
#include <string>

namespace cfd::tunnel {

struct QuicConfig {
    std::string edge_host;       // e.g. "region1.argotunnel.com"
    std::uint16_t edge_port{7844};
    std::string alpn{"argotunnel"};
    std::string tunnel_id;       // UUID from credentials.json
    std::string account_tag;
    std::vector<std::uint8_t> tunnel_secret;   // base64-decoded secret
    std::string ca_bundle_path;  // optional, for pinning
};

// Forward decl of the impl so the header doesn't drag in msquic.
class QuicClientImpl;

class QuicClient : public router::IPacketSink {
public:
    explicit QuicClient(QuicConfig cfg);
    ~QuicClient() override;

    QuicClient(const QuicClient&)            = delete;
    QuicClient& operator=(const QuicClient&) = delete;

    // Connect + send REGISTER_CONNECTION. Blocking.
    [[nodiscard]] std::error_code connect();

    // Open a bidi control stream and call RegistrationServer.registerConnection.
    // Blocks up to `timeout_ms`. On success the edge starts routing CIDR traffic
    // (configured via Zero Trust Dashboard) into this tunnel.
    [[nodiscard]] std::error_code register_connection(
        const RegisterRequest& req,
        RegisterResponse& resp_out,
        int timeout_ms = 10'000);

    // Sink interface: called by Router for outbound packets.
    void on_packet(const std::uint8_t* data, std::size_t len) noexcept override;

    using InboundHandler = std::function<void(const std::uint8_t*, std::size_t)>;
    void set_inbound_handler(InboundHandler h);

    void close() noexcept;

private:
    std::unique_ptr<QuicClientImpl> impl_;  // pimpl owns msquic handles
};

}  // namespace cfd::tunnel

// SPDX-License-Identifier: Apache-2.0
//
// tunnelrpc client. Opens a Cap'n Proto RPC session over a single bidirectional
// QUIC stream and calls RegistrationServer.registerConnection.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace cfd::tunnel {

struct TunnelAuth {
    std::string                     account_tag;
    std::vector<std::uint8_t>       tunnel_secret;   // raw, base64-decoded
};

struct RegisterRequest {
    TunnelAuth                      auth;
    std::array<std::uint8_t, 16>    tunnel_uuid{};
    std::uint8_t                    conn_index{0};
    std::string                     version{"cfd-cpp/0.1.0"};
    std::string                     arch;            // e.g. "linux_mipsel"
    std::vector<std::string>        features;        // e.g. {"ha-origin","quic-datagram-v3"}
};

struct RegisterResponse {
    bool                              ok{false};
    std::array<std::uint8_t, 16>      assigned_uuid{};
    std::string                       location_name;
    std::string                       error_cause;
    std::int64_t                      retry_after_seconds{0};
    bool                              should_retry{false};
};

// Decode "<UUID-string>" into 16 raw bytes. Returns false on bad format.
bool parse_uuid(const std::string& s, std::array<std::uint8_t, 16>& out) noexcept;

// Base64 decode (RFC 4648, std alphabet, padding required). Returns false on
// invalid input. Output is appended to `out`.
bool base64_decode(const std::string& in, std::vector<std::uint8_t>& out);

}  // namespace cfd::tunnel

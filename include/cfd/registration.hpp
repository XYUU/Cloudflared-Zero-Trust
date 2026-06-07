// SPDX-License-Identifier: Apache-2.0
//
// tunnelrpc client. Opens a Cap'n Proto RPC session over a single bidirectional
// QUIC stream and calls RegistrationServer.registerConnection.
//
// Memory model:
//   - capnp::MessageBuilder owns its arena; we hold it by value inside the
//     RegistrationRequest, so request bytes are freed when the request goes
//     out of scope.
//   - Serialized wire bytes are returned in a std::vector<std::uint8_t>; the
//     QUIC stream owns them only for the duration of the StreamSend, then the
//     SEND_COMPLETE callback drops them.
#pragma once
#include <array>
#include <cstdint>
#include <optional>
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

// Encode a RegisterRequest as a Cap'n Proto message ready for stream send.
// Stub-mode (no capnp at compile time) returns an empty buffer.
std::vector<std::uint8_t> encode_register_request(const RegisterRequest& req);

// Decode a wire response. Returns std::nullopt on parse error.
std::optional<RegisterResponse> decode_register_response(
    const std::uint8_t* data, std::size_t len) noexcept;

// Decode "<UUID-string>" into 16 raw bytes. Returns false on bad format.
bool parse_uuid(const std::string& s, std::array<std::uint8_t, 16>& out) noexcept;

// Base64 decode (RFC 4648, std alphabet, padding required). Returns false on
// invalid input. Output is appended to `out`.
bool base64_decode(const std::string& in, std::vector<std::uint8_t>& out);

}  // namespace cfd::tunnel

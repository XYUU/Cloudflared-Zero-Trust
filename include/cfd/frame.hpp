// SPDX-License-Identifier: Apache-2.0
// Encoder/decoder for cloudflared's QUIC datagram framing.
//
// Reference: github.com/cloudflare/cloudflared - connection/quic_datagram_v3.go
// Layout (current v3, subject to change upstream):
//   byte 0      : version (0x03)
//   byte 1      : type   (0x01 = IP packet, 0x02 = ICMP, ...)
//   byte 2..3   : flow id (big endian, uint16)
//   byte 4..    : payload (IP packet bytes)
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cfd::tunnel {

enum class FrameType : std::uint8_t {
    IpPacket = 0x01,
    Icmp     = 0x02,
};

struct Frame {
    FrameType            type{FrameType::IpPacket};
    std::uint16_t        flow_id{0};
    std::vector<std::uint8_t> payload;  // owns its bytes; freed by ~vector
};

constexpr std::uint8_t kFrameVersion = 0x03;
constexpr std::size_t  kFrameHeader  = 4;

// Encode a frame into `out`. Returns total bytes written; throws on overflow.
std::size_t encode(const Frame& in, std::vector<std::uint8_t>& out);

// Decode an on-the-wire datagram. Returns std::nullopt on malformed input.
struct DecodeResult {
    FrameType                    type;
    std::uint16_t                flow_id;
    std::span<const std::uint8_t> payload;  // points into the input buffer
};

bool decode(std::span<const std::uint8_t> in, DecodeResult& out) noexcept;

}  // namespace cfd::tunnel

// SPDX-License-Identifier: Apache-2.0
#include "cfd/frame.hpp"
#include <cassert>
#include <cstdio>

int main() {
    using namespace cfd::tunnel;
    Frame in;
    in.type = FrameType::IpPacket;
    in.flow_id = 0xBEEF;
    in.payload = {0x45, 0x00, 0x00, 0x14};  // truncated IPv4 header

    std::vector<std::uint8_t> wire;
    encode(in, wire);
    assert(wire.size() == kFrameHeader + 4);
    assert(wire[0] == kFrameVersion);
    assert(wire[1] == 0x01);
    assert(wire[2] == 0xBE && wire[3] == 0xEF);

    DecodeResult out{};
    assert(decode(wire, out));
    assert(out.type == FrameType::IpPacket);
    assert(out.flow_id == 0xBEEF);
    assert(out.payload.size() == 4);

    // Malformed inputs
    DecodeResult tmp{};
    assert(!decode(std::span<const std::uint8_t>{}, tmp));
    std::vector<std::uint8_t> bad = {0xFF, 0x01, 0, 0};
    assert(!decode(bad, tmp));

    std::puts("OK");
    return 0;
}

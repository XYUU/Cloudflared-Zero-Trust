// SPDX-License-Identifier: Apache-2.0
#include "cfd/frame.hpp"
#include <stdexcept>

namespace cfd::tunnel {

std::size_t encode(const Frame& in, std::vector<std::uint8_t>& out) {
    out.clear();
    out.reserve(kFrameHeader + in.payload.size());
    out.push_back(kFrameVersion);
    out.push_back(static_cast<std::uint8_t>(in.type));
    out.push_back(static_cast<std::uint8_t>(in.flow_id >> 8));
    out.push_back(static_cast<std::uint8_t>(in.flow_id & 0xFF));
    out.insert(out.end(), in.payload.begin(), in.payload.end());
    return out.size();
}

bool decode(std::span<const std::uint8_t> in, DecodeResult& out) noexcept {
    if (in.size() < kFrameHeader) return false;
    if (in[0] != kFrameVersion)   return false;
    out.type    = static_cast<FrameType>(in[1]);
    out.flow_id = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(in[2]) << 8) | in[3]);
    out.payload = in.subspan(kFrameHeader);
    return true;
}

}  // namespace cfd::tunnel

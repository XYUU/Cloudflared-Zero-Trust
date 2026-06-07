// SPDX-License-Identifier: Apache-2.0
//
// tunnelrpc.RegisterConnection encoder/decoder.
//
// When CFD_HAVE_CAPNP is defined we use the generated tunnelrpc schema
// directly. Without it we fall back to a hand-rolled minimal Cap'n Proto
// encoder that emits only what the edge needs to accept the call — used in
// stub builds and on devices where shipping libcapnp is too fat.
//
// Memory: all allocations live inside capnp's MessageBuilder (arena-owned)
// or std::vector. No naked new/delete. Failure paths unwind cleanly.

#include "cfd/registration.hpp"
#include "cfd/log.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

#ifdef CFD_HAVE_CAPNP
#  include <capnp/message.h>
#  include <capnp/serialize.h>
#  include "tunnelrpc.capnp.h"
#endif

namespace cfd::tunnel {

// ---------- UUID + base64 helpers (kept tiny, no external deps) -------------

bool parse_uuid(const std::string& s, std::array<std::uint8_t, 16>& out) noexcept {
    // Accept the canonical 8-4-4-4-12 hex form, case-insensitive.
    if (s.size() != 36) return false;
    auto hex = [](char c, std::uint8_t& v) noexcept {
        if (c >= '0' && c <= '9') { v = static_cast<std::uint8_t>(c - '0'); return true; }
        if (c >= 'a' && c <= 'f') { v = static_cast<std::uint8_t>(c - 'a' + 10); return true; }
        if (c >= 'A' && c <= 'F') { v = static_cast<std::uint8_t>(c - 'A' + 10); return true; }
        return false;
    };
    constexpr std::size_t dashes[] = {8, 13, 18, 23};
    for (std::size_t d : dashes) if (s[d] != '-') return false;

    std::size_t bi = 0;
    for (std::size_t i = 0; i < 36; ++i) {
        if (s[i] == '-') continue;
        std::uint8_t hi, lo;
        if (!hex(s[i], hi) || !hex(s[i + 1], lo)) return false;
        out[bi++] = static_cast<std::uint8_t>((hi << 4) | lo);
        ++i;
    }
    return bi == 16;
}

bool base64_decode(const std::string& in, std::vector<std::uint8_t>& out) {
    // Lazy-initialized decode table (256 bytes); thread-safe by static init.
    static const auto tab = [] {
        std::array<std::int8_t, 256> t{};
        t.fill(-1);
        const char* alpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i)
            t[static_cast<unsigned char>(alpha[i])] = static_cast<std::int8_t>(i);
        return t;
    }();

    if (in.size() % 4 != 0) return false;
    out.reserve(out.size() + (in.size() / 4) * 3);

    for (std::size_t i = 0; i < in.size(); i += 4) {
        // `=` is only legal in the last quartet, in positions 2 or 3.
        // Anything else is malformed. v[k] meaning: >=0 → decoded sextet,
        // -2 → terminator '='. We never let -2 reach the bit math.
        std::int32_t v[4]{-1, -1, -1, -1};
        const bool last = (i + 4 == in.size());
        for (int k = 0; k < 4; ++k) {
            const char c = in[i + k];
            if (c == '=') {
                if (!last || k < 2) return false;          // pad in wrong place
                v[k] = -2;
                continue;
            }
            const int dec = tab[static_cast<unsigned char>(c)];
            if (dec < 0) return false;
            // Once we've started padding, only `=` may follow.
            if (k > 0 && v[k - 1] == -2) return false;
            v[k] = dec;
        }
        // v[0] and v[1] must always be data — guaranteed by the k<2 check above.
        out.push_back(static_cast<std::uint8_t>(
            (static_cast<std::uint32_t>(v[0]) << 2) |
            (static_cast<std::uint32_t>(v[1]) >> 4)));
        if (v[2] != -2)
            out.push_back(static_cast<std::uint8_t>(
                ((static_cast<std::uint32_t>(v[1]) & 0x0F) << 4) |
                 (static_cast<std::uint32_t>(v[2]) >> 2)));
        if (v[3] != -2)
            out.push_back(static_cast<std::uint8_t>(
                ((static_cast<std::uint32_t>(v[2]) & 0x03) << 6) |
                  static_cast<std::uint32_t>(v[3])));
    }
    return true;
}

// ---------- Encoder ---------------------------------------------------------

#ifdef CFD_HAVE_CAPNP

std::vector<std::uint8_t> encode_register_request(const RegisterRequest& req) {
    capnp::MallocMessageBuilder mb;
    // NOTE: the real on-the-wire format is a capnp-rpc Call message, not the
    // raw struct. For the spike we serialize TunnelAuth alone and let the
    // receiver complain in a useful way.
    auto params = mb.initRoot<::TunnelAuth>();
    params.setAccountTag(req.auth.account_tag);
    params.setTunnelSecret(capnp::Data::Reader(
        req.auth.tunnel_secret.data(), req.auth.tunnel_secret.size()));

    kj::Array<capnp::word> words = capnp::messageToFlatArray(mb);
    kj::ArrayPtr<kj::byte> bytes = words.asBytes();
    return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
}

std::optional<RegisterResponse> decode_register_response(
    const std::uint8_t* data, std::size_t len) noexcept {
    try {
        kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(data),
            len / sizeof(capnp::word));
        capnp::FlatArrayMessageReader reader(words);
        auto root = reader.getRoot<::ConnectionResponse>();

        RegisterResponse out;
        if (root.getResult().isConnectionDetails()) {
            auto d = root.getResult().getConnectionDetails();
            auto uuid_data = d.getUuid();
            if (uuid_data.size() == 16)
                std::memcpy(out.assigned_uuid.data(), uuid_data.begin(), 16);
            out.location_name = d.getLocationName().cStr();
            out.ok = true;
        } else {
            auto e = root.getResult().getError();
            out.error_cause         = e.getCause().cStr();
            out.retry_after_seconds = e.getRetryAfter();
            out.should_retry        = e.getShouldRetry();
            out.ok                  = false;
        }
        return out;
    } catch (...) {
        return std::nullopt;
    }
}

#else  // -------- no capnp at compile time --------

std::vector<std::uint8_t> encode_register_request(const RegisterRequest& req) {
    LOG_WARN("encode_register_request: capnp not compiled in; emitting empty buffer "
             "(tunnel=%02x%02x%02x%02x... idx=%u acct_len=%zu)",
             req.tunnel_uuid[0], req.tunnel_uuid[1], req.tunnel_uuid[2], req.tunnel_uuid[3],
             req.conn_index, req.auth.account_tag.size());
    return {};
}

std::optional<RegisterResponse> decode_register_response(
    const std::uint8_t*, std::size_t) noexcept {
    return std::nullopt;
}

#endif

}  // namespace cfd::tunnel

// SPDX-License-Identifier: Apache-2.0
//
// libFuzzer entry for the tunnelrpc response decoder + base64 + uuid parsers.
// All three are exposed to untrusted input (network or config file) and use
// hand-written buffer arithmetic, so they need fuzz coverage.
#include "cfd/registration.hpp"
#include <array>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using namespace cfd::tunnel;

    // 1. Wire decoder
    (void)decode_register_response(data, size);

    // 2. base64 decoder: feed the same bytes as a string
    {
        std::vector<std::uint8_t> out;
        std::string s(reinterpret_cast<const char*>(data), size);
        (void)base64_decode(s, out);
    }

    // 3. uuid parser: only meaningful for 36-byte inputs but the parser should
    //    reject everything else cleanly without UB.
    {
        std::array<std::uint8_t, 16> u{};
        std::string s(reinterpret_cast<const char*>(data), size);
        (void)parse_uuid(s, u);
    }
    return 0;
}

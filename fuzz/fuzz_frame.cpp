// SPDX-License-Identifier: Apache-2.0
//
// libFuzzer entry point for the v3 datagram decoder. Goal: confirm that no
// untrusted edge byte sequence can crash decode() or escape its bounds.
//
// Build & run:
//   cmake -B b -DCMAKE_CXX_COMPILER=clang++
//   cmake --build b --target fuzz_frame
//   ./b/fuzz/fuzz_frame -max_total_time=60
#include "cfd/frame.hpp"
#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    cfd::tunnel::DecodeResult out{};
    (void)cfd::tunnel::decode(std::span<const std::uint8_t>(data, size), out);
    return 0;
}

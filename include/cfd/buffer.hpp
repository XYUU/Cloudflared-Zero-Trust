// SPDX-License-Identifier: Apache-2.0
// Fixed-capacity byte buffer for packet I/O. Backed by a single heap
// allocation owned by std::unique_ptr — no manual delete[] anywhere in the code.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#if __cpp_exceptions
#  include <stdexcept>
#endif

namespace cfd {

class Buffer {
public:
    explicit Buffer(std::size_t cap)
        : data_(std::make_unique<std::uint8_t[]>(cap)), cap_(cap), len_(0) {}

    Buffer(const Buffer&)            = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&&) noexcept        = default;
    Buffer& operator=(Buffer&&) noexcept = default;

    std::uint8_t*       data()       noexcept { return data_.get(); }
    const std::uint8_t* data() const noexcept { return data_.get(); }
    std::size_t         size() const noexcept { return len_; }
    std::size_t         capacity() const noexcept { return cap_; }

    void resize(std::size_t n) {
        if (n > cap_) [[unlikely]] overflow_("Buffer::resize overflow");
        len_ = n;
    }
    void clear() noexcept { len_ = 0; }

    void append(const void* p, std::size_t n) {
        if (len_ + n > cap_) [[unlikely]] overflow_("Buffer::append overflow");
        std::memcpy(data_.get() + len_, p, n);
        len_ += n;
    }

private:
    [[noreturn]] static void overflow_(const char* msg) {
#if __cpp_exceptions
        throw std::length_error(msg);
#else
        (void)msg;
        std::terminate();
#endif
    }

    std::unique_ptr<std::uint8_t[]> data_;
    std::size_t cap_;
    std::size_t len_;
};

}  // namespace cfd

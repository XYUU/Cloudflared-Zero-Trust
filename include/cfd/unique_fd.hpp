// SPDX-License-Identifier: Apache-2.0
// RAII wrapper for POSIX file descriptors. Non-copyable, movable.
// Closing in the destructor is the single source of truth — eliminates the
// most common leak class in this codebase.
#pragma once
#include <unistd.h>
#include <utility>

namespace cfd {

class UniqueFd {
public:
    UniqueFd() noexcept = default;
    explicit UniqueFd(int fd) noexcept : fd_(fd) {}

    UniqueFd(const UniqueFd&)            = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& o) noexcept : fd_(o.fd_) { o.fd_ = -1; }
    UniqueFd& operator=(UniqueFd&& o) noexcept {
        if (this != &o) {
            reset(o.fd_);
            o.fd_ = -1;
        }
        return *this;
    }

    ~UniqueFd() { reset(); }

    int get() const noexcept { return fd_; }
    bool valid() const noexcept { return fd_ >= 0; }
    explicit operator bool() const noexcept { return valid(); }

    int release() noexcept {
        int t = fd_;
        fd_ = -1;
        return t;
    }

    void reset(int new_fd = -1) noexcept {
        if (fd_ >= 0 && fd_ != new_fd) {
            // ::close may set errno; we intentionally ignore — the fd is gone either way.
            (void)::close(fd_);
        }
        fd_ = new_fd;
    }

private:
    int fd_ = -1;
};

}  // namespace cfd

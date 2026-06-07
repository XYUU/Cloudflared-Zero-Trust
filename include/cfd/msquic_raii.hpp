// SPDX-License-Identifier: Apache-2.0
// RAII wrappers around msquic's HQUIC handle family. msquic is a C library
// with paired *Open / *Close calls — every handle MUST be closed exactly once
// even on error paths. We model that with std::unique_ptr + custom deleters.
//
// Handles are kind-specific (Registration / Configuration / Connection / Stream),
// so the deleter remembers which Close to call. We embed the dispatch by
// keeping a pointer to the relevant Close function inside the deleter.
#pragma once
#ifdef CFD_HAVE_MSQUIC

#include <msquic.h>
#include <memory>
#include <atomic>

namespace cfd::tunnel::detail {

extern const QUIC_API_TABLE* g_msquic;  // set once by api_table_init()
QUIC_STATUS api_table_init() noexcept;
void        api_table_release() noexcept;

enum class HandleKind : std::uint8_t { Registration, Configuration, Connection, Stream };

struct HandleDeleter {
    HandleKind kind{HandleKind::Connection};
    void operator()(HQUIC h) const noexcept {
        if (!h || !g_msquic) return;
        switch (kind) {
            case HandleKind::Registration:  g_msquic->RegistrationClose(h);  break;
            case HandleKind::Configuration: g_msquic->ConfigurationClose(h); break;
            case HandleKind::Connection:    g_msquic->ConnectionClose(h);    break;
            case HandleKind::Stream:        g_msquic->StreamClose(h);        break;
        }
    }
};

using UniqueHandle = std::unique_ptr<std::remove_pointer_t<HQUIC>, HandleDeleter>;

inline UniqueHandle wrap(HQUIC h, HandleKind k) {
    return UniqueHandle(h, HandleDeleter{k});
}

}  // namespace cfd::tunnel::detail

#endif  // CFD_HAVE_MSQUIC

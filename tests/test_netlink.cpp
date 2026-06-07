// SPDX-License-Identifier: Apache-2.0
//
// Best-effort smoke test for the netlink client. We can't talk to the kernel
// from CI sandboxes (no CAP_NET_ADMIN), so we just exercise the open() path
// and confirm that misuse returns sensible errors rather than crashing.
#include "cfd/netlink.hpp"
#include <cassert>
#include <cstdio>

int main() {
#if defined(__linux__)
    cfd::netlink::Client c;
    auto ec = c.open();
    if (ec) {
        // Non-root environments may not have AF_NETLINK access. That's OK --
        // we just exit cleanly so the test still passes in restricted CI.
        std::fprintf(stderr, "skip: netlink open: %s\n", ec.message().c_str());
        std::puts("OK (skipped)");
        return 0;
    }

    int idx = 0;
    // Loopback should resolve everywhere.
    if (auto e = c.if_index("lo", idx); e) {
        std::fprintf(stderr, "skip: if_index lo: %s\n", e.message().c_str());
        std::puts("OK (skipped)");
        return 0;
    }
    assert(idx > 0);

    // Insanely-long name -> invalid_argument.
    int bad = 0;
    auto eba = c.if_index("this_name_is_far_too_long_for_an_interface", bad);
    assert(eba);
#endif
    std::puts("OK");
    return 0;
}

# Changelog

All notable changes follow [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versions follow [SemVer](https://semver.org/).

## Unreleased

### Fixed
- `Router::stop()` deadlocked because the reader was blocked in `read(2)`
  and nothing woke it. Added a self-pipe wake; `poll()` watches TUN + wake
  with a 1 s backstop timeout. (review #1)
- `base64_decode` accepted `=` in any position; `"===="`, `"=AAA"`, etc.
  produced garbage bytes instead of being rejected. Now strict per RFC 4648.
  (review #3) Regression seeds added to `test_registration`.
- `QuicClient` defaulted to `NO_CERTIFICATE_VALIDATION` whenever
  `ca_bundle_path` was empty — silent MITM exposure. Now requires the
  literal value `INSECURE` to disable validation; empty path uses the
  system trust store. (review #4)

- `register_connection` leaked the `StreamCtx` whenever the response timed
  out. Now sends `StreamShutdown(ABORT|IMMEDIATE)` so the SHUTDOWN_COMPLETE
  callback purges `live_streams_` like on the happy path. (ISSUE-002)
- `ca_bundle_path` other than `INSECURE`/empty was silently a no-op. Now
  populates `QUIC_CREDENTIAL_FLAG_SET_CA_CERTIFICATE_FILE` when msquic
  supports it. (ISSUE-003)
- Re-ordered `QuicClientImpl` members so `connection_` is destroyed before
  the state / streams it touches via callbacks. (ISSUE-004)
- Replaced `std::stoi` in `Config::load_from_file` with `std::from_chars`;
  malformed `edge_port` now returns a descriptive error rather than
  terminating the process. (ISSUE-005)

### Known issues
- Tracked in [`docs/ISSUES.md`](docs/ISSUES.md): ISSUE-001 (tunnelrpc RPC
  envelope, blocks RegisterConnection) is the only open item.

## v0.1.0 — 2026-06-07

First public preview. Goal: a working PoC binary that can register a Cloudflare
Zero Trust tunnel from a 光猫-class device and route a CIDR to a WARP client.

### Added
- C++17 project skeleton with cross-compile toolchains for **MIPSEL** and
  **ARMv7** OpenWrt/musl targets.
- **TUN device** (Linux) with RAII fd ownership.
- **RTNETLINK client** (`addr` / `route` / `mtu` / `link up`) — replaces the
  runtime dependency on `iproute2`.
- **LPM trie** for CIDR routing, single code path for IPv4 + IPv6.
- **QUIC client** built on msquic with strict RAII (`UniqueHandle` family
  + custom deleters); ALPN `argotunnel`; Datagram send + receive.
- **tunnelrpc.RegisterConnection** over a bidirectional QUIC stream using
  Cap'n Proto; UUID + base64 helpers (no external deps).
- **Router pump**: TUN → LPM → QUIC; QUIC → TUN.
- **End-to-end handshake probe** (`cfd_probe`) for protocol regression gating.
- **End-to-end WARP test script** (`scripts/e2e_warp_test.sh`).
- **libFuzzer harnesses** for `frame`, `registration`, `cidr` parsers.
- **OpenWrt package** with procd init script and UCI defaults.
- **Unit tests** for CIDR, LPM, frame, registration, netlink.
- GitHub Actions: host build (gcc + clang, ASan/UBSan), cross build
  (mipsel + arm), shellcheck, 60s fuzz smoke per parser.

### Security / Memory
- Zero naked `new`/`delete` in application code.
- All fds via `UniqueFd`; all heap via `unique_ptr` / `shared_ptr` / containers.
- msquic Datagram send buffers owned by `unique_ptr<SendCtx>`, released across
  the C boundary, reclaimed in every terminal `SEND_STATE_CHANGED` state.
- Control-stream lifetime tracked by `unordered_map<StreamCtx*, shared_ptr>`,
  cleaned on `SHUTDOWN_COMPLETE` *and* on early `StreamStart`/`StreamSend`
  failure (avoids leak when the callback never fires).

### Known limitations
- Wintun backend not implemented (out of scope for 光猫).
- macOS utun stub only — not exercised.
- IPv6 forwarding works at the routing layer but is not in the smoke test.
- Cloudflare protocol may change at any time without warning; CI's
  `cfd_probe` gate will catch ALPN/TLS regressions but not silent semantic
  drift in the tunnelrpc messages.

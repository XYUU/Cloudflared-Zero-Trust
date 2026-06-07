# Known issues

Tracking real defects found by code review.

## Open

### ISSUE-001: tunnelrpc RegisterConnection wire format is incomplete

**Severity:** 🔴 Blocks end-to-end functionality.

**Where:** [`src/tunnel/registration.cpp`](../src/tunnel/registration.cpp)
`encode_register_request`.

**What's wrong:** the encoder writes a bare `TunnelAuth` struct as the root
of the message. cloudflared's edge expects a full **capnp-rpc `Message`**
of kind `call`, with the method ID of `RegistrationServer.registerConnection`
and the params struct as the call's content. Without that envelope the edge
will reject the message and tear down the stream.

**Why it shipped this way:** the spike prioritized validating the QUIC +
ALPN + bidi-stream path. The RPC envelope needs the generated capnp client
class (`tunnelrpc.capnp.h::RegistrationServer::Client`) hooked into a KJ
event loop, which is a significant chunk of work and pulls in `libkj-async`.

**Plan:**
1. Generate the C++ stub from `proto/tunnelrpc.capnp` via `capnpc -oc++`
   (already wired in [`cmake/capnp.cmake`](../cmake/capnp.cmake)).
2. Implement a minimal `capnp::TwoPartyClient` over a `kj::AsyncIoStream`
   adapter that bridges our `StreamCtx` to KJ's read/write.
3. Use `RegistrationServer::Client(...).registerConnectionRequest()` to
   build the params, then `send()` and `wait()` for the result.
4. Delete the hand-rolled root-struct path entirely.

**Workaround:** none. RegisterConnection fails today.

---

## Closed in Unreleased

### ISSUE-002 (closed): `live_streams_` leaked on timeout
Fixed by calling `StreamShutdown(ABORT|IMMEDIATE)` in the timeout branch of
`register_connection`. The eventual `STREAM_SHUTDOWN_COMPLETE` callback now
purges the map entry as on the happy path.

### ISSUE-003 (closed): `ca_bundle_path` pinning was a no-op
Now sets `QUIC_CREDENTIAL_FLAG_SET_CA_CERTIFICATE_FILE` and
`cred.CaCertificateFile = path` when msquic was built with that capability;
falls back to the system trust store with a clear log line otherwise.

### ISSUE-004 (closed): `connection_` declared before stream-tracking state
Re-ordered members so `connection_` is destroyed first; remaining members
(state mutex, handlers, `live_streams_`) outlive the msquic callbacks that
might touch them.

### ISSUE-005 (closed): `Config::load_from_file` could throw on bad port
Replaced `std::stoi` with `std::from_chars`; bad input now returns a
descriptive error instead of `std::terminate`.

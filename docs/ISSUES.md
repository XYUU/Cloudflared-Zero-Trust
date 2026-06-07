# Known issues

Tracking real defects found by code review.

## Open

---

## Closed in Unreleased

### ISSUE-001 (closed): tunnelrpc RegisterConnection wire format
Replaced hand-rolled TunnelAuth serialisation with a proper capnp-rpc call via
`capnp::TwoPartyClient` over a `kj::AsyncIoStream` adapter (`MsquicAsyncStream`)
that bridges msquic stream callbacks to KJ cross-thread promise fulfillers.

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

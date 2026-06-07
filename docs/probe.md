# cfd_probe — QUIC handshake verification

This is the gate that proves Option B is real: if `argotunnel` ALPN
negotiates with a Cloudflare edge, the rest of the project has a target.
If it fails (e.g. server closes with NO_APPLICATION_PROTOCOL), we have to
re-think the protocol layer.

## Local smoke (without msquic)

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/src/cfd_probe --host region1.argotunnel.com --insecure
# -> stub mode reports "not_supported" — expected.
```

## Real handshake (host)

```sh
cmake -B build -DCFD_FETCH_MSQUIC=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/src/cfd_probe --host region1.argotunnel.com
# expected: OK  host=...  ~80-300 ms
```

## On the embedded target

After cross-building with `mipsel-openwrt-musl.cmake`, scp `cfd_probe` to
the device and run it directly. The probe is ~150 KB stripped — small
enough for any 16MB flash router.

## Interpreting failures

| Symptom                                       | Meaning                                              |
|-----------------------------------------------|------------------------------------------------------|
| `MsQuicOpen2 failed 0x...`                    | msquic init — library mis-linked                     |
| `connection_refused` in <50 ms                | UDP/443 blocked or DNS wrong                          |
| `connection_aborted` after ~100-300 ms        | TLS or ALPN rejected (most likely outcome to debug)  |
| `OK` but no datagrams flow                    | DatagramReceiveEnabled / tunnel registration missing |

## CI gate

Add to `.github/workflows/probe.yml` (or equivalent):

```yaml
- run: cmake -B b -DCFD_FETCH_MSQUIC=ON
- run: cmake --build b --target cfd_probe
- run: ./b/src/cfd_probe --host region1.argotunnel.com
```

A regression here means upstream changed ALPN or transport parameters — we
diff `cloudflared/connection/quic_*.go` and follow.

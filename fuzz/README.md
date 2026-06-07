# Fuzzing

libFuzzer harnesses guarding the protocol parsers that touch untrusted input:

| Target              | Surface                                                  |
|---------------------|----------------------------------------------------------|
| `fuzz_frame`        | `cfd::tunnel::decode()` (v3 QUIC datagram)               |
| `fuzz_registration` | `decode_register_response`, `base64_decode`, `parse_uuid`|
| `fuzz_cidr`         | `Cidr::parse`, `LpmTrie::insert`/`lookup`                |

## Build (host clang only)

```sh
cmake -B build-fuzz -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
                    -DCFD_BUILD_TESTS=OFF
cmake --build build-fuzz --target fuzz_frame fuzz_registration fuzz_cidr
```

## Run

```sh
mkdir -p corpus_run
./build-fuzz/fuzz/fuzz_frame -max_total_time=300 corpus_run fuzz/corpus/frame
```

A single crash leaves a `crash-*` reproducer in `corpus_run/`. Commit it back
into `fuzz/corpus/<target>/` after fixing — it becomes a regression seed.

## CI integration

Wire each target into a 60-second smoke job per PR; nightly jobs run for an
hour. The bar: zero new crashes vs. the previous corpus.

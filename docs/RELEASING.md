# Cutting a release

The release process is intentionally short: every step that humans get wrong
(building artifacts, computing sha256, attaching notes) is automated.

## Steps

1. Update [`CHANGELOG.md`](../CHANGELOG.md):
   - Move items from `## Unreleased` into a new section
     `## v<MAJOR>.<MINOR>.<PATCH> — YYYY-MM-DD`.
   - Keep entries terse; link PRs when useful.
2. Commit and push.
3. Tag and push the tag:
   ```sh
   git tag -a vX.Y.Z -m "vX.Y.Z"
   git push origin vX.Y.Z
   ```
4. The [`release`](../.github/workflows/release.yml) workflow fires:
   - Builds `cfd` for `x86_64`, `mipsel`, `arm`.
   - Strips each binary.
   - Generates a `.sha256` for each.
   - Reads the CHANGELOG section for this tag.
   - Publishes a GitHub Release with all of the above attached.

## Pre-release tags

Use suffixes (`v0.2.0-rc1`, `v0.2.0-beta.2`) — the workflow marks anything
containing `-` as prerelease automatically.

## Manual verification before tagging

```sh
# fast pass on host
cmake -B b -DCFD_FETCH_MSQUIC=OFF && cmake --build b -j && ctest --test-dir b
# cross-compile sanity
cmake -B b-mips -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mipsel-openwrt-musl.cmake \
                -DCFD_TOOLCHAIN_PREFIX=mipsel-linux-gnu- -DCFD_BUILD_TESTS=OFF
cmake --build b-mips --target cfd
```

## What does NOT auto-happen

- Pushing to OpenWrt feeds.
- Bumping `PKG_VERSION` in [`openwrt/Makefile`](../openwrt/Makefile) —
  do this in the same PR that lands the CHANGELOG section.

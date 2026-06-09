# cfd — C++ Cloudflare Tunnel client for embedded routers

精简版的 [cloudflared](https://github.com/cloudflare/cloudflared)，用 **C++17** 重写，
目标是在光猫 / 家用路由器（MIPS、ARM、musl）上跑。功能：

- 通过 QUIC 拨号 Cloudflare 边缘，注册 Zero Trust Tunnel
- 在本机维护 TUN 设备 + CIDR LPM 路由
- 让 **Cloudflare WARP** 客户端通过 Cloudflare 边缘 → 本机 → 内网 CIDR

> ⚠️ 这是 cloudflared 私有协议的**第三方互操作实现**，Cloudflare 不提供任何兼容承诺。
> WARP 必须配置 Zero Trust + Split Tunnel Include 才能"连到"本机背后的网段；不存在
> 让 WARP 直连本机公网 IP 的官方协议。

## 编译

宿主机：
```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build
```

交叉编译（光猫 MIPS LE，OpenWrt SDK）：
```sh
cmake -B build-mipsel \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mipsel-openwrt-musl.cmake \
  -DCFD_SYSROOT=/opt/openwrt-sdk/staging_dir/target-mipsel_24kc_musl \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-mipsel -j
```

ARMv7：
```sh
cmake -B build-arm \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-openwrt-musl.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-arm -j
```

## 运行
### 配置生成
- (二选一)
['token_decode'](scripts/token_decode.sh)提供了解码配置的能力，cloudflared部署时，页面会提示一个形如：
```
cloudflared tunnel run --token Base64编码的你的token
```
将"Base64编码的你的token"放在命令行末尾作为参数传入脚本即可解码json配置。
或者：
- 在一台普通机器上跑一次 `cloudflared tunnel create my-router`，生成的
   `<UUID>.json` 里的字段就是配置信息。

### 使用方法
1. 将配置填到 示例配置文件[`docs/config.example.ini`](docs/config.example.ini)，并拷贝为cfd.ini。
2. 在 Zero Trust Dashboard：`cloudflared tunnel route ip add <CIDR> <UUID>`。
3. 在 WARP 客户端：Settings → Split Tunnel → Include `<CIDR>`。
4. 在光猫上：

```sh
cfd --config /etc/cfd.ini
```

## 内存安全

- 所有 fd 由 [`UniqueFd`](include/cfd/unique_fd.hpp) RAII 管理，析构关闭。
- 所有堆内存由 `std::unique_ptr` / `std::shared_ptr` / 容器持有，**代码中无裸 `new`/`delete`**
  （唯一例外是 LPM trie 的内部节点，由 `LpmTrie::~LpmTrie` 递归释放）。
- msquic handle 由 `unique_ptr<HQUIC, MsQuicDeleter>` 自定义 deleter 持有。
- 推荐宿主调试构建：`-DCFD_ENABLE_ASAN=ON -DCFD_ENABLE_UBSAN=ON`。

## 进度

- [x] 项目骨架、跨平台 CMake、toolchain
- [x] CIDR / LPM trie 解析与匹配 + 单测
- [x] TUN 设备（Linux）
- [x] 帧编解码 (v3 datagram)
- [x] Router (TUN ↔ Tunnel pump)
- [x] msquic 实接：握手、ALPN、DatagramSend + SEND_COMPLETE 回收
- [x] `cfd_probe` 端到端握手验证工具（见 [`docs/probe.md`](docs/probe.md)）
- [x] tunnelrpc RegisterConnection 控制流（QUIC bidi + Cap'n Proto；
      capnp 缺失时 stub 出 `not_supported`）
- [x] RTNETLINK 地址 / 路由 / MTU / link up（去掉 iproute2 依赖，IPv4/IPv6 统一路径）
- [x] 端到端联调脚本 [`scripts/e2e_warp_test.sh`](scripts/e2e_warp_test.sh)
- [x] libFuzzer 入口：`fuzz_frame` / `fuzz_registration` / `fuzz_cidr`
      （见 [`fuzz/README.md`](fuzz/README.md)）
- [x] OpenWrt 包 + procd init（见 [`openwrt/README.md`](openwrt/README.md)）
- [x] GitHub Actions：host build × {gcc,clang} + ASan/UBSan、cross build × {mipsel,arm}、
      fuzz smoke、shellcheck、CodeQL、tag-driven release（见 [`docs/RELEASING.md`](docs/RELEASING.md)）
- [ ] Wintun 后端（光猫不需要，可推迟）

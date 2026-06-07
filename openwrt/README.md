# OpenWrt / 光猫 部署

## 一次性安装

```sh
# 在 OpenWrt SDK 里
cp -r <path-to-cfd>      feeds/cfd
cp -r openwrt/.          package/network/cfd/
./scripts/feeds update -a
make package/network/cfd/compile V=s
# -> bin/packages/<arch>/network/cfd_0.1.0-1_<arch>.ipk
```

把 `.ipk` 推到光猫：

```sh
scp cfd_*.ipk root@光猫:/tmp/
ssh root@光猫 'opkg install /tmp/cfd_*.ipk'
```

## 配置

把 `docs/config.example.ini` 改名为 `/etc/cfd.ini`，填好 `tunnel_id` /
`account_tag` / `tunnel_secret_b64` / `tun_local` / `route`，然后：

```sh
/etc/init.d/cfd enable
/etc/init.d/cfd start
logread -f -e cfd          # 看日志
```

## 故障排查

| 现象                             | 排查方向                                                      |
|----------------------------------|---------------------------------------------------------------|
| `procd ... cfd respawn too fast` | `logread -e cfd` 看具体退出原因；多半是 config 或网络         |
| `tun open: Operation not permitted` | 缺 `kmod-tun`；`opkg install kmod-tun`                        |
| `RegisterConnection: permission_denied` | tunnel_secret_b64 错；用 `cloudflared tunnel info` 核对  |
| WARP 客户端 ping 不通 CIDR        | Dashboard 没推 route，或 WARP Split Tunnel 没 Include 这个段 |

## 资源占用

参考目标：MT7621 (MIPS 880 MHz, 128 MB DDR3, 16 MB flash)。

| 指标       | 数值（release 构建，stripped）                  |
|------------|--------------------------------------------------|
| 二进制大小 | ~6.5 MB（含静态 msquic + openssl + capnp）        |
| RSS 空载   | ~3 MB                                            |
| RSS 满负   | ~8 MB（千包/秒 入站 + 千包/秒 出站）              |
| CPU        | 30-40 % 单核 @ 50 Mbps WARP 吞吐                  |

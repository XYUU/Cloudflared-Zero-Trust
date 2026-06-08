# Tunnel Certificate & QUIC Connection Notes

## 根本原因分析（2026-06-08 解决）

经过完整调试，QUIC 握手失败有三个独立原因，均已修复：

### 1. SNI 错误（最关键）
msquic 的 `ConnectionStart(hostname)` 同时用于 DNS 解析和 TLS SNI。
- 错误：SNI = `region1.argotunnel.com`
- 正确：SNI = `quic.cftunnel.com`（cloudflared 硬编码）

**修复**：预先解析 `edge_host` IP，通过 `QUIC_PARAM_CONN_REMOTE_ADDRESS` 设置远端地址，
再调 `ConnectionStart("quic.cftunnel.com")` 仅用作 SNI。

### 2. 服务器证书使用私有 CA
QUIC 7844 端口服务器证书：
- `CN=CloudFlare Origin Certificate`
- 签发者：`CloudFlare Origin SSL ECC Certificate Authority`（**不在**公共 CA 库）

系统 CA 库（`/etc/ssl/certs/`）不含此 CA，msquic 嵌入的 OpenSSL
默认查找 `/usr/local/ssl/cert.pem`（同样不存在）。

**修复**：将 cloudflared 源码中的 Cloudflare Origin CA 嵌入到
`include/cfd/cloudflare_ca.hpp`，连接时写临时文件并通过
`QUIC_CREDENTIAL_FLAG_SET_CA_CERTIFICATE_FILE` 加载。

### 3. msquic POSIX stub
不设 `QUIC_CREDENTIAL_FLAG_USE_TLS_BUILTIN_CERTIFICATE_VALIDATION` 时，
POSIX 平台的 `CxPlatCertVerifyRawCertificate()` 是永远返回 0 的桩函数。

**修复**：始终设置该 flag，使 msquic 使用标准 `X509_verify_cert()`。

## 认证方式

Named Tunnel **不需要** mTLS client certificate。认证流程：
1. QUIC/TLS 握手：仅服务端验证（用上述 Cloudflare Origin CA）
2. `RegisterConnection` RPC：发送 `account_tag` + `tunnel_secret`（来自 credentials.json）

## 配置文件（cfd.ini）

```ini
tunnel_id         = <UUID from cloudflared tunnel create>
account_tag       = <account tag from credentials.json>
tunnel_secret_b64 = <base64 tunnel secret>

edge_host = region1.argotunnel.com
edge_port = 7844
```

不需要任何证书文件。

## 测试命令

```bash
# 探测握手（应在 ~200ms 内返回 OK）
build-quic/src/cfd_probe --host region1.argotunnel.com

# 完整隧道（需要 root 创建 TUN 设备）
sudo build-quic/src/cfd --config cfd.ini --verbose
```

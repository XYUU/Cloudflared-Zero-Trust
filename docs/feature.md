1. 某一次我运行时出现了如下日志：
```
2026-06-08T06:06:55 WARN  quic_client.cpp:757  QUIC shutdown by peer: error=0x2710
2026-06-08T06:07:00 WARN  quic_client.cpp:663  RegisterConnection rejected: EDUPCONN (retry=0 after=0)
2026-06-08T06:07:00 WARN  main.cpp:270  conn[2] register failed: Permission denied
2026-06-08T06:07:15 INFO  quic_client.cpp:668  RegisterConnection ok: location=mad06
2026-06-08T06:07:15 INFO  main.cpp:276  conn[3] registered at mad06
2026-06-08T06:07:15 INFO  main.cpp:285  tunnel up: 3/4 connections healthy
2026-06-08T06:07:15 INFO  main.cpp:290  cfd running. Ctrl-C to stop.
```
[cloudflared](https://github.com/cloudflare/cloudflared.git)有失败重新建立连接相关逻辑吗？参照它的逻辑实现，使Cloudflare侧Status显示Healthy。

2. 实现 SessionManager 协议（处理 TCP/UDP session proxy）                                                                                                      
3. 实现 ConfigurationManager 协议（接收路由配置更新）
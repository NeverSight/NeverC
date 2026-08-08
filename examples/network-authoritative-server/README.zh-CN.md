**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 示例](../../docs/examples/README.zh-CN.md)

# 权威游戏服务器

这个可运行示例使用 NeverC 的可移植网络栈，而非原始平台 socket。它提供：

- 基于 TCP 的控制平面，签发 CSPRNG 支持的会话令牌；
- UDP 和原生 QUIC 实时输入平面；
- 60 Hz 权威模拟循环和有界输入队列；
- 针对加入请求的时间戳/nonce 重放保护，以及每个会话的单调递增输入序列号。

默认目标为 `x86_64-linux-gnu`。可用任意受支持的 NeverC 目标覆盖：

```bash
neverc make          # debug：-g（首次构建默认）
neverc make release  # release：-O2 --strip
neverc make debug    # 切回 debug
neverc make TARGET=aarch64-linux-gnu
neverc make TARGET=x86_64-pc-windows-msvc OUTPUT=authoritative-server.exe
```

Makefile 会持久化 `TARGET` 和 `PROFILE`，之后的 `neverc make` 会保持同一产物
选择。发布构建使用 NeverC 内置 `--strip`。
参见 [发布构建](../../docs/release-builds/README.zh-CN.md)。


使用 P-256 TLS 证书和密钥运行 QUIC 端点：

```bash
./authoritative-server cert.pem key.pem
```

可选地址默认为 TCP `:7000`、UDP `:7001` 和 QUIC `:7002`。
TCP 加入行格式为 `JOIN <client-id> <unix-ms> <32-hex-nonce>`。UDP 输入格式为
`INPUT <32-hex-session-token> <sequence> <dx> <dy>`。QUIC 客户端协商
`neverc-game/1`，在首个流上用 `AUTH <token>` 认证，然后发送包含
`<sequence> <dx> <dy>` 的数据报。

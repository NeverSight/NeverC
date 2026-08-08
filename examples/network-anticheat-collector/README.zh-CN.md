**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 示例](../../docs/examples/README.zh-CN.md)

# mTLS 反作弊遥测收集器

这个可运行的收集器通过 TLS 1.3 提供多路复用 NRPC 协议，并强制要求
客户端证书。`anticheat.Telemetry/Collect` 是双向流：代理发送已签名的
遥测记录，并为每条被接受的记录接收一个 nonce ACK。

每条 DATA 消息由一个 64 字节的头部和一个不透明的正文组成（最大 1 MiB）。
头部包含版本 `1`、三个零字节、一个八字节大端序 Unix 毫秒时间戳、一个
16 字节 nonce、一个四字节大端序正文长度，以及一个 32 字节
HMAC-SHA256。MAC 覆盖 `agent-id || first-32-header-bytes || body`。
必须提供 NRPC 元数据值 `agent-id`。在 30 秒的时钟窗口内，每个 nonce
只会被接受一次。

被接受的记录进入一个有界队列。专用的单写入器会追加一条 JSONL 审计事件，
其中包含客户端证书指纹、nonce、正文摘要、时间戳和正文大小；收集器绝不会
将不受信任的正文原始字节直接写入审计日志。

为本机或任意受支持的目标构建：

```bash
neverc make          # debug：-g（首次构建默认）
neverc make release  # release：-O2 --strip
neverc make debug    # 切回 debug
neverc make TARGET=aarch64-pc-windows-msvc OUTPUT=anticheat-collector.exe
```

Makefile 会持久化 `PROFILE`，之后的 `neverc make` 会保持同一 debug/release
选择。发布构建使用 NeverC 内置 `--strip`。
参见 [发布构建](../../docs/release-builds/README.zh-CN.md)。


运行时需提供服务器证书、服务器密钥、受信任的客户端 CA、共享的 32 字节
签名密钥和审计日志路径：

```bash
./anticheat-collector server.pem server-key.pem agent-ca.pem \
  0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef \
  telemetry-audit.jsonl
```

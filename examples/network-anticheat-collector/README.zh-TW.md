**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 範例](../../docs/examples/README.zh-TW.md)

# mTLS 反作弊遙測收集器

這個可執行的收集器透過 TLS 1.3 提供多工 NRPC 通訊協定，並強制要求
用戶端憑證。`anticheat.Telemetry/Collect` 是雙向串流：代理程式傳送
已簽署的遙測記錄，並為每筆獲接受的記錄接收一個 nonce ACK。

每個 DATA 訊息由 64 位元組標頭和不透明本文組成（上限 1 MiB）。標頭包含
版本 `1`、三個零位元組、一個八位元組大端序 Unix 毫秒時間戳記、一個
16 位元組 nonce、一個四位元組大端序本文長度，以及一個 32 位元組
HMAC-SHA256。MAC 涵蓋 `agent-id || first-32-header-bytes || body`。
必須提供 NRPC 中繼資料值 `agent-id`。在 30 秒的時鐘視窗內，每個 nonce
只會獲接受一次。

獲接受的記錄會進入有界佇列。專用的單一寫入器會附加一筆 JSONL 稽核事件，
其中包含用戶端憑證指紋、nonce、本文摘要、時間戳記和本文大小；收集器絕不會
將不受信任的本文原始位元組直接寫入稽核日誌。

為主機或任何受支援的目標建置：

```bash
neverc make
neverc make TARGET=aarch64-pc-windows-msvc OUTPUT=anticheat-collector.exe
```

執行時需提供伺服器憑證、伺服器金鑰、受信任的用戶端 CA、共用的 32 位元組
簽署金鑰和稽核路徑：

```bash
./anticheat-collector server.pem server-key.pem agent-ca.pem \
  0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef \
  telemetry-audit.jsonl
```

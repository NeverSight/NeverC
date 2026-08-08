**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 範例](../../docs/examples/README.zh-TW.md)

# 權威遊戲伺服器

這個可執行範例使用 NeverC 的可攜式網路堆疊，而非原始平台 socket。它提供：

- 基於 TCP 的控制平面，簽發 CSPRNG 支援的會話權杖；
- UDP 和原生 QUIC 即時輸入平面；
- 60 Hz 權威模擬迴圈和有界輸入佇列；
- 針對加入請求的時間戳記/nonce 重放保護，以及每個工作階段的單調遞增輸入序號。

預設目標為 `x86_64-linux-gnu`。可用任意支援的 NeverC 目標覆寫：

```bash
neverc make          # debug：-g（首次建置預設）
neverc make release  # release：-O2 --strip
neverc make debug    # 切回 debug
neverc make TARGET=aarch64-linux-gnu
neverc make TARGET=x86_64-pc-windows-msvc OUTPUT=authoritative-server.exe
```

Makefile 會持久化 `TARGET` 和 `PROFILE`，之後的 `neverc make` 會保持同一產物
選擇。發布建置使用 NeverC 內建 `--strip`。
參見 [發布建置](../../docs/release-builds/README.zh-TW.md)。


使用 P-256 TLS 憑證和金鑰執行 QUIC 端點：

```bash
./authoritative-server cert.pem key.pem
```

可選位址預設為 TCP `:7000`、UDP `:7001` 和 QUIC `:7002`。
TCP 加入列格式為 `JOIN <client-id> <unix-ms> <32-hex-nonce>`。UDP 輸入格式為
`INPUT <32-hex-session-token> <sequence> <dx> <dy>`。QUIC 用戶端協商
`neverc-game/1`，在首個串流上用 `AUTH <token>` 認證，然後傳送包含
`<sequence> <dx> <dy>` 的資料報。

**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC サンプル](../../docs/examples/README.ja.md)

# mTLS アンチチート・テレメトリコレクター

この実行可能なコレクターは、クライアント証明書を必須として、TLS 1.3
上で多重化 NRPC プロトコルを提供します。`anticheat.Telemetry/Collect`
は双方向ストリームです。エージェントは署名済みテレメトリレコードを送信し、
受理された各レコードに対して nonce ACK を 1 つ受信します。

各 DATA メッセージは、64 バイトのヘッダーと不透明な本文（最大 1 MiB）
で構成されます。ヘッダーには、バージョン `1`、3 個のゼロバイト、8 バイト
のビッグエンディアン Unix ミリ秒タイムスタンプ、16 バイトの nonce、
4 バイトのビッグエンディアン本文長、32 バイトの HMAC-SHA256 が含まれます。
MAC の対象は `agent-id || first-32-header-bytes || body` です。NRPC
メタデータ値 `agent-id` は必須です。nonce は 30 秒の時刻ウィンドウ内で
一度だけ受理されます。

受理されたレコードは上限付きキューに入ります。専用の単一ライターが、
クライアント証明書のフィンガープリント、nonce、本文ダイジェスト、
タイムスタンプ、本文サイズを含む JSONL 監査イベントを追記します。
コレクターが信頼できない本文の生バイトを監査ログへ直接書き込むことは
ありません。

ホストまたは任意の対応ターゲット向けにビルドします。

```bash
neverc make          # debug: -g（初回ビルドの既定）
neverc make release  # release: -O2 --strip
neverc make debug    # debug に戻す
neverc make TARGET=aarch64-pc-windows-msvc OUTPUT=anticheat-collector.exe
```

Makefile は `PROFILE` を保持するため、以降の `neverc make` は同じ
debug/release 選択を使います。リリースは NeverC 組み込みの `--strip` を使います。
[リリースビルド](../../docs/release-builds/README.ja.md) を参照。


サーバー証明書、サーバー鍵、信頼するクライアント CA、共有 32 バイト
署名鍵、および監査パスを指定して実行します。

```bash
./anticheat-collector server.pem server-key.pem agent-ca.pem \
  0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef \
  telemetry-audit.jsonl
```

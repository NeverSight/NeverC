**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC サンプル](../../docs/examples/README.ja.md)

# 権威型ゲームサーバー

この実行可能なサンプルは、生のプラットフォームソケットではなく NeverC の
ポータブルネットワークスタックを使用します。提供する機能は次のとおりです。

- CSPRNG ベースのセッショントークンを発行する TCP コントロールプレーン；
- UDP とネイティブ QUIC によるリアルタイム入力プレーン；
- 60 Hz の権威型シミュレーションループと上限付き入力キュー；
- 参加要求向けのタイムスタンプ/nonce リプレイ保護と、セッションごとの
  単調増加入力シーケンス番号。

ホスト向けにビルドするか、対応する NeverC ターゲットトリプルを指定します。

```bash
neverc make
neverc make TARGET=aarch64-linux-gnu
neverc make TARGET=x86_64-pc-windows-msvc OUTPUT=authoritative-server.exe
```

QUIC エンドポイント用に P-256 TLS 証明書と鍵を指定して実行します。

```bash
./authoritative-server cert.pem key.pem
```

オプションのアドレスは既定で TCP `:7000`、UDP `:7001`、QUIC `:7002` です。
TCP 参加行は `JOIN <client-id> <unix-ms> <32-hex-nonce>` です。UDP 入力は
`INPUT <32-hex-session-token> <sequence> <dx> <dy>` です。QUIC クライアントは
`neverc-game/1` をネゴシエートし、最初のストリームで `AUTH <token>` により
認証した後、`<sequence> <dx> <dy>` を含むデータグラムを送信します。

**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC サンプル](../../docs/examples/README.ja.md)

# Windows カーネルドライバーの例

NeverC で構築した最小限の WDM カーネルドライバーです。デフォルトでは **x64** を対象とし、
ARM64 向けにビルドすることもできます。macOS / Linux からのクロスコンパイルに対応しています。

NeverC はオールインワンコンパイラです——単一の呼び出しでプリプロセス、コンパイル、
最適化（auto-LTO）、および内蔵リンカーによるリンクを処理します。

## ビルド

リポジトリから：

```bash
cd examples/windows-driver
neverc make
```

これで `ExampleDriver-x64.sys` が生成されます。ARM64 向け、または両方をビルドするには：

```bash
neverc make ARCH=arm64
neverc make all-arch
```

スタンドアロンの NeverC リリースから：

```bash
neverc make NEVERC=/path/to/neverc
```

出力は `ExampleDriver-<アーキテクチャ>.sys`（auto-LTO 最適化済み）です。
デフォルトビルドにはデバッグ用の `-g` が含まれています。**リリースビルドでは `-g` を
削除**してデバッグシンボルを除去し、バイナリサイズを削減してください
（~38 KB → ~3 KB）。

## 手動ビルド（Make なし）

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fms-kernel \
  -Wall -nostdlib -shared \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -lntoskrnl -lhal \
  -o ExampleDriver-x64.sys driver.c
```

ARM64 の場合は target を `aarch64-pc-windows-msvc` に変更するだけで、他は同じです。
`-fms-kernel` がターゲットに対応する WDK のヘッダーとインポートライブラリを選択し、
WDK が要求するアーキテクチャマクロも定義するため、手動で渡す必要はありません。

> `-g` は DWARF デバッグ情報を PE に埋め込みます。`llvm-dwarfdump` で検査できます。
> リリースビルドではバイナリサイズを削減するため省略してください。

## テスト署名

Windows は署名されていないカーネルドライバーの読み込みを拒否します。
`-ftest-sign` は Authenticode 署名を付加し、テストマシンでこのチェックを
通過できるようにします:

```bash
neverc make TESTSIGN=1
neverc make ARCH=arm64 TESTSIGN=1
```

手動実行時に `-ftest-sign` を追加しても構いません。このオプションは
`-fms-kernel` と併用する場合のみ受け付けられます。テスト署名はユーザーモード
バイナリには意味がないためです。

署名に使う証明書はコンパイラに組み込まれています — 自己署名証明書であり、
その秘密鍵は設計上公開されています。真正性は一切保証せず、意図的に制限を
緩めたマシンでコード整合性チェックを満たすだけのものです。対象マシンでは
管理者として一度だけ次の設定を行ってください:

```cmd
bcdedit /set testsigning on
certutil -addstore Root neverc-test-signing.cer
certutil -addstore TrustedPublisher neverc-test-signing.cer
```

その後再起動します。証明書は `utils/neverc-test-signing.cer` にあります。

**テストマシンの外に出るものには決して使用しないでください。** 本番環境では
正規のコード署名証明書を使用してください（Windows 10 1607 以降では
Microsoft ハードウェア デベロッパー センターによる構成証明署名も必要です）。

## 機能

- `\Device\ExampleDriver` にデバイスオブジェクトを作成
- `\DosDevices\ExampleDriver` にシンボリックリンクを作成
- `IRP_MJ_CREATE`、`IRP_MJ_CLOSE`、`IRP_MJ_DEVICE_CONTROL` を処理
- `DbgPrint` 経由でロード/アンロードメッセージを出力

## ロード（Windows テストマシン上）

```cmd
sc create ExampleDriver type= kernel binPath= C:\path\to\ExampleDriver-x64.sys
sc start ExampleDriver
sc stop ExampleDriver
sc delete ExampleDriver
```

テスト署名を有効にするか、本番環境用のコード署名証明書を使用してください。

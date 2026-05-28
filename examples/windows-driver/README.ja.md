# Windows カーネルドライバーの例

NeverC で構築した最小限の WDM カーネルドライバーです。macOS / Linux からのクロスコンパイルに対応しています。

NeverC はオールインワンコンパイラです——単一の呼び出しでプリプロセス、コンパイル、
最適化（auto-LTO）、および内蔵リンカーによるリンクを処理します。

## ビルド

リポジトリから：

```bash
cd examples/windows-driver
make
```

スタンドアロンの NeverC リリースから：

```bash
make NEVERC=/path/to/neverc
```

出力は `ExampleDriver.sys`（auto-LTO 最適化済み）です。
デフォルトビルドにはデバッグ用の `-g` が含まれています。**リリースビルドでは `-g` を
削除**してデバッグシンボルを除去し、バイナリサイズを削減してください
（~38 KB → ~3 KB）。

## 手動ビルド（Make なし）

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fms-kernel -fms-extensions -fms-compatibility \
  -D_AMD64_ -DNTDDI_VERSION=0x06010000 -D_WIN32_WINNT=0x0601 \
  -Wall -nostdlib -shared \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -lntoskrnl -lhal \
  -o ExampleDriver.sys driver.c
```

> `-g` は DWARF デバッグ情報を PE に埋め込みます。`llvm-dwarfdump` で検査するか、
> WinDbg でシンボルをロードできます。リリースビルドではバイナリサイズを削減するため
> 省略してください。

## 機能

- `\Device\ExampleDriver` にデバイスオブジェクトを作成
- `\DosDevices\ExampleDriver` にシンボリックリンクを作成
- `IRP_MJ_CREATE`、`IRP_MJ_CLOSE`、`IRP_MJ_DEVICE_CONTROL` を処理
- `DbgPrint` 経由でロード/アンロードメッセージを出力

## ロード（Windows テストマシン上）

```cmd
sc create ExampleDriver type= kernel binPath= C:\path\to\ExampleDriver.sys
sc start ExampleDriver
sc stop ExampleDriver
sc delete ExampleDriver
```

テスト署名を有効にするか、本番環境用のコード署名証明書を使用してください。

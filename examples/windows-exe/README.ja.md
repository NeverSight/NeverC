**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Windows Ring3 EXE サンプル

NeverC でクロスコンパイルした Windows ユーザーモード実行ファイルです。Win32 API を使用。

## ビルド

```bash
cd examples/windows-exe
make
```

## 手動ビルド

```bash
neverc --target=x86_64-pc-windows-msvc -Wall -fms-extensions -fms-compatibility -D_AMD64_ -Xlinker --subsystem=console -lkernel32 -luser32 -lmsvcrt -o example.exe main.c
```

## 機能

- システム情報を `GetSystemInfo` で取得
- `CreateToolhelp32Snapshot` でプロセス列挙
- `VirtualAlloc`/`VirtualQuery`/`VirtualFree` のデモ


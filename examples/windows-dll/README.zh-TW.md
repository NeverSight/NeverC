**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Windows Ring3 DLL 範例

使用 NeverC 交叉編譯的 Windows 使用者態 DLL。

## 建置

```bash
cd examples/windows-dll
make
```

## 手動建置（不使用 Make）

```bash
neverc --target=x86_64-pc-windows-msvc -Wall -fms-extensions -fms-compatibility -D_AMD64_ -shared -Xlinker --entry=DllMain -Xlinker --subsystem=windows -lkernel32 -luser32 -o example.dll dllmain.c
```

## 功能說明

- 匯出跨程序記憶體存取封裝
- 程序/模組列舉
- XOR 緩衝區加密輔助函式


**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 範例](../../docs/examples/README.zh-TW.md)

# Windows Ring3 EXE 範例

使用 NeverC 交叉編譯的 Windows 使用者態可執行檔。示範 Win32 API 的系統資訊查詢、程序列舉和虛擬記憶體操作。

## 建置

```bash
cd examples/windows-exe
neverc make          # debug：-g（首次建置預設）
neverc make release  # release：-O2 --strip
neverc make debug    # 切回 debug
```

Makefile 會持久化 `PROFILE`，後續 `neverc make` 會保持同一 debug/release
選擇。release 使用 NeverC 內建 `--strip`：刪除除錯中繼資料與不需要的靜態
符號名，同時保留載入器/動態 ABI 仍需要的名稱。詳見
[發行建置](../../docs/release-builds/README.zh-TW.md)。

## 手動建置（不使用 Make）

```bash
neverc --target=x86_64-pc-windows-msvc -Wall -Xlinker --subsystem=console -lkernel32 -luser32 -lmsvcrt -o example.exe main.c
```

## 功能說明

- 通過 `GetSystemInfo` 查詢系統資訊
- 使用 `CreateToolhelp32Snapshot` 列舉程序
- 示範 `VirtualAlloc`/`VirtualQuery`/`VirtualFree`


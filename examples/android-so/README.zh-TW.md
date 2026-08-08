**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 範例](../../docs/examples/README.zh-TW.md)

# Android 共享庫範例

使用 NeverC 交叉編譯的 ARM64 原生 `.so` 共享庫，用於 Android 平台。可從 macOS、Windows 或 Linux 建置——無需 Android NDK 或 CMake。

## 建置

```bash
cd examples/android-so
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
neverc --target=aarch64-linux-android -Wall -shared -fPIC -ldl -o libneverc.so lib.c
```

## 功能說明

- 提供遊戲安全研究常用的輔助函式：PID 查詢、`/proc/self/maps` 讀取、RWX 記憶體分配、XOR 緩衝區加密
- 使用 `dlopen` 動態載入 `liblog.so`
- 示範使用 `mmap` + `PROT_EXEC` 分配可執行記憶體


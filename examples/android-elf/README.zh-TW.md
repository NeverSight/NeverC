**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 範例](../../docs/examples/README.zh-TW.md)

# Android ELF 範例

使用 NeverC 交叉編譯的 ARM64 原生 ELF 可執行檔，用於 Android 平台。設計為在已 root 的 Android 裝置上透過 `adb shell` 直接執行。可從 macOS、Windows 或 Linux 建置——無需 Android NDK 或 CMake。

NeverC 在 `runtime/android/` 中內建了 Android sysroot（NDK r26c, API 21+），因此一次呼叫即可完成預處理、編譯、最佳化（自動 LTO）和連結。

## 建置

從儲存庫根目錄：

```bash
cd examples/android-elf
neverc make          # debug：-g（首次建置預設）
neverc make release  # release：-O2 --strip
neverc make debug    # 切回 debug
```

Makefile 會持久化 `PROFILE`，後續 `neverc make` 會保持同一 debug/release
選擇。release 使用 NeverC 內建 `--strip`：刪除除錯中繼資料與不需要的靜態
符號名，同時保留載入器/動態 ABI 仍需要的名稱。詳見
[發行建置](../../docs/release-builds/README.zh-TW.md)。

使用獨立的 NeverC 發行版：

```bash
neverc make NEVERC=/path/to/neverc
```

## 手動建置（不使用 Make）

```bash
neverc --target=aarch64-linux-android -Wall -fPIE -lm -ldl -llog -o android-elf main.c
```

## 部署和執行

透過 adb 推送到裝置並執行：

```bash
neverc make run
```

或手動操作：

```bash
adb push android-elf /data/local/tests/
adb shell chmod 755 /data/local/tests/android-elf
adb shell /data/local/tests/android-elf
```

## 功能說明

- 列印裝置資訊（`uname`）和核心版本
- 檢查 root/權限狀態（`uid`/`euid`，`su` 路徑）
- 動態載入 `liblog.so` 並呼叫 `__android_log_print`
- 讀取 `/proc/self/maps` 顯示記憶體佈局
- 示範 Android 上的 `dlopen`/`dlsym`、`readlink`、`fopen`

**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 範例](../../docs/examples/README.zh-TW.md)

# Linux 全靜態連結範例

使用 NeverC 建置的自包含全靜態連結 Linux 可執行檔。輸出二進位零執行階段相依性。

NeverC 在 `runtime/linux/` 中內建了 Linux sysroot（Ubuntu 22.04，glibc 2.35），單次呼叫即可完成預處理、編譯、最佳化（auto-LTO）以及透過內建連結器進行連結。

## 建置

```bash
cd examples/linux-static
neverc make          # debug：-g（首次建置預設）
neverc make release  # release：-O2 --strip
neverc make debug    # 切回 debug
```

Makefile 會持久化 `PROFILE`，後續 `neverc make` 會保持同一 debug/release
選擇。release 使用 NeverC 內建 `--strip`：刪除除錯中繼資料與不需要的靜態
符號名，同時保留載入器/動態 ABI 仍需要的名稱。詳見
[發行建置](../../docs/release-builds/README.zh-TW.md)。


AArch64:

```bash
neverc make TARGET=aarch64-linux-gnu
```

## 手動建置

```bash
neverc --target=x86_64-linux-gnu -Wall -static -lm -o static-demo main.c
```

## 執行

```bash
chmod +x static-demo
./static-demo
```

## 功能說明

- 系統資訊（指標大小、架構）
- 數學函數：`sqrt`、`sin`、`pow`、`log`
- 字串操作：`snprintf`、`strdup`
- 動態記憶體：`malloc`/`free`

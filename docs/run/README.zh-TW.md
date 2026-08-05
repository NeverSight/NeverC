**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 文件索引](../README.zh-TW.md) · [← NeverC 專案](../../README.md)

# `neverc run`

將 C 或 NeverC 程式編譯為**暫存可執行檔**，在**本機**執行，回傳其結束碼，然後刪除產物。工作流程刻意設計得類似 `go run`。

需要保留二進位檔、分發或用偵錯器除錯時，請使用一般編譯命令（`neverc ... -o output`）。

## 語法

```text
neverc run [編譯器選項] file.c [file2.nc ...] [程式參數...]
neverc run [編譯器參數...] -- [程式參數...]
```

也可執行 `neverc run --help` 查看內建摘要。

## 參數解析

`neverc run` 使用以下兩種規則之一，將參數拆分為**編譯器呼叫**和可選的**程式參數**。

### 預設（Go 風格）拆分

1. 由左向右掃描，找到第一個以 `.c` 或 `.nc` 結尾且不以 `-` 開頭的參數。
2. **第一個原始檔之前及連續 `.c`/`.nc` 原始檔**全部傳給編譯器。
3. 連續原始檔**之後**的參數傳給暫存程式的 `argv`。

範例：

```bash
# 編譯器：-O2 -fbuiltin-string hello.c
# 程式：（無）
neverc run -O2 -fbuiltin-string hello.c

# 編譯器：-O2 main.c helper.nc
# 程式：  --verbose two words
neverc run -O2 main.c helper.nc -- --verbose two words

# 編譯器：-DGENERATED=.c -O2 main.c
# 程式：  argument
neverc run -DGENERATED=.c -O2 main.c argument
```

說明：

- 只有 `.c` 和 `.nc` 會被當作 run 原始檔。以 `-` 開頭的參數（如 `-DGENERATED=.c`）始終留在編譯器側。
- 多個原始檔會編譯並連結成一個暫存二進位檔，與一般多檔編譯相同。

### 顯式 `--` 分隔

當編譯器需要在原始檔清單**之後**再接收參數（連結選項、非源輸入、`-x c -` 等）時，用 `--` 分隔編譯器尾部與程式參數：

```bash
# 編譯器：hello.c helper.o -lm
# 程式：  arg.c -x        （這些是 argv，不是編譯器選項）
neverc run hello.c helper.o -lm -- arg.c -x

# 編譯器：hello.c -O1
# 程式：  x
neverc run hello.c -O1 -- x
```

`--` 之前的所有內容會原樣轉發給 `neverc`（並附加內部 `-o <temp>`）；`--` 之後的內容成為程式參數。

## 執行時行為

| 主題 | 行為 |
|------|------|
| 工作目錄 | 暫存程式在**目前目錄**執行。相對路徑與一般二進位檔相同。 |
| 環境 | 繼承目前環境（`PATH`、已匯出變數等）。 |
| 標準 I/O | stdin、stdout、stderr 連接到暫存行程，管線與重新導向照常運作。 |
| 結束碼 | 成功時回傳**程式**結束碼。編譯失敗時回傳**編譯器**結束碼，且**不會**執行程式。 |
| 暫存檔 | 可執行檔位於唯一的 `neverc-run-*` 目錄。執行結束後刪除（無論程式成功或失敗）。清理失敗會單獨報錯。 |

## 範例

**帶最佳化與 string builtin 的快速執行：**

```bash
neverc run -O2 -fbuiltin-string hello.c
```

**向 `main` 傳參（含帶空格的參數）：**

```bash
neverc run -fbuiltin-string greet.c -- Alice "two words"
```

**編譯多個翻譯單元後執行：**

```bash
neverc run -O2 main.c util.nc -- --port 8080
```

**原始檔後面還有編譯器參數時使用 `--`：**

```bash
neverc run app.c extra.o -lm -- --config prod.json
```

## 限制與注意

- **僅本機執行。** `neverc run` 總是在呼叫 `neverc` 的機器上嘗試執行暫存二進位檔。交叉編譯選項（`-target ...`）也許仍能編譯，但產物通常無法在本機執行。
- **無持久產物。** 命令結束後二進位檔會被刪除，無法事後掛偵錯器。需要保留可執行檔時請用 `neverc ... -o out`。
- **與 `neverc` 同一工具鏈。** 該命令會重新呼叫處理 `run` 的同一個 `neverc` 二進位檔，轉發你的編譯選項（除內部 `-o` 外）。
- **`.nc` 原始檔。** 規則與 `.c` 相同；`.nc` 自動啟用的語言擴展照常生效。

## 相關命令

| 命令 | 適用場景 |
|------|----------|
| `neverc file.c -o out` | 保留二進位檔、交叉編譯或整合到建置腳本 |
| `neverc build` / `neverc make` | 基於 `neverc.toml` 的專案式建置 |
| `neverc run --help` | 內建用法摘要 |

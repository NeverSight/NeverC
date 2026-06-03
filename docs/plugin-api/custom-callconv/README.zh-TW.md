**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# 自訂呼叫慣例

NeverC 支援**資料驅動的自訂呼叫慣例** —— 你可以透過外部外掛程式或原始碼屬性，將任意實體暫存器指派給任意函式的參數與回傳值，無需修改編譯器本體或任何 TableGen 定義。

## 概述

傳統 LLVM 呼叫慣例透過 `.td` / `.inc` 檔案固化在後端。新增或修改慣例需要編輯編譯器原始碼並重新執行 TableGen。NeverC 以**執行期資料驅動**方案取代了這個流程：

- 一份**暫存器指派清單**（純字串）作為字串屬性附加到函式上。
- 後端讀取這份清單，將參數 / 回傳值指派到指定的實體暫存器。
- 清單可來自**外部外掛程式**（IR pass）、**原始碼屬性**（`__attribute__` / `__declspec`），或兩者併用。

呼叫慣例從「編譯期寫死在後端」變成「執行期由外部策略驅動」。

## 清單格式

清單是以分號分隔的字串。每段由 key 與逗號分隔的暫存器名組成（大小寫不敏感、容忍空白）：

```
gpr:rcx,rdx,r8,r9; xmm:xmm0,xmm1; ret:rax; ret_xmm:xmm0
```

| 段名 | 別名 | 含義 |
|---|---|---|
| `args` | | **位置模式**：每項為暫存器名或 `stack`/`mem`，按參數索引逐個指定 |
| `gpr` | `arg_gpr` | **池模式**：整數/指標參數暫存器，依序使用，用盡溢出到堆疊 |
| `xmm` | `arg_xmm` | **池模式**：浮點/向量參數暫存器 |
| `fpr` | `arg_fpr` | AArch64 的 `xmm` 別名 |
| `ret_gpr` | `ret` | 整數/指標回傳值暫存器 |
| `ret_xmm` | | 浮點/向量回傳值暫存器 |
| `ret_fpr` | | AArch64 的 `ret_xmm` 別名 |
| `csr` | | 自訂 callee-saved 暫存器集合（預設：標準 ABI 集合） |

### 兩種參數模式

**池模式**（`gpr:` / `xmm:`）：整數參數依序從 `gpr` 池取暫存器，浮點參數從 `xmm` 池取。池耗盡後其餘參數溢出到堆疊。

**位置模式**（`args:`）：第 *i* 個參數使用第 *i* 項 token。token 可以是暫存器名或 `stack` / `mem`（強制該參數走堆疊）：

```
args:rcx,stack,r8;ret:rax   # 參數0→rcx, 參數1→堆疊, 參數2→r8, 回傳→rax
```

`args` 段存在時優先於 `gpr` / `xmm`。型別不符、索引越界、暫存器已佔用等情況均回退到堆疊槽。

### 支援的架構

| 架構 | GPR 名 | SIMD 名 | 位寬選擇 |
|---|---|---|---|
| **x86-64** | `rax`, `rcx`, `rdx`, `rsi`, `rdi`, `r8`–`r11` | `xmm0`–`xmm15` | i32→32 位子暫存器, i64→64 位 |
| **AArch64** | `x0`–`x28` | `v0`–`v31` | i32→`w`, i64→`x`, f16→`h`, f32→`s`, f64→`d`, f128/vec→`q` |

### 限制

- **Callee-saved**：預設使用標準 ABI 集合。用 `csr:r12,r13` 宣告自訂集合（x86-64 與 AArch64 皆支援）。
- **保留暫存器**：堆疊指標（`rsp` / `sp`）以及 AArch64 的 `x29`/`x30`（FP/LR）永遠不能作為參數/回傳暫存器 —— spec 中寫到它們會被直接跳過。
- **csr 衝突**：若某暫存器同時出現在 `csr` 與參數/回傳清單中，bridge 會發出警告（callee 會保存/還原它，破壞其傳值作用）。
- **可變參數函式**：不支援 —— 編譯器輸出明確錯誤而非靜默錯傳參數。
- **間接呼叫**：函式指標呼叫無法攜帶自訂慣例。外掛程式在函式位址被取用時發出警告；間接呼叫回退到標準慣例。
- **尾呼叫**：自訂慣例函式自動停用尾呼叫。

## 用法

### 1. 外掛程式驅動（建議）

參考外掛程式 `CustomCallConvPlugin.c` 位於 `pluginsdk/examples/`。

**編譯外掛程式：**

```bash
cd pluginsdk/examples && make CustomCallConvPlugin.dylib
```

**屬性模式**（預設）—— 只影響有 `custom_attr` 原始碼標註的函式：

```bash
neverc -fplugin-pass=./CustomCallConvPlugin.dylib input.c -o output.o
```

**全域模式** —— 給所有函式套用（需顯式 `cc-all=1`）：

```bash
neverc -fplugin-pass=./CustomCallConvPlugin.dylib \
       -fplugin-pass-arg=cc-all=1 \
       -fplugin-pass-arg=ccspec="gpr:r10,r11,rsi;ret:rdx" \
       input.c -o output.o
```

### 2. 原始碼屬性

```c
__attribute__((custom_attr("neverc-callconv", "gpr:r10,r11,rsi;ret:rdx")))
int add3(int a, int b, int c) { return a + b + c; }

__declspec(custom_attr("neverc-callconv", "gpr:r10;ret:rdx"))
int msfunc(int a) { return a; }
```

### 3. 組合使用

原始碼屬性與外掛程式參數可同時使用。每個函式最多處理一次。

## LTO 支援

外掛程式同時註冊 `NEVERC_HOOK_POST_OPT` 與 `NEVERC_HOOK_LTO_POST_OPT`，確保 LTO 合併翻譯單元後仍能套用自訂慣例。

## 外掛程式 API

```c
API->FunctionSetCustomCallConv(F, "gpr:r10,r11,rsi;ret:rdx");
```

設定 `CallingConv::NeverC_Custom`（CC 1000）、寫入屬性、並**同步所有直接呼叫點**。傳入 `NULL` 或 `""` 可清除。

## 測試

GoogleTest 套件（22 項測試，全部 PASS）：

```bash
ninja -C build-neverc neverc-tests
build-neverc/bin/neverc-tests --gtest_filter='CustomCallConvTest.*'
```

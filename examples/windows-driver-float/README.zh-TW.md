**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# 帶浮點運算的 Windows 核心驅動程式

使用 NeverC 建置的 WDM 核心驅動程式，展示**核心態浮點 / SIMD 的安全使用方式**。
支援從 macOS / Linux 交叉編譯。

## 建置

```bash
cd examples/windows-driver-float
make
```

使用獨立的 NeverC 發行版：

```bash
make NEVERC=/path/to/neverc
```

輸出為 `FloatDriver.sys`（auto-LTO 最佳化）。
預設建置包含 `-g` 用於除錯；釋出版本應移除 `-g`。

---

## 兩個問題要處理

核心態浮點有兩個獨立的問題：

### 問題 1 — `_fltused` ABI 標記（編譯/連結期）

只要翻譯單元裡有任何浮點運算，MSVC 編譯器就會發出對符號 `_fltused`
的未定義引用。使用者態程式透過 `libcmt.lib` 提供該符號，連結器滿意，
同時拉入一些 FP 相關的 CRT 程式碼。

核心驅動程式**不**連結 `libcmt`（我們傳了 `-nostdlib` 和 `-Xlinker --nodefaultlib`），
所以未解析的 `_fltused` 會導致連結錯誤。

**NeverC 的解決方式**：使用 `-fms-kernel` 時，LLVM X86 後端會將 `_fltused`
本地定義為 0。可在產生的組合語言中看到：

```asm
# 使用者態目標：
    .globl  _fltused              # 外部引用 —— 需要 libcmt
```

```asm
# -fms-kernel 目標：
    .globl  _fltused
    .set    _fltused, 0           # 本地定義！無需外部符號
```

所以你**永遠不用手動在驅動中寫 `int _fltused = 0;`**。

### 問題 2 — 核心**不**保存 FP/SIMD 暫存器（執行期）

Windows 核心預設**不**在上下文切換時保存/還原 x87 / XMM / YMM / ZMM
暫存器。如果驅動從任意核心程式碼中碰這些暫存器，就會悄悄污染當前
執行在該 CPU 上的使用者態執行緒的 SIMD 狀態。

**解決方式**：用
[`KeSaveExtendedProcessorState`](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kesaveextendedprocessorstate)
和 `KeRestoreExtendedProcessorState` 包裹每段浮點 / SIMD 程式碼：

```c
XSTATE_SAVE save;
NTSTATUS status = KeSaveExtendedProcessorState(XSTATE_MASK_LEGACY, &save);
if (!NT_SUCCESS(status))
    return status;

// ... 你的 FP / SIMD 程式碼 ...

KeRestoreExtendedProcessorState(&save);
```

### XSTATE 遮罩

| 遮罩 | 涵蓋範圍 |
|------|---------|
| `XSTATE_MASK_LEGACY_FLOATING_POINT`（位元 0） | x87 堆疊 |
| `XSTATE_MASK_LEGACY_SSE`（位元 1） | XMM0–15 |
| `XSTATE_MASK_LEGACY` | 位元 0 \| 位元 1（涵蓋大多數普通 `double` / SSE 程式碼） |
| `XSTATE_MASK_GSSE` / AVX（位元 2） | YMM0–15 的高位部分 |
| `XSTATE_MASK_AVX512` | AVX-512 ZMM 暫存器 |

依程式碼使用的最寬暫存器，按位元 OR 組合後傳入。

---

## 這個驅動程式做了什麼

- 在 `\Device\FloatDriver` 建立裝置物件，在 `\DosDevices\FloatDriver` 建立符號連結
- 在 `DriverEntry` 中呼叫 `ComputeAreaSafe()`（用 FP 狀態保存/還原包裹 `ComputeArea()`），
  分別傳入 `radius=1.0` 和 `radius=5.0`
- 透過 `DbgPrint` 列印 double 的原始位元（因為 `DbgPrint` 不支援 `%f`
  —— 我們用 `RtlCopyMemory` 提取 64 位元模式）
- 透過 `-fms-kernel` 隱式定義 `_fltused`

## 驗證 `_fltused` 產生

對比使用 / 不使用 `-fms-kernel` 時編譯器的輸出：

```bash
# 使用者態（需要 libcmt）：
neverc --target=x86_64-pc-windows-msvc -S /tmp/foo.c -o - | grep fltused
#  .globl  _fltused

# 核心（本地定義為 0）：
neverc --target=x86_64-pc-windows-msvc -fms-kernel -S /tmp/foo.c -o - | grep fltused
#  .globl  _fltused
#  .set    _fltused, 0
```

## 載入（在 Windows 測試機上）

```cmd
sc create FloatDriver type= kernel binPath= C:\path\to\FloatDriver.sys
sc start FloatDriver
sc stop FloatDriver
sc delete FloatDriver
```

請啟用測試簽署或使用程式碼簽署憑證用於正式環境。

## 注意事項

- **`DbgPrint` 不支援 `%f`** —— 核心除錯輸出常式沒有浮點格式化能力。
  將 double 轉換為定點整數顯示，或像本範例一樣列印原始位元。
- **不要在 IRQL ≥ DISPATCH_LEVEL 使用浮點**，除非絕對必要。
  `KeSaveExtendedProcessorState` 文件說明了 IRQL 限制。
- **效能**：狀態保存/還原並非免費；對熱路徑應將 FP 工作打包進單個包裹區域。

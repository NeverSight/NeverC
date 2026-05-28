# 帶 CET 影子堆疊的 Windows 核心驅動程式

使用 NeverC 建置的最小 WDM 核心驅動程式，啟用了 Intel CET（控制流強制技術）
核心影子堆疊。支援從 macOS / Linux 交叉編譯。

## 建置

```bash
cd examples/windows-driver-cet
make
```

使用獨立的 NeverC 發行版：

```bash
make NEVERC=/path/to/neverc
```

輸出為 `CetDriver.sys`（auto-LTO 最佳化）。
預設建置包含 `-g` 用於除錯；**釋出版本應移除 `-g`** 以移除除錯符號並縮小二進位檔案大小。

## CET 專用旗標

| 旗標 | 層級 | 用途 |
|------|------|------|
| `-fcf-protection=return` | 編譯器 | 產生影子堆疊相容程式碼 |
| `-Xlinker --cetcompat` | 連結器 | 在 PE 中設定 `IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT` |

## 手動建置（不使用 Make）

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fcf-protection=return \
  -fms-kernel -fms-extensions -fms-compatibility \
  -D_AMD64_ -DNTDDI_VERSION=0x06010000 -D_WIN32_WINNT=0x0601 \
  -Wall -nostdlib -shared \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -Xlinker --cetcompat \
  -lntoskrnl -lhal \
  -o CetDriver.sys driver.c
```

## 功能說明

- 在 `\Device\CetDriver` 建立裝置物件
- 在 `\DosDevices\CetDriver` 建立符號連結
- 透過間接呼叫（`ComputeFn` 函式指標）驗證 CET 相容性——影子堆疊保護這些呼叫的返回位址
- 透過 `DbgPrint` 輸出載入/卸載訊息

---

## CET 技術詳解

CET 有**兩個獨立的保護機制**：

### 1. 影子堆疊 — 後向邊保護（RET）

硬體維護第二個堆疊（影子堆疊）來鏡像 CALL/RET。**不需要在函式標頭加入任何特殊指令**——完全透明：

```
┌─ CALL target ─────────────────────────────────┐
│                                                │
│  一般堆疊:   PUSH return_addr  (RSP)           │
│  影子堆疊:   PUSH return_addr  (SSP, 硬體)     │
│                                                │
└────────────────────────────────────────────────┘

┌─ RET ─────────────────────────────────────────┐
│                                                │
│  一般堆疊:   POP return_addr_A  (RSP)          │
│  影子堆疊:   POP return_addr_B  (SSP, 硬體)    │
│                                                │
│  比較: return_addr_A == return_addr_B ?         │
│    ✓ 相符   → 正常返回                         │
│    ✗ 不相符 → #CP 例外 (BUGCHECK)              │
│                                                │
└────────────────────────────────────────────────┘
```

影子堆疊管理指令（作業系統用於上下文切換，不放在函式標頭）：

```asm
RDSSPQ  rax         ; 讀取目前影子堆疊指標
INCSSPQ rax         ; 推進 SSP（丟棄條目）
SAVEPREVSSP         ; 儲存前一個影子堆疊權杖
RSTORSSP [addr]     ; 還原到已儲存的影子堆疊
WRSS  [addr], rax   ; 寫入管理員影子堆疊
WRUSS [addr], rax   ; 寫入使用者影子堆疊（僅 ring 0）
SETSSBSY            ; 標記目前影子堆疊為忙碌
CLRSSBSY [addr]     ; 清除忙碌標記
```

### 2. 間接分支追蹤（IBT） — 前向邊保護（間接 CALL/JMP）

需要在每個合法的間接呼叫/跳轉目標處放置 `ENDBR64` 指令（`F3 0F 1E FA`，4 位元組）。
在不支援 CET 的 CPU 上，`ENDBR64` 等效於 NOP。

```
┌─ 間接 CALL/JMP ──────────────────────────────┐
│                                               │
│  CPU 設定內部 TRACKER = WAIT_FOR_ENDBR        │
│  跳轉到目標位址...                             │
│                                               │
│  目標處第一條指令是 ENDBR64 ?                   │
│    ✓ 是 → 清除 TRACKER，正常執行               │
│    ✗ 否 → #CP 例外                            │
│                                               │
│  直接 CALL/JMP 不設定 TRACKER                  │
│                                               │
└───────────────────────────────────────────────┘
```

### Windows 核心的選擇

| 保護 | 機制 | Windows 核心是否使用？ |
|------|------|----------------------|
| 後向邊（RET） | CET 影子堆疊 | **是** (KCET) |
| 前向邊（間接 CALL/JMP） | CET IBT (ENDBR) | **否** — 用 CFG 取代 |

因此預設使用 `-fcf-protection=return`：僅啟用影子堆疊，不產生 ENDBR64。
如需 ENDBR64 可改為 `-fcf-protection=full`（在 Windows 上為無害 NOP，但提供 Linux 移植相容性）。

### 組合語言對比：`-fcf-protection` 各模式

原始碼：

```c
unsigned long rotate13(unsigned long val) {
    return (val << 13) | (val >> 19);
}
```

#### `-fcf-protection=none`（無 CET）

```asm
rotate13:
    mov  eax, ecx
    rol  eax, 13
    ret
```

#### `-fcf-protection=return`（僅影子堆疊——本範例使用此模式）

```asm
rotate13:
    mov  eax, ecx      ; 與 "none" 完全相同！
    rol  eax, 13        ; 影子堆疊完全透明——
    ret                 ; 硬體在 CALL/RET 時自動操作
```

程式碼產生與 `none` **完全相同**。唯一區別是連結器旗標 `--cetcompat` 在 PE 除錯目錄中
設定 `IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT` 位元，告知 Windows 此二進位檔案
相容影子堆疊。

#### `-fcf-protection=full`（影子堆疊 + IBT）

```asm
rotate13:
    endbr64             ; ← IBT 標記 (F3 0F 1E FA)
    mov  eax, ecx       ;    在非 CET CPU 上為 NOP
    rol  eax, 13        ;    Windows 不使用（CFG 處理前向邊）
    ret
```

`ENDBR64` 出現在每個函式入口。在 Windows 上每個函式浪費 4 位元組，但不會造成問題。

---

## 在目標機器上啟用 KCET

```cmd
reg add HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity /v Enabled /t REG_DWORD /d 1 /f
reg add HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\KernelShadowStacks /v Enabled /t REG_DWORD /d 1 /f
```

需要重新啟動。透過 `msinfo32.exe` → 「核心模式硬體強制堆疊保護」驗證。

**需求：**

- 目標機器啟用 HVCI
- Windows build 21389 或更高
- 支援 CET 的 CPU（Intel Tiger Lake+ / AMD Zen 3+）

## 載入

```cmd
sc create CetDriver type= kernel binPath= C:\path\to\CetDriver.sys
sc start CetDriver
sc stop CetDriver
sc delete CetDriver
```

請啟用測試簽署或使用程式碼簽署憑證用於正式環境。

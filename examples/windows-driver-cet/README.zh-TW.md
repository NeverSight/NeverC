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

## 編譯器 vs bin2bin：誰對 CET 友善？

CET 在**原始碼級編譯器**和 **bin2bin 工具**（加殼、混淆、hook、dump+rebuild）
之間畫了一道清晰的紅線。硬體 Shadow Stack 強制三條規則，重塑整個防護 /
混淆產業：

> 1. **不要修改返回位址。**
> 2. **不要自我修改程式碼**（HVCI 強制程式碼頁 W^X）。
> 3. **尋找尊重 1、2 的強混淆變換。**

### 編譯器能「加密返回位址」嗎？

**不能。** 這是常見誤解。Shadow Stack 由 CPU 而非作業系統強制，對使用者
態程式碼不可見。如果你在函式 epilogue 裡 XOR 加密主堆疊上的返回位址：

```c
void my_func() {
    // ... 函式本體 ...
    // epilogue 嘗試加密返回位址：
    // XOR [rsp], 0xDEADBEEF
    // RET           <- 硬體比較主堆疊 vs 影子堆疊
                     //   不再匹配 -> #CP 例外 -> BUGCHECK
}
```

影子堆疊仍保存著原始返回位址。RET 觸發硬體比較；不匹配則觸發 `#CP`，
BUGCHECK 核心。編譯器**無法**觸及影子堆疊：

- 使用者態：沒有任何指令能寫影子堆疊
- 核心態：`WRSSQ` 是特權指令，只有 `ntoskrnl` 使用

### 編譯器能做的 CET 友善混淆

| 變換 | 為何 CET 安全 |
|------|--------------|
| **控制流扁平化** | switch dispatcher 用直接 CALL/JMP；cases 需要時加 ENDBR64 |
| **VM 虛擬化** | handler 之間用間接 JMP（帶 ENDBR64）連接，不用 push+ret |
| **字串 / 常數加密** | 純資料變換，不影響控制流 |
| **MBA 表達式** | `x + y → (x ^ y) + 2*(x & y)` —— 僅資料 |
| **不透明謂詞** | 透過直接跳轉實現條件分支 |
| **函式克隆 / 內聯** | 不改變呼叫堆疊語義 |
| **指令替換** | `MOV → XOR + ADD` —— 不影響堆疊 |

### CET 敵對模式（KCET 下會崩潰）

| 模式 | 為何不可行 |
|------|----------|
| **返回位址加密** | 影子堆疊不匹配 → `#CP` |
| **PUSH addr; RET dispatcher**（經典 VMProtect / Themida 風格） | 同上 —— 影子堆疊沒有 `addr` 這一項 |
| **Stack pivoting**（ROP gadget chain） | 影子堆疊無法跟隨 pivot |
| **自我修改程式碼** | HVCI 阻止對可執行頁的寫入 |
| **執行期程式碼產生** | 同上 —— HVCI W^X 違規 |
| **基於 trampoline 的 inline hook** | 修改函式 prologue 觸發 HVCI；即使繞過 HVCI，trampoline RET 處的影子堆疊也會出問題 |

### 為何 bin2bin 工具有結構性劣勢

編譯器從語義化的 IR 產生 CET 正確的程式碼。bin2bin 工具必須從編譯後的位元組
**重新發現**語義：

1. **指令邊界歧義** —— x86 是變長指令。在錯誤位移加 ENDBR64（4 位元組）會破壞所有 RIP-relative 定址和重定位。
2. **間接目標識別** —— bin2bin 不能總是判斷 `.data` 裡哪些位址是跳轉表項 vs 原始資料。要麼過度標記（程式碼膨脹、新的 ROP gadget 起點），要麼標記不足（執行期 `#CP`）。
3. **自證危險** —— 設定 `IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT` 是一種承諾。如果 bin2bin 輸出包含任何 CET 敵對模式，驅動在非 CET 機器上能載入，但在 KCET 主機上立刻 BSOD。
4. **CFG 完整性** —— 編譯器看到完整呼叫圖；bin2bin 必須推斷，沒有精確目標的間接呼叫迫使保守的 ENDBR 放置。

### 產業現狀

| 工具 / 類別 | CET 狀態 |
|------------|---------|
| **NeverC / Clang / MSVC（編譯器）** | 透過 `-fcf-protection` + 連結器旗標原生 CET 友善 |
| **OLLVM / Tigress / NeverC passes** | IR 級變換 → 天然 CET 安全 |
| **Microsoft Detours (4.0+)** | 已更新為 CET 相容 |
| **VMProtect / Themida（舊版）** | Push+RET dispatcher 在 KCET 主機上殺死驅動 |
| **VMProtect / Themida（新版）** | 添加 ENDBR-aware dispatcher，混合支援 |
| **Manual map / dump+rebuild loader** | 必須重建所有 ENDBR 標記 —— 容易出錯 |

### 遊戲安全視角

反作弊驅動（EAC、BattlEye、FACEIT AC、Vanguard）出廠時設定了 `--cetcompat`，
因此可以在啟用 KCET 的機器上正常執行。
作弊驅動——通常透過 bin2bin 工具加殼、hook 或 trampoline 注入——很難保持
CET 合規。KCET + HVCI 形成一道**「編譯器友善、bin2bin 敵對」的硬體壁壘**，
不對稱地有利於工程化良好的安全軟體，而非惡意程式碼風格的程式。

這就是 Microsoft 對核心軟體如此推動 KCET 的更深層原因：讓合法核心程式碼
更容易強化，同時讓攻擊者技術逐漸變得更難。

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

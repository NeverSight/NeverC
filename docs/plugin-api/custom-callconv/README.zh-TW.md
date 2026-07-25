**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 外掛 ABI](../README.zh-TW.md)

# 自訂呼叫慣例

NeverC 支援**資料驅動的自訂呼叫慣例** —— 你可以完全透過外部外掛或原始碼屬性，把任意實體暫存器指派給任意函式的引數與回傳值，無需修改編譯器本體，也不用改動任何 TableGen 定義。

## 概觀

傳統 LLVM 呼叫慣例透過 `.td` / `.inc` 檔固化在後端。新增或修改一個慣例必須編輯編譯器原始碼並重新執行 TableGen。NeverC 以**執行期資料驅動**模型取代這套流程，它由兩層構成：

- **spec** —— 一個人類可手寫的短字串，例如 `gpr:rcx,rdx;ret:rax` —— 由外掛或原始碼屬性作為 `"neverc-callconv"` 字串屬性附加到函式上。
- 在程式碼產生之前，宿主會把這份 spec **具現化**成 `"neverc-cc-plan-v1"` 屬性：一張不可變、已通過驗證的精確位置表，並綁定到特定的目標 schema。後端只消費 plan。

spec 是你寫的東西，plan 是後端信任的東西。呼叫慣例因此從「編譯期寫死在後端」變成「執行期由外部策略驅動」，同時沒有放棄驗證。

## Spec 格式

spec 是以分號分隔的字串。每一段由一個 key 與逗號分隔的暫存器名稱清單組成（不分大小寫，容忍空白）：

```
gpr:rcx,rdx,r8,r9; xmm:xmm0,xmm1; ret:rax; ret_xmm:xmm0
```

| 段名 | 別名 | 意義 |
|---|---|---|
| `args` | | **位置模式**：每一項是暫存器名稱或 `stack`/`mem`，依引數索引逐一對應 |
| `gpr` | `arg_gpr` | **池模式**：整數/指標引數暫存器，依序取用，用盡後溢位到堆疊 |
| `xmm` | `arg_xmm` | **池模式**：浮點/向量引數暫存器 |
| `fpr` | | `xmm` 的目標中立別名 |
| `ret_gpr` | `ret` | 整數/指標回傳值暫存器 |
| `ret_xmm` | | 浮點/向量回傳值暫存器 |
| `ret_fpr` | | `ret_xmm` 的目標中立別名 |
| `csr` | | 自訂 callee-saved 暫存器集合（預設為標準 ABI 集合） |

任何一段都可以省略，無法辨識的段會被忽略。這些 key 只在 [`llvm/include/llvm/CodeGen/NeverCCallConv.h`] 定義一次，因此產生者與剖析器不會產生偏離。

### 兩種引數模式

**池模式**（`gpr:` / `xmm:`）：整數引數依序從 `gpr` 池取暫存器，浮點與向量引數從 `xmm` 池取。某個池耗盡後，其餘引數溢位到堆疊。

**位置模式**（`args:`）：第 *i* 個引數使用第 *i* 項 token。每項要嘛是暫存器名稱，要嘛是 `stack` / `mem`，後者強制該引數走堆疊：

```
args:rcx,stack,r8;ret:rax   # 引數0→rcx、引數1→堆疊、引數2→r8、回傳值→rax
```

`args` 段存在時優先於 `gpr` / `xmm`。若某項 token 與引數型別的暫存器類別不符、索引超出 token 清單範圍，或暫存器已被占用，都會回退到堆疊槽，而不會讓編譯失敗。

### 支援的架構

暫存器名稱透過依目標劃分的表格解析，該表格是 spec 可以書寫哪些名稱的唯一依據。

| 架構 | GPR 名稱 | SIMD 名稱 | 位寬選擇 |
|---|---|---|---|
| **x86-64** | `rax`、`rbx`、`rcx`、`rdx`、`rsi`、`rdi`、`rbp`、`r8`–`r15` | `xmm0`–`xmm15` | i32 → 32 位元子暫存器，i64/指標 → 64 位元 |
| **AArch64** | `x0`–`x28` | `v0`–`v31` | i32→`w`，i64→`x`，f16→`h`，f32→`s`，f64→`d`，f128/向量→`q` |

GPR 一律寫成 64 位元形式，後端會依每個值的型別收窄到對應的子暫存器。AArch64 的向量暫存器寫作 `v0`–`v31`，後端依型別挑選 `H`/`S`/`D`/`Q` 形式。

### 限制

- **保留暫存器**：堆疊指標不在兩張表格內（x86-64 的 `rsp`，AArch64 的 `sp`/`x31`），AArch64 的 `x29`/`x30`（FP/LR）同樣不在。spec 裡寫到它們會被直接略過，該值落到下一個合法位置。
- **框架指標**：x86-64 上 `rbp` **是**可選的，因為它本來就是合法的 callee-saved 暫存器；但把它當引數暫存器只在 `-fomit-frame-pointer` 下才成立，風險自負。
- **Callee-saved**：預設使用標準 ABI 集合。`csr:r12,r13` 宣告自訂集合，呼叫方會建構與之相符的保留暫存器遮罩，藉此得知哪些暫存器能跨呼叫存活。x86-64 與 AArch64 皆支援。
- **csr 衝突**：若某暫存器同時出現在 `csr` 與引數/回傳清單中，外掛會發出警告 —— callee 會還原它，從而破壞它的傳值作用。編譯仍會成功。
- **可變引數函式**：不支援。兩個後端都會給出明確診斷，而不是靜默地錯傳可變引數部分。
- **間接呼叫**：函式指標呼叫無法攜帶自訂慣例。當自訂慣例函式的位址被取用時外掛會警告；間接呼叫回退到標準慣例。
- **尾呼叫**：只要呼叫的任一側使用自訂慣例，兩個後端都會停用尾呼叫。
- **未涵蓋的值**：plan 未涵蓋的引數或回傳值回退到目標的標準慣例（x86-64 用 SysV，AArch64 用 AAPCS）。

## 用法

### 1. 外掛驅動（建議）

參考外掛 [`CustomCallConvPlugin.c`] 位於 `pluginsdk/examples/`。它在 `neverc.ir.pass.post_opt` 階段註冊了一個模組層級的 IR pass。

**編譯外掛：**

```bash
cd pluginsdk/examples && make CustomCallConvPlugin.dylib   # 或 .so / .dll
```

**屬性模式**（預設）—— 只影響帶有 `custom_attr` 原始碼標註的函式：

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib input.c -o output.o
```

**全域模式** —— 為每個已定義函式套用同一份 spec（需要明確給出 `cc-all`）：

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib \
       -fplugin-arg=org.neverc.example.custom-callconv:cc-all \
       -fplugin-arg=org.neverc.example.custom-callconv:ccspec="gpr:r10,r11,rsi;ret:rdx" \
       input.c -o output.o
```

**依名稱前綴過濾：**

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib \
       -fplugin-arg=org.neverc.example.custom-callconv:cc-all \
       -fplugin-arg=org.neverc.example.custom-callconv:ccprefix=secret_ \
       -fplugin-arg=org.neverc.example.custom-callconv:ccspec="gpr:r9,r8;ret:rax" \
       input.c -o output.o
```

**多樣化** —— 在四種內建配置間輪替，使函式之間不共用同一份配置（反逆向）：

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib \
       -fplugin-arg=org.neverc.example.custom-callconv:cc-all \
       -fplugin-arg=org.neverc.example.custom-callconv:ccshuffle \
       input.c -o output.o
```

外掛註冊的四個選項是 `cc-all` 與 `ccshuffle`（旗標型，`=1` 或 `=true` 可省略），以及 `ccspec` 與 `ccprefix`（字串值）。未給出 `ccspec` 時，全域模式使用預設值 `gpr:r10,r11,rsi,rdi;ret:rdx`。

### 2. 原始碼屬性

在 C 原始碼中用 `custom_attr` 屬性直接標註函式，支援 GNU 與微軟兩種語法：

```c
// GNU 語法
__attribute__((custom_attr("neverc-callconv", "gpr:r10,r11,rsi;ret:rdx")))
int add3(int a, int b, int c) { return a + b + c; }

// 微軟語法
__declspec(custom_attr("neverc-callconv", "gpr:r10;ret:rdx"))
int msfunc(int a) { return a; }
```

`custom_attr("key", "value")` 產生乾淨的函式字串屬性（`"key"="value"`），**不產生**警告，**也不會進入** `llvm.global.annotations`。這是一個**通用**機制 —— 任意 key/value 都可以，不限於呼叫慣例。IR 與 MIR pass 用 `F.getFnAttribute("key")` 讀回。

### 3. 組合使用

原始碼屬性與外掛引數可以同時使用。帶 `custom_attr` 的函式走外掛的屬性模式路徑，`cc-all` 涵蓋其餘函式。每個函式最多被處理一次。

## 具現化的 plan

spec 只指定暫存器名稱，並沒有說明每個值的每個位元組落在哪裡。在最佳化管線結束之後、程式碼產生之前，宿主會執行 `materializeCallingConventionPlans`，把每個 `CallingConv::NeverC_Custom` 函式轉換成精確且已驗證的 plan：

- 已經帶有 `"neverc-cc-plan-v1"` 屬性的函式只會被**驗證，不會被重新產生** —— 它的 schema 摘要、目標 ID 與慣例 ID 必須與目前目標一致。
- 帶有 `"neverc-callconv"` spec 的函式，其暫存器名稱會對照目標暫存器表格解析。產生的 plan 取代該 spec，spec 隨後從 IR 中移除。
- 兩者都沒有、但其目標透過外掛 ABI 註冊了呼叫慣例的函式，由該慣例的 `PlanCallingConvention` 回呼來規劃。

每個直接呼叫點都會繼承被呼叫方的 plan，這正是呼叫方與被呼叫方跨翻譯單元維持配置一致的原因。plan 是一個扁平字串：

```
neverc-cc-plan-v1;schema=<摘要>;target=<high>:<low>;cc=<high>:<low>;stack=<位元組>;returns=<位置>;arguments=<位置>;callee-saved=<暫存器編號>
```

每個位置的格式是 `<r|s>,<值索引>,<片段位移>,<大小>,<對齊>,<暫存器編號>,<堆疊位移>,<旗標>`，多個位置之間以 `|` 分隔。內建路徑的 schema 摘要是 `llvm-<目標三元組>`；由外掛註冊的目標則提供自己的摘要。

由於暫存器編號只在定義它的 schema 下才有意義，不相符會直接報錯，而不是靜默產生錯誤程式碼：

| 情形 | 診斷訊息 |
|---|---|
| plan 字串無法剖析 | `malformed NeverC calling convention plan` |
| schema 摘要不一致 | `NeverC calling convention plan belongs to a foreign target schema` |
| 目標 ID 不一致 | `NeverC calling convention plan has a foreign target ID` |
| 慣例 ID 不一致 | `NeverC calling convention plan has a foreign convention ID` |

正是這一點讓 plan 可以安全地嵌入 bitcode 並穿過 LTO：為另一個目標產生的 plan 不可能被誤用。

## 外掛 API

範例外掛只用到穩定的 IR core 表 —— 並不存在專用的呼叫慣例進入點。為一個函式施加慣例，是三次呼叫加上呼叫點同步：

```c
NevercIRAttributeHandle Attribute = {0};
Core->CreateStringAttribute(Core->Context, Task, SV("neverc-callconv"), Spec,
                            &Attribute);
Core->AddFunctionAttribute(Core->Context, Task, Function,
                           NEVERC_IR_ATTRIBUTE_LOCATION_FUNCTION, 0, Attribute);
Core->SetFunctionCallingConvention(Core->Context, Task, Function,
                                   NEVERC_IR_CALLING_CONVENTION_NEVER_C_CUSTOM);
```

`NEVERC_IR_CALLING_CONVENTION_NEVER_C_CUSTOM` 是 `CallingConv::NeverC_Custom`（LLVM 值 1000）在 ABI 層的穩定名稱。接著外掛用 `GetValueUseCount` / `GetValueUse` 走訪該函式的所有使用點，對每一個作為 `call`、`invoke` 或 `callbr` 被呼叫方運算元的使用點，透過 `SetInstructionProperty` 搭配 `NEVERC_IR_PROPERTY_CALLING_CONVENTION` 為指令設定相同的慣例。其餘任何使用點都代表位址發生了逃逸，這正是「位址被取用」警告的來源。

如果外掛註冊了自己的目標，也可以在其 `NevercCallingConventionDescriptor` 上提供 `PlanCallingConvention` 回呼直接產出 plan，跳過 spec 這一層。參見[目標、MC、組譯與目的檔](../target-mc-object.zh-TW.md)。

## 測試

GoogleTest 套件位於 [`tests/neverc/CustomCallConvTests.cpp`]，共 26 個測試。每個測試都會建置範例外掛、在給定 spec 下把一小段程式編譯成組合語言，然後斷言最終的暫存器或堆疊位置。

```bash
ninja -C build-neverc neverc-tests
build-neverc/bin/neverc-tests --gtest_filter='CustomCallConvTest.*'
```

涵蓋範圍：

| 類別 | 測試數 |
|---|---|
| x86-64 池 / 位置 / 堆疊 / 溢位 / i64 / sret / byval / 回退 | 9 |
| AArch64 GPR / FPR / 堆疊 / `csr` / 非統一 spec 跨呼叫 | 5 |
| 前端 `custom_attr`（GNU / `__declspec` / 端到端） | 3 |
| plan 具現化與 schema 拒絕 | 3 |
| 強化（`csr`、兩個目標上的可變引數、間接呼叫、`rsp`、csr 衝突） | 6 |

## 架構

```
原始碼屬性                     外掛 IR pass
custom_attr(...)              (neverc.ir.pass.post_opt)
       │                            │
       └─────────────┬──────────────┘
                     ▼
   "neverc-callconv" = spec、CallingConv::NeverC_Custom
   施加到函式及其直接呼叫點
                     │
                     ▼
   ┌──────────────────────────────────────────┐
   │ materializeCallingConventionPlans        │
   │ （最佳化之後、程式碼產生之前）           │
   │                                          │
   │  spec       → 把名稱解析成實體暫存器     │
   │  外掛慣例   → PlanCallingConvention      │
   │  既有 plan  → 驗證 schema / 目標         │
   └──────────────────────────────────────────┘
                     │
                     ▼
   "neverc-cc-plan-v1" = 已驗證的位置表
   spec 被移除；plan 複製到各直接呼叫點
                     │
                     ▼
   ┌──────────────────────────────────────────┐
   │ 後端 CCAssignFn（每個目標一份）          │
   │  CC_X86_NeverC     / RetCC_X86_NeverC    │
   │  CC_AArch64_NeverC / RetCC_AArch64_NeverC│
   │                                          │
   │  讀取 plan → 指派位置                    │
   │  未涵蓋的值 → 標準慣例                   │
   │  停用尾呼叫                              │
   └──────────────────────────────────────────┘
                     │
                     ▼
   使用自訂暫存器配置的機器碼
```

後端執行器是**一次性實作** —— 所有策略決策都在外掛裡。新增一個慣例永遠不需要重新建置 NeverC。

<!-- reference links -->
[`CustomCallConvPlugin.c`]: ../../../pluginsdk/examples/CustomCallConvPlugin.c
[`llvm/include/llvm/CodeGen/NeverCCallConv.h`]: ../../../llvm/include/llvm/CodeGen/NeverCCallConv.h
[`tests/neverc/CustomCallConvTests.cpp`]: ../../../tests/neverc/CustomCallConvTests.cpp

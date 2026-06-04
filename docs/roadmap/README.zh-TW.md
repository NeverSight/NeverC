**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 文件索引](../README.zh-TW.md)

# NeverC 路線圖

本文件概述 NeverC 專案在現有 shellcode 編譯器和內建執行時之外的主要規劃方向。

---

## 1. 標準函式庫 (`std`)

NeverC 將提供一套完整的標準函式庫，參照 Go 標準函式庫設計——提供開箱即用的套件，涵蓋常見系統程式設計需求，無需外部相依性。

### 計畫中的套件

| 套件 | 描述 |
|------|------|
| `fmt` | 格式化 I/O（printf 系列 + 型別安全擴充） |
| `os` | 作業系統互動：環境變數、行程管理、檔案權限 |
| `io` | Reader/Writer 介面、緩衝 I/O、管線工具 |
| `fs` | 檔案系統操作：走訪、glob、暫存檔、原子寫入 |
| `net` | TCP/UDP 通訊端、DNS 解析、HTTP 客戶端/伺服器 |
| `net/http` | HTTP/1.1 與 HTTP/2 客戶端及伺服器 |
| `crypto` | 雜湊（SHA-256、SHA-512、BLAKE3）、HMAC、AES、ChaCha20、RSA、Ed25519 |
| `encoding` | JSON、Base64、Hex、CSV、二進位（大小端序） |
| `sync` | 互斥鎖、讀寫鎖、WaitGroup、Once、原子操作 |
| `time` | 單調/掛鐘時間、時長、計時器、格式化 |
| `bytes` | 位元組切片操作、緩衝區 |
| `math` | 數學常數、基本函式、亂數產生 |
| `sort` | 泛型排序與搜尋 |
| `container` | 鏈結串列、堆積、環形緩衝區 |
| `log` | 帶層級的結構化日誌 |
| `flag` | 命令列旗標解析 |
| `path` | 路徑操作（POSIX 和 Windows） |
| `regexp` | 正規表示式比對（RE2 語法） |
| `compress` | gzip、zlib、zstd、lz4 |
| `hash` | CRC32、CRC64、FNV、xxHash |
| `unicode` | Unicode 表、大小寫折疊、UTF-8/UTF-16 轉換 |

### 設計原則

- **純 C23** — 每個套件都以標準 NeverC/C23 編譯；無隱藏 C++ 或平台特定組合語言
- **零外部相依性** — 標準函式庫以 LLVM bitcode 嵌入編譯器，與現有的 `string` 和 `mimalloc` 內建功能一致
- **跨平台** — 所有套件在 macOS、Linux、Windows（x86_64 / AArch64）上運作
- **Shellcode 相容** — 在獨立模式下有意義的套件（如 `crypto`、`encoding`、`bytes`）支援 `-fshellcode`

---

## 2. UI 元件庫 (`neverc-ui`)

NeverC 將提供類似 Qt 的跨平台 UI 元件庫——但採用 HTML/JS/CSS 前端渲染引擎，天然適合 AI 生成介面。

### 目標

- **元件化架構** — 視窗、按鈕、文字輸入、列表、樹狀結構、表格、選單、對話方塊、分頁標籤和佈局容器作為一等 C 型別
- **HTML/JS/CSS 渲染器** — 透過內嵌輕量級瀏覽器引擎渲染 UI；開發者撰寫 C 邏輯，視覺層使用標準 Web 技術
- **拖曳式視覺化設計器** — 配套 GUI 建構器，產生 NeverC 相容的 C 程式碼，無需手寫佈局即可快速建立原型
- **AI 原生設計流程** — LLM 可一次生成 C 業務邏輯和 HTML/CSS 佈局，因為視覺層使用的是全世界最廣泛理解的 UI 語言
- **原生外觀** — 透過 CSS 變數和系統字型/色彩偵測實現平台自適應主題（macOS、Windows、Linux）
- **輕量級嵌入** — 渲染器作為內建執行時提供（類似 `string` / `mimalloc`）；沒有 Electron 等級的負擔
- **事件系統** — 使用者互動的 C 回呼函式（點擊、輸入、調整大小、拖曳、鍵盤、自訂事件）
- **資料繫結** — C 結構體與 UI 狀態之間的宣告式繫結；變更自動傳播
- **自訂渲染** — 透過原始 canvas/WebGL 進行遊戲 UI、資料視覺化或自訂控件

### 為什麼用 HTML/CSS 做 C 的 UI 庫？

- 每個 AI 模型都已經掌握 HTML/CSS——生成 UI 程式碼無需專門訓練
- Web 技術是經過最充分驗證的佈局系統；無需重新發明 flexbox、grid 或文字渲染
- 安全研究工具（儀表板、十六進位檢視器、封包檢查器）受益於豐富的樣式介面，無需學習專有控件 API
- 視覺化設計器匯出的 HTML 範本既可在 NeverC 應用中使用，也可在獨立瀏覽器中快速迭代

---

## 3. IDE 與語言工具 (`neverc-ide`)

NeverC 將為 `.nc` 語言擴充提供一流的 IDE 支援——VSCode 擴充實現即時生產力，獨立 NeverC IDE 提供完全整合的開發體驗。

### VSCode 擴充

- **語法醒目提示** — 完整 `.nc` 語法，支援 NeverC 特有型別的語義 token（`string`、`u8`–`u64`、`i8`–`i64`、`f32`、`f64`）
- **智慧補全** — 內建型別、點呼叫方法（`.c_str()`、`.len()`、`.starts_with()`）和 `#include` 路徑的自動補全
- **診斷** — 即時顯示 `neverc` 編譯器的錯誤和警告
- **跳至定義** — 跨翻譯單元跳轉到函式、結構體和巨集定義
- **懸停文件** — 內建函式、編譯器內建和標準函式庫套件的內嵌文件
- **程式碼動作** — 常見錯誤的快速修復建議，`std` 套件的自動匯入
- **偵錯** — 整合 LLDB/GDB 偵錯配接器，支援中斷點、逐步和變數檢查
- **Shellcode 模式** — 針對 `-fshellcode` 管線的語法感知功能：壞位元組醒目提示、shellcode 大小顯示、目標特定補全
- **外掛 API 整合** — 外掛掛鈎點視覺化和鷹架

### 獨立 IDE

- **基於 NeverC UI (`neverc-ui`)** — IDE 本身是 HTML/JS/CSS 元件庫的展示，用自己的 UI 框架建構
- **整合終端** — 無需離開 IDE 即可建置、執行和偵錯
- **視覺化 shellcode 管線** — IR → MIR → 擷取管線的圖形視圖，逐 pass 輸出檢查
- **專案範本** — 一鍵鷹架：宿主二進位、shellcode、EVM 合約、Solana 程式
- **AI 輔助編碼** — 內建 LLM 整合，理解 NeverC 語義，產生 `.nc` 程式碼，解釋編譯器診斷
- **跨編譯儀表板** — 視覺化目標選擇器，平台矩陣和建置狀態

### 為什麼同時做 VSCode 和獨立 IDE？

- VSCode 涵蓋了大多數已在該生態中的開發者
- 獨立 IDE 為安全研究員提供更深入的、專門建構的體驗，包含 shellcode 管線視覺化和整合二進位分析
- 兩者共享同一個語言伺服器後端——改進同時惠及兩者

---

## 4. EVM 智慧合約後端

NeverC 將支援把 C 原始碼編譯為 EVM（以太坊虛擬機）位元組碼——讓開發者能用 C 取代 Solidity 撰寫智慧合約。

### 目標

- **新 LLVM 後端目標** — `evm` 目標三元組（如 `neverc --target=evm hello.c -o contract.bin`）
- **ABI 相容** — 產生 Solidity 相容的 ABI 描述符，合約可與現有以太坊工具鏈（Hardhat、Foundry、ethers.js）互動
- **儲存布局** — 將 C 結構體對映到 EVM 儲存槽，布局具確定性
- **內建 EVM 原語** — `msg.sender`、`msg.value`、`block.number`、`tx.origin` 作為內建變數或內建函式
- **payable / view / pure 修飾符** — 對映到 Solidity 可見性語義的函式屬性
- **事件發射** — 從標註的函式呼叫產生 `LOG0`–`LOG4` 操作碼
- **Gas 最佳化** — IR pass 最小化 gas 開銷（堆疊排程、常數折疊、死儲存消除）
- **revert / require** — 帶自訂錯誤訊息的錯誤處理原語

### 為什麼用 C 寫 EVM？

- Solidity 的語法對 JavaScript 開發者友好，但對系統程式設計師陌生；C 是通用語言
- NeverC 現有的 IR 最佳化管線在許多場景下能產生比 `solc` 更緊湊的位元組碼
- 安全研究員已經用 C 思考——用 C 撰寫稽核工具和 fuzzer 對 C 合約是天然匹配
- 外掛 API 允許在編譯期進行自訂 gas 分析和弱點偵測 pass

---

## 5. Solana eBPF 後端

NeverC 將支援把 C 原始碼編譯為 Solana 的 eBPF 位元組碼——實現用 C 開發鏈上程式。

### 目標

- **eBPF 目標** — `sbf`（Solana BPF）目標三元組（如 `neverc --target=sbf-solana hello.c -o program.so`）
- **Solana 執行時繫結** — 內建 Solana 系統呼叫標頭檔：`sol_invoke_signed`、`sol_log`、`sol_memcpy`、帳戶資訊結構體
- **帳戶模型** — C 結構體覆蓋 Solana 帳戶資料，自動序列化/反序列化
- **CPI（跨程式呼叫）** — 型別安全的包裝器，用於呼叫其他鏈上程式
- **PDA（程式衍生地址）** — 內建 PDA 推導和驗證函式
- **運算預算感知** — 當估計的運算單元超出程式限制時發出編譯器警告
- **Anchor 相容** — 可選 IDL 產生，與 Anchor 前端互操作

### 為什麼用 C 寫 Solana？

- Solana 執行時本身執行 eBPF——C 是 BPF 目標最自然的原始語言
- 現有基於 C 的 BPF 工具鏈（clang + solana-bpf）設定複雜；NeverC 將一切打包到單一二進位
- 效能關鍵的程式受益於 C 的零開銷抽象和 NeverC 的最佳化 pass
- shellcode 編譯經驗（位置無關、最小執行時程式碼）直接對映到鏈上程式約束

---

## 時程

這些功能目前處於研究和設計階段。暫不承諾具體發佈日期。進展將在本文件中更新，並在專案發佈頁公佈。

| 功能 | 狀態 |
|------|------|
| 標準函式庫 (`std`) | 研究 / 設計 |
| UI 元件庫 (`neverc-ui`) | 研究 / 設計 |
| IDE 與語言工具 (`neverc-ide`) | 研究 / 設計 |
| EVM 智慧合約後端 | 研究 / 設計 |
| Solana eBPF 後端 | 研究 / 設計 |

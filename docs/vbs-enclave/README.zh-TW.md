**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 文件索引](../README.zh-TW.md) · [← NeverC 專案](../i18n/README.zh-TW.md)

# Windows 上的 VBS enclave DLL

NeverC 可以為 64 位元 Windows 目標連結與 Microsoft 相容的 VBS enclave DLL。支援的連結器契約如下：

```text
/DLL /INCREMENTAL:NO /INTEGRITYCHECK /ENCLAVE /GUARD:MIXED
```

透過 Windows 驅動程式的 `-Xmslink` 或 `-Wl,` 傳遞 Microsoft 連結器選項：

```powershell
neverc.exe --target=x86_64-pc-windows-msvc -fno-lto -shared -nostdlib `
  enclave.obj guarded.obj legacy.obj `
  -lvertdll -lbcrypt -llibcmt -llibvcruntime -lucrt `
  -Xmslink /INCREMENTAL:NO `
  -Xmslink /NODEFAULTLIB `
  -Xmslink /ENCLAVE `
  -Xmslink /INTEGRITYCHECK `
  -Xmslink /GUARD:MIXED `
  -Xmslink /DYNAMICBASE `
  -Xmslink /MACHINE:X64 `
  -o game-security-enclave.dll
```

本範例透過 `-l` 明確選取 MSVC CRT 與 UCRT 程式庫的 enclave 版本。任一明確指定的 `-vctoolsdir` 或 `-winsysroot` 仍依照一般優先順序生效。未指定這些覆寫項目時，不論在 macOS、Linux 或 Windows 上，所有 `/ENCLAVE` 連結都只會從 NeverC 隨附的目標執行階段解析 Windows 程式庫；驅動程式不會自動偵測或回退至主機上安裝的 Visual Studio 工具組或 Windows SDK。

## 使用隨附執行階段進行跨主機建置

編譯與 COFF 連結不受主機平台限制。安裝目標執行階段後，同一條命令可以在 macOS、Linux 或 Windows 上執行：

```text
neverc runtime install windows-x64
neverc runtime install windows-arm64
```

目標套件包含 Windows 標頭、enclave CRT、enclave UCRT、`vertdll.lib`、`bcrypt.lib` 與其他必要的 Windows 匯入程式庫。使用隨附執行階段解析時，只有明確 `/ENCLAVE` 與全域 `/NODEFAULTLIB` 同時出現，NeverC 才會從一般的隨附 CRT/UCRT 目錄切換到 enclave CRT/UCRT 目錄。在此模式下，驅動程式會在連結前驗證隨附的 `libcmt.lib`、`libvcruntime.lib`、`ucrt.lib`、`vertdll.lib` 與 `bcrypt.lib` 均存在。這些程式庫仍須使用 `-l...` 明確選取。單獨使用 `/ENCLAVE` 既不會啟用 enclave CRT/UCRT 目錄，也不會選取其中的程式庫；它會繼續使用隨附的一般執行階段搜尋路徑。

跨主機連結階段會產生未簽署、未經處理的 enclave DLL。VEIID 處理、SignTool 簽署，以及透過 `CreateEnclave`/`LoadEnclaveImage` 實際載入仍只能在 Windows 上進行，因此請將在 macOS 或 Linux 上連結的 DLL 移至 Windows 封裝機或測試機，以完成最後三個階段。有關執行階段的安裝與探索，請參閱[目標執行階段](../runtime/README.zh-TW.md)。

## 必要的映像輸入

enclave 連結必須提供下列兩個映像資料定義：

- `__enclave_config`，其中包含映像的 `IMAGE_ENCLAVE_CONFIG` 資料。
- `_load_config_used`，其 load-config 結構必須足夠大，能夠容納 `EnclaveConfigurationPointer`。

NeverC 會在移除未使用程式碼時保留 `__enclave_config`，必要時從封存庫中擷取它，並驗證最終重定位後的 load-config 指標等於該設定物件的虛擬位址。缺少、絕對、已捨棄、遭截斷或重定位錯誤的定義都會造成連結錯誤。

`/GUARD:MIXED` 會為受保護與舊式目的檔混合輸入啟用 CFG 輸出。它會產生 5 位元組的 GFID 和 GIAT 項目：4 位元組 RVA 加上 1 位元組中繼資料；目前一般目標的中繼資料為零。其 `GuardFlags` 包含 CFG 與項目大小位元。舊式目的檔會透過保守掃描重定位來提供位址被取得目標，並排除展開中繼資料。

明確的增量連結要求與 `/ENCLAVE` 不相容，因此會遭到拒絕。連結器採用最後一個生效的 `/INCREMENTAL` 選項，包括來自目的檔指令的選項。

`/ENCLAVE` 不會隱含選擇 DLL 輸出、CFG、完整性檢查、enclave CRT 程式庫、VEIID 處理或簽署。請在建置管線中明確指定這些選擇。在隨附執行階段模式下，只有明確指定全域 `/NODEFAULTLIB` 時，才會啟用上文所述的 enclave CRT/UCRT 搜尋路徑與五個程式庫的驗證；沒有該選項時，仍使用隨附的一般 Windows 執行階段路徑。明確指定的使用者工具鏈覆寫項目繼續保持一般優先順序。

## 建置與部署流程

1. 在啟用 CFG 的情況下編譯安全性敏感的原始碼，例如使用 `-fms-guard=cf`。當最終連結使用 `/GUARD:MIXED` 時，舊式目的檔可以維持未插樁狀態。
2. 定義 enclave 設定與進入點，然後連結 enclave CRT/UCRT 以及必要的 Vertdll 與 BCrypt 匯入程式庫。
3. 檢查未簽署的 PE 映像，並驗證其 load-config 目錄、CFG 資料表、enclave 設定指標與基底重定位。
4. 在 Windows 上對完成的映像執行 Windows SDK 的 VEIID 工具。
5. 在 Windows 上使用 SignTool 簽署經 VEIID 處理的映像。簽署必須是最後一次檔案變更。
6. 在 Windows 主機程式中檢查 `IsEnclaveTypeSupported(ENCLAVE_TYPE_VBS)`，使用 `CreateEnclave` 配置 enclave，透過 `LoadEnclaveImage` 載入 DLL，並呼叫 `InitializeEnclave`。

對於反作弊系統，enclave 適合承載小型驗證或金鑰處理元件；這類元件的程式碼與私有狀態需要與一般遊戲處理程序建立更強的邊界。請保持 enclave 介面精簡，並驗證主機提供的所有資料：主機仍然控制輸入、排程、儲存與可用性。VBS enclave 是伺服器端權威、遙測、驅動程式防禦和一般處理程序強化的補充，而非替代品。

## 驗證

`VBS enclave differential CI` 工作流程在 Windows 上執行。其靜態閘道會：

- 建置 NeverC 連結器與聚焦的 COFF 測試；
- 建立等價的 Microsoft 連結與 NeverC 連結 enclave DLL；
- 比較公開的 PE/load-config/CFG 語意；
- 對 PE 驗證器執行突變測試；以及
- 為差異執行階段探測準備經 VEIID 處理的映像。

執行階段探測會先執行 Microsoft 映像。如果託管 runner 缺少 VBS 或可用的簽署環境，結果會明確標示為環境略過。一旦 Microsoft 參考映像成功載入，任何一個 NeverC 候選映像失敗都會成為硬性測試失敗。設定妥當的自託管 VBS runner 可以將執行階段成功設為強制閘道。

連結器支援 x86-64 與 ARM64 COFF enclave 映像。它會驗證已發佈的設定指標，接著依最終的一般 DLL 匯入集合產生連續的 80 位元組 `IMAGE_ENCLAVE_IMPORT` 項目。項目初始只包含匯入名稱，識別欄位均為零，供 VEIID 繫結；連結器會回填數量、清單與項目大小。作用中的延遲載入匯入會被拒絕。連結器不會對 `IMAGE_ENCLAVE_CONFIG` 內部帶版本的欄位施加額外原則。

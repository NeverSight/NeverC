**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 範例](../../docs/examples/README.zh-TW.md)

# Windows 核心驅動程式範例

使用 NeverC 建置的最小 WDM 核心驅動程式。預設面向 **x64**，也可以建置 ARM64 版本。
支援從 macOS / Linux 交叉編譯。

NeverC 是一體化編譯器——單次呼叫即可完成預處理、編譯、最佳化（auto-LTO）
以及透過內建連結器進行連結。

## 建置

從儲存庫根目錄：

```bash
cd examples/windows-driver
neverc make
```

這會產生 `ExampleDriver-x64.sys`。若要改為建置 ARM64，或兩者都建置：

```bash
neverc make ARCH=arm64
neverc make all-arch
```

使用獨立的 NeverC 發行版：

```bash
neverc make NEVERC=/path/to/neverc
```

輸出為 `ExampleDriver-<架構>.sys`（auto-LTO 最佳化）。
預設建置包含 `-g` 用於除錯；**釋出版本應移除 `-g`** 以移除除錯符號並縮小二進位檔案大小
（~38 KB → ~3 KB）。

## 手動建置（不使用 Make）

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fms-kernel \
  -Wall -nostdlib -shared \
  -Xlinker --driver \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -lntoskrnl -lhal \
  -o ExampleDriver-x64.sys driver.c
```

建置 ARM64 時只需將 target 換成 `aarch64-pc-windows-msvc`，其餘不變。
`-fms-kernel` 會自動選用與目標架構相符的 WDK 標頭檔和匯入程式庫，並定義 WDK
所需的架構巨集，因此無需手動傳入。
`--driver` 將映像標記為核心模式：程式碼和資料區段設為非分頁，匯入表移入可丟棄的
INIT 區段，並寫入核心載入器會驗證的 PE 總和檢查碼。

> `-g` 將 DWARF 除錯資訊嵌入 PE；可使用 `llvm-dwarfdump` 檢查。
> 釋出版本應省略此選項以縮小二進位檔案大小。

## 測試簽章

Windows 拒絕載入未簽章的核心驅動程式。`-ftest-sign` 會附加 Authenticode 簽章，
使映像在測試機上通過該檢查：

```bash
neverc make TESTSIGN=1
neverc make ARCH=arm64 TESTSIGN=1
```

也可在手動呼叫時加上 `-ftest-sign`。此選項只能與 `-fms-kernel` 搭配使用，
因為測試簽章對使用者模式程式沒有意義。

簽章身分內建於編譯器中——一張自簽憑證，其私鑰按設計就是公開的。它不提供
任何真實性保證，只是讓映像能通過你主動放寬限制的機器上的程式碼完整性檢查。
該機器需以系統管理員身分一次性設定：

```cmd
bcdedit /set testsigning on
certutil -addstore Root neverc-test-signing.cer
certutil -addstore TrustedPublisher neverc-test-signing.cer
```

然後重新開機。憑證直接從編譯器匯出，這樣它與實際簽章所用的身分始終一致：

```bash
neverc --print-test-sign-cert > neverc-test-signing.cer
```

（原始碼樹中 `utils/neverc-test-signing.cer` 也有一份副本，但它不包含在發行套件裡。）

若手邊沒有 Windows 機器，可以用 `osslsigncode` 檢查簽章。注意 `-CAfile` 需要 PEM
格式，而憑證是 DER，必須先轉換——直接傳 DER 會報出令人困惑的
"signature verification failed"，其真實原因是 "no certificate found"：

```bash
openssl x509 -inform DER -in neverc-test-signing.cer -out nc.pem
osslsigncode verify -CAfile nc.pem ExampleDriver-x64.sys
```

**切勿將其用於任何離開測試機的產物。** 生產環境請使用真實的程式碼簽章憑證
（Windows 10 1607 及更高版本還需要 Microsoft 硬體開發人員中心的證明簽章）。

## 功能說明

- 在 `\Device\ExampleDriver` 建立裝置物件
- 在 `\DosDevices\ExampleDriver` 建立符號連結
- 處理 `IRP_MJ_CREATE`、`IRP_MJ_CLOSE`、`IRP_MJ_DEVICE_CONTROL`
- 透過 `DbgPrint` 輸出載入/卸載訊息

## 載入（在 Windows 測試機上）

```cmd
sc create ExampleDriver type= kernel binPath= C:\path\to\ExampleDriver-x64.sys
sc start ExampleDriver
sc stop ExampleDriver
sc delete ExampleDriver
```

請啟用測試簽章或使用程式碼簽章憑證用於生產環境。

# Windows 核心驅動程式範例

使用 NeverC 建置的最小 WDM 核心驅動程式。支援從 macOS / Linux 交叉編譯。

NeverC 是一體化編譯器——單次呼叫即可完成預處理、編譯、最佳化（auto-LTO）
以及透過內建連結器進行連結。

## 建置

從儲存庫根目錄：

```bash
cd examples/windows-driver
make
```

使用獨立的 NeverC 發行版：

```bash
make NEVERC=/path/to/neverc
```

輸出為 `ExampleDriver.sys`（auto-LTO 最佳化）。
預設建置包含 `-g` 用於除錯；**釋出版本應移除 `-g`** 以移除除錯符號並縮小二進位檔案大小
（~38 KB → ~3 KB）。

## 手動建置（不使用 Make）

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fms-kernel -fms-extensions -fms-compatibility \
  -D_AMD64_ -DNTDDI_VERSION=0x06010000 -D_WIN32_WINNT=0x0601 \
  -Wall -nostdlib -shared \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -lntoskrnl -lhal \
  -o ExampleDriver.sys driver.c
```

> `-g` 將 DWARF 除錯資訊嵌入 PE；可使用 `llvm-dwarfdump` 檢查或在 WinDbg 中
> 載入符號。釋出版本應省略此選項以縮小二進位檔案大小。

## 功能說明

- 在 `\Device\ExampleDriver` 建立裝置物件
- 在 `\DosDevices\ExampleDriver` 建立符號連結
- 處理 `IRP_MJ_CREATE`、`IRP_MJ_CLOSE`、`IRP_MJ_DEVICE_CONTROL`
- 透過 `DbgPrint` 輸出載入/卸載訊息

## 載入（在 Windows 測試機上）

```cmd
sc create ExampleDriver type= kernel binPath= C:\path\to\ExampleDriver.sys
sc start ExampleDriver
sc stop ExampleDriver
sc delete ExampleDriver
```

請啟用測試簽署或使用程式碼簽署憑證用於正式環境。

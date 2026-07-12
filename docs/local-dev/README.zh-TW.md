**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 文件索引](../README.zh-TW.md)

# 本地開發

從原始碼建置 NeverC 並設定本地開發環境的指南。

---

## 前置條件

- CMake 3.20+
- Ninja
- C++17 宿主編譯器（GCC、Clang 或 MSVC）

---

## 建置

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake
cmake --build build-neverc --target neverc
```

若偵測到 `ccache` / `sccache` 會自動啟用。

`--target neverc` 是日常的 stage-1 建置（嵌入式 runtime 為佔位空 blob），
多數本地編譯/偵錯已足夠。若需要編譯器二進位內嵌 string / mimalloc / std / NVK
runtime（或對齊 CI 產物），再執行 stage-2 傘目標：

```bash
cmake --build build-neverc --target neverc-embed-runtime-bitcode
```

兩階段 bootstrap 細節見 [Builtins](../builtins/README.zh-TW.md)。

### 包含測試建置

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake -DNEVERC_INCLUDE_TESTS=ON
cmake --build build-neverc --target check-neverc
```

`check-neverc` 依賴 `neverc-embed-runtime-bitcode`，首次執行測試前會自動
bootstrap 並重新連結編譯器，無需手動執行 embed 目標。

---

## 設定 PATH（macOS / Linux）

建置完成後，`neverc` 執行檔位於 `build-neverc/bin/neverc`。使用輔助腳本將其加入 `PATH`，無需每次輸入完整路徑：

```bash
source ./tools/neverc-env.sh
```

之後即可直接執行 `neverc`：

```bash
neverc --version
neverc -c hello.c -o hello.o
```

### 從 PATH 移除

當不再需要本地建置在 `PATH` 中時，在同一 shell 工作階段中移除：

```bash
source ./tools/neverc-env.sh --remove   # 或 -r
```

### 持久化設定

自動將 `source` 行寫入 shell 設定檔（`~/.zshrc`、`~/.bashrc` 或 `~/.profile`）：

```bash
source ./tools/neverc-env.sh --install
```

撤銷：

```bash
source ./tools/neverc-env.sh --uninstall
```

---

## Windows (CMD)

在 Windows 上使用 `.bat` 腳本（無需管理員權限）：

```cmd
tools\neverc-env.bat             &REM 加入 PATH（當前工作階段）
tools\neverc-env.bat --remove    &REM 從 PATH 移除（當前工作階段）
tools\neverc-env.bat --global    &REM 透過 setx 持久化到使用者 PATH
tools\neverc-env.bat --global -r &REM 透過 setx 從使用者 PATH 移除
```

與 Unix 腳本不同，無需 `source` — `.bat` 直接修改當前 `cmd` 工作階段。`--global` 透過 `setx` 寫入使用者級登錄檔（無需管理員權限）。

---

## macOS 預編譯產物

發佈產物已使用 Apple Developer ID 憑證簽署並經 Apple 公證。解壓後可直接使用，無需任何額外操作。

---

## 交叉編譯到 Windows

NeverC 在 `runtime/` 中內建了各平台 SDK（Windows SDK/WDK、Linux sysroot、macOS sysroot、Android NDK），無需額外設定。

```bash
neverc --target=x86_64-pc-windows-msvc \
  -fbuiltin-string -o hello.exe hello.c -lkernel32
```

Windows dyncode（`-fdyncode`、PEB 匯入解析等）詳見 [dyncode 編譯器文件](../dyncode-compiler/README.zh-TW.md)。

---

## 驗證

```bash
neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
neverc -c /tmp/hello.c -o /tmp/hello.o
```

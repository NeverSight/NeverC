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

### 包含測試建置

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake -DNEVERC_INCLUDE_TESTS=ON
cmake --build build-neverc --target check-neverc
```

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

## 驗證

```bash
neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
neverc -c /tmp/hello.c -o /tmp/hello.o
```

**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 文档索引](../README.zh-CN.md)

# 本地开发

从源码构建 NeverC 并配置本地开发环境的指南。

---

## 前置条件

- CMake 3.20+
- Ninja
- C++17 宿主编译器（GCC、Clang 或 MSVC）

---

## 构建

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake
cmake --build build-neverc --target neverc
```

如果检测到 `ccache` / `sccache` 会自动启用。

`--target neverc` 是日常的 stage-1 构建（嵌入式 runtime 为占位空 blob），
多数本地编译/调试够用。若需要编译器二进制内嵌 string / mimalloc / std / NVK
runtime（或对齐 CI 制品），再跑 stage-2 伞目标：

```bash
cmake --build build-neverc --target neverc-embed-runtime-bitcode
```

两阶段 bootstrap 细节见 [Builtins](../builtins/README.zh-CN.md)。

### 带测试构建

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake -DNEVERC_INCLUDE_TESTS=ON
cmake --build build-neverc --target check-neverc
```

`check-neverc` 依赖 `neverc-embed-runtime-bitcode`，首次跑测试前会自动
bootstrap 并重链编译器，无需手写 embed 目标。

---

## 设置 PATH（macOS / Linux）

构建完成后，`neverc` 二进制文件位于 `build-neverc/bin/neverc`。使用辅助脚本将其加入 `PATH`，无需每次输入完整路径：

```bash
source ./utils/build/neverc-env.sh
```

之后就可以直接运行 `neverc`：

```bash
neverc --version
neverc -c hello.c -o hello.o
```

### 从 PATH 移除

当不再需要本地构建在 `PATH` 中时，在同一 shell 会话中移除：

```bash
source ./utils/build/neverc-env.sh --remove   # 或 -r
```

### 持久化设置

自动将 `source` 行写入 shell 配置文件（`~/.zshrc`、`~/.bashrc` 或 `~/.profile`）：

```bash
source ./utils/build/neverc-env.sh --install
```

撤销：

```bash
source ./utils/build/neverc-env.sh --uninstall
```

### 切换本地开发版 / 线上 release 版

如果本机同时装了 release（默认在 `~/.neverc`）又在源码树里 build 过，用 `neverc-env.sh` 在当前 shell 里切换即可，不会互相覆盖文件：

```bash
source ./utils/build/neverc-env.sh              # 本地开发版（build-neverc/bin）
source ./utils/build/neverc-env.sh --local      # 同上
source ./utils/build/neverc-env.sh --release    # 线上 release 版（~/.neverc/bin）
source ./utils/build/neverc-env.sh --status     # 查看当前用的是哪个 neverc
source ./utils/build/neverc-env.sh --remove     # 从 PATH 移除两个版本
```

切换后会设置 `NEVERC_ENV`（`local` 或 `release`），方便脚本判断当前模式：

```bash
echo "$NEVERC_ENV"
neverc --version
which neverc
```

如果 release 装在其他目录，切换前指定与 `install.sh` 相同的 prefix：

```bash
NEVERC_INSTALL_DIR=$HOME/.neverc-v3389.1.2 source ./utils/build/neverc-env.sh --release
```

可选：在 shell 配置里加别名，任意目录一行切换（把路径换成你的仓库绝对路径）：

```bash
alias neverc-dev='source /path/to/NeverC/utils/build/neverc-env.sh --local'
alias neverc-rel='source /path/to/NeverC/utils/build/neverc-env.sh --release'
```

---

## Windows (CMD)

在 Windows 上使用 `.bat` 脚本（无需管理员权限）：

```cmd
utils\build\neverc-env.bat             &REM 加入 PATH（当前会话）
utils\build\neverc-env.bat --remove    &REM 从 PATH 移除（当前会话）
utils\build\neverc-env.bat --global    &REM 通过 setx 持久化到用户 PATH
utils\build\neverc-env.bat --global -r &REM 通过 setx 从用户 PATH 移除
```

与 Unix 脚本不同，无需 `source` — `.bat` 直接修改当前 `cmd` 会话。`--global` 通过 `setx` 写入用户级注册表（无需管理员权限）。

---

## macOS 预编译产物

发布产物已使用 Apple Developer ID 证书签名并经过 Apple 公证。解压后可直接使用，无需任何额外操作。

---

## 交叉编译到 Windows

NeverC 在 `runtime/` 中内置了各平台 SDK（Windows SDK/WDK、Linux sysroot、macOS sysroot、Android NDK），无需额外配置。

```bash
neverc --target=x86_64-pc-windows-msvc \
  -fbuiltin-string -o hello.exe hello.c -lkernel32
```

Windows dyncode（`-fdyncode`、PEB 导入解析等）详见 [dyncode 编译器文档](../dyncode-compiler/README.zh-CN.md)。

---

## 验证

```bash
neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
neverc -c /tmp/hello.c -o /tmp/hello.o
```

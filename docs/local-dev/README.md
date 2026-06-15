**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Documentation index](../README.md)

# Local Development

Guide for building NeverC from source and setting up a local development environment.

---

## Prerequisites

- CMake 3.20+
- Ninja
- A C++17 host compiler (GCC, Clang, or MSVC)

---

## Building

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake
cmake --build build-neverc --target neverc
```

`ccache` / `sccache` is auto-detected and enabled if present.

### Building with Tests

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake -DNEVERC_INCLUDE_TESTS=ON
cmake --build build-neverc --target check-neverc
```

---

## Setting Up PATH (macOS / Linux)

After building, the `neverc` binary lives at `build-neverc/bin/neverc`. Instead of typing the full path every time, use the helper script to add it to your shell's `PATH`:

```bash
source ./tools/neverc-env.sh
```

Now you can run `neverc` directly:

```bash
neverc --version
neverc -c hello.c -o hello.o
```

### Removing from PATH

When you no longer need the local build on `PATH`, remove it in the same shell session:

```bash
source ./tools/neverc-env.sh --remove   # or -r
```

### Persistent Setup

Automatically write the `source` line to your shell rc file (`~/.zshrc`, `~/.bashrc`, or `~/.profile`):

```bash
source ./tools/neverc-env.sh --install
```

To undo:

```bash
source ./tools/neverc-env.sh --uninstall
```

---

## Windows (CMD)

On Windows, use the `.bat` script instead (no admin required):

```cmd
tools\neverc-env.bat             &REM add to PATH (current session)
tools\neverc-env.bat --remove    &REM remove from PATH (current session)
tools\neverc-env.bat --global    &REM persist to user PATH via setx
tools\neverc-env.bat --global -r &REM remove from user PATH via setx
```

Unlike the Unix script, no `source` is needed — the `.bat` modifies the current `cmd` session directly. The `--global` flag writes to the user-level registry via `setx` (no admin privileges needed).

---

## Prebuilt macOS Binaries

The release is signed with an Apple Developer ID certificate and notarized by Apple. Extract the archive and use directly — no quarantine workaround needed.

---

## Cross-Compiling to Windows

NeverC bundles platform SDKs in `runtime/` (Windows SDK/WDK, Linux sysroot, macOS sysroot, Android NDK); no external SDK setup is needed.

```bash
neverc --target=x86_64-pc-windows-msvc \
  -fbuiltin-string -o hello.exe hello.c -lkernel32
```

See [shellcode compiler docs](../shellcode-compiler/README.md) for Windows shellcode (`-fshellcode`, PEB import resolution, etc.).

---

## Verify

```bash
neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
neverc -c /tmp/hello.c -o /tmp/hello.o
```

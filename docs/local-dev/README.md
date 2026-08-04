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

`--target neverc` is the daily stage-1 build (empty embedded-runtime
placeholders). That is enough for most local compile/debug work. When you need
embedded string / mimalloc / std / NVK runtimes in the binary itself (or want a
CI-like compiler), run the stage-2 umbrella:

```bash
cmake --build build-neverc --target neverc-embed-runtime-bitcode
```

Details of the two-stage bootstrap live in [Builtins](../builtins/README.md).

### Building with Tests

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake -DNEVERC_INCLUDE_TESTS=ON
cmake --build build-neverc --target check-neverc
```

`check-neverc` depends on `neverc-embed-runtime-bitcode`, so the first test run
bootstraps and relinks the compiler before CTest. You do not need to run the
embed target by hand.

---

## Setting Up PATH (macOS / Linux)

After building, the `neverc` binary lives at `build-neverc/bin/neverc`. Instead of typing the full path every time, use the helper script to add it to your shell's `PATH`:

```bash
source ./utils/build/neverc-env.sh
```

Now you can run `neverc` directly:

```bash
neverc --version
neverc -c hello.c -o hello.o
```

### Removing from PATH

When you no longer need the local build on `PATH`, remove it in the same shell session:

```bash
source ./utils/build/neverc-env.sh --remove   # or -r
```

### Persistent Setup

Automatically write the `source` line to your shell rc file (`~/.zshrc`, `~/.bashrc`, or `~/.profile`):

```bash
source ./utils/build/neverc-env.sh --install
```

To undo:

```bash
source ./utils/build/neverc-env.sh --uninstall
```

### Switching Local Dev vs Release

If you have both a release install (default: `~/.neverc`) and an in-tree build, use
`neverc-env.sh` to switch the active `neverc` in the current shell without
overwriting either installation:

```bash
source ./utils/build/neverc-env.sh              # local dev (build-neverc/bin)
source ./utils/build/neverc-env.sh --local      # same as above
source ./utils/build/neverc-env.sh --release    # release install (~/.neverc/bin)
source ./utils/build/neverc-env.sh --status     # show which neverc is active
source ./utils/build/neverc-env.sh --remove     # remove both from PATH
```

Switching sets `NEVERC_ENV` to `local` or `release`:

```bash
echo "$NEVERC_ENV"
neverc --version
which neverc
```

If the release was installed to a custom prefix, pass the same directory as
`install.sh` uses:

```bash
NEVERC_INSTALL_DIR=$HOME/.neverc-v3389.1.2 source ./utils/build/neverc-env.sh --release
```

Optional shell aliases (replace with your repository path):

```bash
alias neverc-dev='source /path/to/NeverC/utils/build/neverc-env.sh --local'
alias neverc-rel='source /path/to/NeverC/utils/build/neverc-env.sh --release'
```

---

## Windows (CMD)

On Windows, use the `.bat` script instead (no admin required):

```cmd
utils\build\neverc-env.bat             &REM add to PATH (current session)
utils\build\neverc-env.bat --remove    &REM remove from PATH (current session)
utils\build\neverc-env.bat --global    &REM persist to user PATH via setx
utils\build\neverc-env.bat --global -r &REM remove from user PATH via setx
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

See [dyncode compiler docs](../dyncode-compiler/README.md) for Windows dyncode (`-fdyncode`, PEB import resolution, etc.).

---

## Verify

```bash
neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
neverc -c /tmp/hello.c -o /tmp/hello.o
```

**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Documentation](../README.md)

# The `.nc` File Extension

## Overview

NeverC recognizes `.nc` as its native source file extension. When the compiler detects a `.nc` input file, it **automatically enables** all NeverC language extensions — no extra flags required.

## What Gets Enabled

| Flag | Effect |
|------|--------|
| `-fneverc-types` | Rust-style integer aliases (`u8`, `u16`, `u32`, `u64`, `i8`, `i16`, `i32`, `i64`, `usize`, `isize`) |
| `-fbuiltin-string` | Built-in `string` value type with automatic memory management, dot-call syntax, and UTF-8 support |

## Usage

Simply name your source file with a `.nc` extension:

```bash
# Automatic — no -fbuiltin-string or -fneverc-types needed
neverc hello.nc -o hello

# Equivalent to:
neverc -fneverc-types -fbuiltin-string hello.c -o hello
```

```c
// hello.nc
#include <stdio.h>

int main(void) {
    string greeting = "Hello, NeverC!";
    printf("%s (len=%zu)\n", greeting.c_str(), greeting.len);

    u32 x = 42;
    i64 y = -100;

    string msg = greeting + " x=" + neverc_string_format("%u", x);
    printf("%s\n", msg.c_str());
    return 0;
}
```

## How It Works

The detection operates at two levels of the compiler pipeline:

### 1. Driver / Toolchain Layer

The driver inspects each input file's extension before constructing the compiler invocation. For `.nc` files, `-fneverc-types` and `-fbuiltin-string` are unconditionally injected into the command line — the user does not need to pass them manually.

For `.c` files, these flags remain opt-in: the user must explicitly pass `-fneverc-types` and/or `-fbuiltin-string`.

### 2. CompilerInvocation Layer

As a safety net, the frontend also checks input file extensions when parsing the invocation. If any input has a `.nc` extension, `LangOpts.NeverCTypes` and `LangOpts.BuiltinString` are set to `1`, ensuring the features are active even if the driver layer is bypassed (e.g., when invoking `-cc1` directly).

## Compatibility

- `.nc` files are treated as C source — the language is still C (C23 by default), not a new language
- All standard C flags (`-std=c11`, `-O2`, `-g`, `-Wall`, etc.) work identically
- `-fshellcode` combines naturally with `.nc`: shellcode mode already enables `string` on its own, and `.nc` ensures `neverc-types` is also active
- Cross-compilation (`-target aarch64-linux-gnu`, etc.) works the same way
- `.c` files are unaffected — they behave exactly as before unless you pass the flags explicitly

## When to Use `.nc` vs `.c`

| Scenario | Recommendation |
|----------|---------------|
| New NeverC project using `string` and Rust-style types | Use `.nc` |
| Existing C codebase you want to keep compatible with other compilers | Use `.c` + explicit flags |
| Shellcode project | Either works — `-fshellcode` enables `string` regardless |
| Mixed codebase | Use `.nc` for NeverC-specific files, `.c` for portable code |

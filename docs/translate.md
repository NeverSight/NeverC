**Languages**: [English](translate.md) | [简体中文](zh-CN/translate.md) | [繁體中文](zh-TW/translate.md) | [日本語](ja/translate.md) | [한국어](ko/translate.md) | [Français](fr/translate.md) | [Deutsch](de/translate.md) | [Español](es/translate.md) | [Italiano](it/translate.md) | [Русский](ru/translate.md) | [العربية](ar/translate.md)

[← Documentation](README.md)

# Translate C++ to NeverC

Experimental `neverc translate` emits reviewable `.nc` source through `cpp-core-v1`, `cpp-core-v2`, `cpp-project-v1` and `cpp-math-v1`.

**Only C++ input translation is currently implemented.** Support for E Language (易语言, `.e`), Python, Go, Rust, TypeScript and JavaScript is planned; their translators are not yet available.

## Setup and scalar translation

Use a normal NeverC installation with its standard resources. The C++ frontend and approved SDK headers are built into NeverC; no separate Clang installation is needed. See the [frontend build notes](../utils/translate-frontends/cpp/README.md).

```sh
neverc translate --from cpp input.cpp -o output.nc
neverc output.nc -c -o output.o
```

The `cpp-core-v1` profile accepts one self-contained C++17 source without includes. It supports `int`, `unsigned int`, `bool`, `void`, trivial aggregate types, free functions, namespaces, overloads and the documented control flow. Every declaration in the input is checked, including unused code.

Select `--profile cpp-core-v2` to add checked `typedef`/`using` aliases, enums with 32-bit `int` or `unsigned int` underlying types, `static_assert`, bounded object pointers and lvalue references. References preserve aliases, including parameters and returned references; nested pointee `const` and null pointers are supported. The single-source and no-include restrictions still apply. See the [core v2 contract](../utils/translate-frontends/docs/cpp-core-v2.md).

Core v2 also adds fixed-size local arrays and array fields, multidimensional indexing, and pointers/references to arrays. Partial initialization fills remaining elements with zero; element initialization preserves source order and aliasing. Extents and initializer expansion are bounded. Global arrays, variable-length arrays and nontrivial element lifetimes remain unsupported.

## Multi-file projects

Select the translation units explicitly from a compilation database and set the project root directory. The built-in frontend analyzes each unit separately; the merger checks definitions, linkage and shared types, and conservatively verifies the one-definition rule (ODR).

```sh
neverc translate --from cpp --profile cpp-project-v1 \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/a.cpp src/b.cpp --out-dir generated
```

Project output is `translated.nc` plus `translated.h`. Only project headers within that root are admitted. If one file has several configurations, select its zero-based database index with `--compdb-entry src/a.cpp=4`. Stored compiler commands are parsed as data and are never executed.

## Bounded double math

`cpp-math-v1` extends projects with `double`, documented conversions/comparisons, and exactly `std::fabs(double)` and `std::floor(double)`. It uses the built-in Clang 20.1.8 / libc++ 200100 / macOS 15.5 header set and requires an explicit macOS 15.0 target (arm64 or x86_64). General floating arithmetic remains unsupported. The floating environment requires masked traps and no flush-to-zero mode; all four standard rounding modes are tested.

```sh
neverc translate --from cpp --profile cpp-math-v1 \
  --target arm64-apple-macosx15.0.0 \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/math.cpp --out-dir generated-math
```

Math translation also verifies installed NeverC math headers, embedded implementation identities and an actual link probe. Generated math modules use the NeverC runtime. With `-fno-builtin-std`, translation fails before writing output only if the final code needs `fabs`/`floor` mappings. Math code requiring neither can still be translated.

## Validation and output

Use `--check` instead of an output option to run the same analysis, emission, syntax and object validation without retaining generated files. `--report PATH` writes structured diagnostics. Translation never runs the source program.

Outputs and sidecars are never overwritten. `--out-dir` requires a new directory with an existing parent; `-o` requires a new `.nc` path. The manifest records target requirements, input/output hashes and the compilation recipe; the source map relates generated lines to original locations.

CI results, execution and installation checks, and skipped tests are recorded by platform. Native macOS arm64 and macOS x86_64 under Rosetta remain distinct validation environments. General C++/STL support, exceptions, templates, strings and `std::vector` are outside the advertised contract. See the [support matrix](../utils/translate-frontends/docs/support-matrix.md), [protocol and recovery rules](../utils/translate-frontends/docs/protocol.md), and [project fixture](../tests/neverc/Inputs/translate/cpp/project).

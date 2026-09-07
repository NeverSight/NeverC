**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Documentation](../README.md)

# Translate C++ to NeverC

Experimental `neverc translate` emits reviewable `.nc` source through `cpp-core-v1`, `cpp-project-v1` and `cpp-math-v1`.

**Only C++ input translation is currently implemented.** Support for E Language (易语言, `.e`), Python, Go, Rust, TypeScript and JavaScript is planned; their translators are not yet available.

## Setup and scalar translation

Use a normal NeverC installation with its standard resources. The C++ frontend and approved SDK headers are built into NeverC; no separate Clang installation is needed. See the [frontend build notes](../../utils/translate-frontends/cpp/README.md).

```sh
neverc translate --from cpp input.cpp -o output.nc
neverc output.nc -c -o output.o
```

The `cpp-core-v1` profile accepts one self-contained C++17 source without includes. It supports `int`, `unsigned int`, `bool`, `void`, trivial aggregate types, free functions, namespaces, overloads and the documented control flow. Every declaration in the input is checked, including unused code.

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

Execution and installation evidence is recorded separately for native macOS arm64 and macOS x86_64 under Rosetta. This does not establish support for native Intel Macs, Linux, Windows or other targets. General C++/STL support, pointers, references, arrays, exceptions, templates, strings and `std::vector` are outside the advertised contract. See the [support matrix](../../utils/translate-frontends/docs/support-matrix.md), [protocol and recovery rules](../../utils/translate-frontends/docs/protocol.md), and [project fixture](../../tests/neverc/Inputs/translate/cpp/project/).

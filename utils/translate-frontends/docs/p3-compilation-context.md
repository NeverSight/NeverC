# C++ compilation-context contract

Implemented by [CompilationContext.h](../../../neverc/lib/Translate/Cpp/CompilationContext.h)
and [CompilationContext.cpp](../../../neverc/lib/Translate/Cpp/CompilationContext.cpp)
for `cpp-project-v1` and `cpp-math-v1`. The core profile remains a single,
include-free source. The [project merger](p3-project-emission.md) separately
checks declaration closure, linkage and ODR evidence after source analysis.

## Selection and ownership

```sh
neverc translate --from cpp --profile cpp-project-v1 \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/a.cpp src/b.cpp --out-dir generated
```

The project root and compilation database are required, and translation units
must be selected explicitly. The database does not select all its entries or
describe a complete link graph. Project output uses `--out-dir` or `--check`.
An omitted target selects the normalized NeverC native target; an explicit target
must agree with the source options and pass the profile's target checks. The
math profile additionally requires an explicit approved macOS 15.0 target.

A source with more than one matching database entry requires a zero-based index:

```sh
--compdb-entry src/a.cpp=4 --compdb-entry src/b.cpp=7
```

Diagnostics list candidate indexes, compilation directories and optional output
names. The selector's last `=` separates the index, allowing `=` in filenames.
Duplicate selections, wrong-source indexes and selectors for unselected files
fail. Even byte-identical duplicate entries require an explicit selection.

Entry directories must be absolute. Resolve source, include and response-file
paths against the original compilation directory, preserving symlink semantics
before reducing `..`. The database, selected source, compilation directory,
response files and owned include directories must resolve within the explicit
project root. An encompassing workspace root can include an out-of-tree build.
Approved SDK roots have separate distribution-relative identities and never
become owned project code. Marking an include path `-isystem` does not exempt
its declarations from source-subset checks.

## Arguments and response files

The parser follows the [Clang compilation-database format](https://releases.llvm.org/20.1.0/tools/clang/docs/JSONCompilationDatabase.html).
A structured `arguments` array takes precedence over `command`. The declared
compiler executable is provenance only: it is classified, never executed. The
pinned helper receives the validated arguments instead. Shell expansion,
environment assignments, compiler wrappers and unknown driver forms are rejected.

`--compdb-quoting gnu|windows` chooses command/response tokenization. Its default
follows the host, never the requested target. Nested `@response` paths resolve
against the original compilation directory, not the containing response file.
Cycles, missing inputs, invalid text and resource-limit violations fail before
starting source analysis. Extra arguments after `--` are appended to each
selected context and resolved against that entry's compilation directory.

| Input | Bound |
| --- | --- |
| Database | 32 MiB, 10,000 entries, JSON nesting depth 32 |
| Response nesting | 16 levels |
| Response bytes per selected unit | 4 MiB cumulative |
| Expanded arguments per selected unit | 65,536 arguments and 4 MiB |

Preserve macro and include order. The accepted options are deliberately narrow:

| Options | Handling |
| --- | --- |
| `-std=c++17`, `-x c++` | Normalize the supported language; reject other standards/languages. |
| `-D`, `-U` | Preserve values and order; the helper rejects unsupported macro observations. |
| `-I`, `-iquote`, `-isystem` | Preserve category/order and normalize owned paths. Reject sysroot-relative forms. |
| `-target`, `--target`, `-arch`, `-m32`, `-m64` | Require agreement with the authoritative target before consuming redundant flags. |
| `-O0`, `-O2` | Preserve source optimization context, including preprocessing observations. |
| `-Wall`, `-Wextra`, `-Wpedantic`, `-Werror`, `-Wno-error`, `-Wno-unused-parameter` | Forward the explicitly supported diagnostics policy. |
| `-c`, `-o`, `-MD`, `-MMD`, `-MF`, `-MT`, `-MQ`, `-MP` | Consume recognized operands without producing the original build outputs. Require exactly the selected source operand. |
| SDK/resource overrides, fast math, packing, plugins, PCH/modules, linker/assembler forwarding, other flags | Reject; these are not silently dropped. |

The helper independently checks the resulting option envelope. The driver-owned
[execution environment policy](protocol.md#compiler-execution-environment)
excludes ambient include paths, deployment overrides and compiler defaults.

## Identity and revalidation

`ProjectContext` owns validated per-unit jobs. `parseProjectContext` returns no
jobs on failure and collects independent selection errors where possible. It
does not emit source, run stored commands, change process cwd, discover a linker
graph or decide which C++ declarations are supported.

Each `ConfigurationID` hashes a length-framed domain version, normalized source
identity, normalized compilation directory, target and normalized argument
sequence. It excludes the database array index and generated output directory.
Database and response inputs retain separate hashes of their exact bytes; SDK
identity and consumed headers are independently validated by the driver/helper.

Recheck database, response, source and consumed dependency bytes before artifact
publication. Normalized context and semantic IDs survive relocation. Regenerating
a database with different absolute-path bytes changes its raw input hash even
when the normalized context is equivalent; the manifest records that change.

## Diagnostics and validation

`TR0601` covers invalid database/ownership context, `TR0602` missing or ambiguous
selection, and `TR0603` response expansion. Existing `TR0004`, `TR0203` and
`TR0204` retain source-option, dependency and target-conflict meanings.

[Context tests](../../../tests/neverc/TranslateCompilationContextTests.cpp)
cover structured and quoted commands, ambiguous selection, targets, response
nesting/cycles/limits, symlinks, relocation, input mutation and nonexecution of
stored commands. Integrated project tests also compile, link and execute the
merged output, verify C ABI clients and ODR rejection, and exercise installation
without the source frontend. See the [support matrix](support-matrix.md) for
verified host/target/runtime combinations.

**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Plugin ABI](../README.md)

# Custom Calling Conventions

NeverC supports **data-driven custom calling conventions** — you can assign arbitrary physical registers to any function's arguments and return values, entirely from an out-of-tree plugin or from source-level attributes, without modifying the compiler or any TableGen definition.

## Overview

Traditional LLVM calling conventions are baked into the backend via `.td` / `.inc` files. Adding or modifying one requires editing compiler sources and re-running TableGen. NeverC replaces this with a **runtime data-driven** model built from two layers:

- A **spec** — a short, human-writable string such as `gpr:rcx,rdx;ret:rax` — is attached to a function as the `"neverc-callconv"` string attribute, either by a plugin or by a source-level attribute.
- Before code generation the host **materializes** that spec into a `"neverc-cc-plan-v1"` attribute: an immutable, validated table of exact locations bound to a specific target schema. The backend consumes only the plan.

The spec is what you write; the plan is what the backend trusts. Calling conventions therefore move from "compile-time hardcoded in the backend" to "runtime data-driven by external policy", without giving up verification.

## Spec Format

A spec is a semicolon-delimited string. Each segment has a key and a comma-separated list of register names (case-insensitive, whitespace-tolerant):

```
gpr:rcx,rdx,r8,r9; xmm:xmm0,xmm1; ret:rax; ret_xmm:xmm0
```

| Segment | Aliases | Meaning |
|---|---|---|
| `args` | | **Positional mode**: each token is a register name or `stack`/`mem`, assigned to arguments by index |
| `gpr` | `arg_gpr` | **Pool mode**: integer/pointer argument registers, used in order; overflow spills to the stack |
| `xmm` | `arg_xmm` | **Pool mode**: float/vector argument registers |
| `fpr` | | Target-neutral alias for `xmm` |
| `ret_gpr` | `ret` | Integer/pointer return registers |
| `ret_xmm` | | Float/vector return registers |
| `ret_fpr` | | Target-neutral alias for `ret_xmm` |
| `csr` | | Custom callee-saved register set (default: the standard ABI set) |

Any segment may be omitted, and unknown segments are ignored. The keys are defined once in [`llvm/include/llvm/CodeGen/NeverCCallConv.h`], so producers and the parser cannot drift apart.

### Two Argument Modes

**Pool mode** (`gpr:` / `xmm:`): integer arguments take registers from the `gpr` pool in order; float and vector arguments take from `xmm`. When a pool is exhausted, the remaining arguments spill to the stack.

**Positional mode** (`args:`): argument *i* uses the *i*-th token. Each token is either a register name or `stack` / `mem`, which forces that argument onto the stack:

```
args:rcx,stack,r8;ret:rax   # arg0→rcx, arg1→stack, arg2→r8, return→rax
```

When `args` is present it takes precedence over `gpr` / `xmm`. A token that names the wrong register class for the argument's type, an index beyond the token list, and an already-allocated register all fall back to a stack slot rather than failing the build.

### Supported Architectures

Register names are resolved through a per-target table, the single source of truth for what a spec may name.

| Architecture | GPR names | SIMD names | Width selection |
|---|---|---|---|
| **x86-64** | `rax`, `rbx`, `rcx`, `rdx`, `rsi`, `rdi`, `rbp`, `r8`–`r15` | `xmm0`–`xmm15` | i32 → 32-bit sub-register, i64/pointer → 64-bit |
| **AArch64** | `x0`–`x28` | `v0`–`v31` | i32→`w`, i64→`x`, f16→`h`, f32→`s`, f64→`d`, f128/vector→`q` |

GPRs are always written in their 64-bit spelling; the backend narrows them to the sub-register that matches each value's type. Vector names are written as `v0`–`v31` on AArch64 and the backend picks the `H`/`S`/`D`/`Q` form by type.

### Constraints

- **Reserved registers**: the stack pointer is absent from both tables (`rsp` on x86-64, `sp`/`x31` on AArch64), as are AArch64 `x29`/`x30` (FP/LR). A spec naming one of them simply skips it and the value goes to the next valid location.
- **Frame pointer**: `rbp` *is* selectable on x86-64 because it is a legitimate callee-saved register, but using it as an argument register is only sound under `-fomit-frame-pointer`. Use at your own risk.
- **Callee-saved**: defaults to the standard ABI set. `csr:r12,r13` declares a custom set, and the caller builds a matching preserved-register mask so it knows which registers survive the call. Supported on both x86-64 and AArch64.
- **csr conflicts**: if a register appears in both `csr` and an argument/return list, the plugin emits a warning — the callee would restore it and destroy its value-passing role. Compilation still succeeds.
- **Variadic functions**: not supported. The compiler emits a clear diagnostic on both backends instead of silently mis-passing the variadic part.
- **Indirect calls**: a function-pointer call cannot carry a custom convention. The plugin warns when a custom-CC function has its address taken; indirect calls fall back to the standard convention.
- **Tail calls**: disabled whenever either side of a call uses the custom convention, on both backends.
- **Unmatched values**: any argument or return value the plan does not cover falls back to the target's standard convention (SysV on x86-64, AAPCS on AArch64).

## Usage

### 1. Plugin-Driven (Recommended)

The reference plugin [`CustomCallConvPlugin.c`] ships under `pluginsdk/examples/`. It registers a module-level IR pass at the `neverc.ir.pass.post_opt` phase.

**Build the plugin:**

```bash
cd pluginsdk/examples && make CustomCallConvPlugin.dylib   # or .so / .dll
```

**Attribute mode** (default) — only functions carrying a `custom_attr` source annotation are affected:

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib input.c -o output.o
```

**Global mode** — apply one spec to every defined function (requires an explicit `cc-all`):

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib \
       -fplugin-arg=org.neverc.example.custom-callconv:cc-all \
       -fplugin-arg=org.neverc.example.custom-callconv:ccspec="gpr:r10,r11,rsi;ret:rdx" \
       input.c -o output.o
```

**Filter by name prefix:**

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib \
       -fplugin-arg=org.neverc.example.custom-callconv:cc-all \
       -fplugin-arg=org.neverc.example.custom-callconv:ccprefix=secret_ \
       -fplugin-arg=org.neverc.example.custom-callconv:ccspec="gpr:r9,r8;ret:rax" \
       input.c -o output.o
```

**Diversify** — cycle through four built-in layouts so functions do not share one (anti-reverse-engineering):

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib \
       -fplugin-arg=org.neverc.example.custom-callconv:cc-all \
       -fplugin-arg=org.neverc.example.custom-callconv:ccshuffle \
       input.c -o output.o
```

The four options the plugin registers are `cc-all` and `ccshuffle` (flags, so `=1` or `=true` is optional) plus `ccspec` and `ccprefix` (string values). Without `ccspec`, global mode uses the default `gpr:r10,r11,rsi,rdi;ret:rdx`.

### 2. Source-Level Attributes

Annotate functions directly in C using the `custom_attr` attribute, in GNU or Microsoft syntax:

```c
// GNU syntax
__attribute__((custom_attr("neverc-callconv", "gpr:r10,r11,rsi;ret:rdx")))
int add3(int a, int b, int c) { return a + b + c; }

// Microsoft syntax
__declspec(custom_attr("neverc-callconv", "gpr:r10;ret:rdx"))
int msfunc(int a) { return a; }
```

`custom_attr("key", "value")` produces a clean function string attribute (`"key"="value"`) with **no** warnings and **no** `llvm.global.annotations`. It is a **general-purpose** mechanism — any key/value pair works, not just calling conventions. IR and MIR passes read it back with `F.getFnAttribute("key")`.

### 3. Combined

Source attributes and plugin arguments work together. A function carrying a `custom_attr` is handled by the plugin's attribute-mode path; `cc-all` covers the rest. Each function is processed at most once.

## Materialized Plans

A spec names registers; it does not say where each byte of each value lives. After the optimization pipeline and before code generation, the host runs `materializeCallingConventionPlans`, which turns every `CallingConv::NeverC_Custom` function into an exact, validated plan:

- A function that already has a `"neverc-cc-plan-v1"` attribute is **validated, not regenerated** — its schema digest, target ID, and convention ID must match the current target.
- A function with a `"neverc-callconv"` spec has its register names resolved against the target register table. The resulting plan replaces the spec, which is then removed from the IR.
- A function with neither, but whose target registers a calling convention through the plugin ABI, is planned by that convention's `PlanCallingConvention` callback.

Every direct call site inherits its callee's plan, which is what keeps caller and callee agreeing on the layout across translation units. The plan is a flat string:

```
neverc-cc-plan-v1;schema=<digest>;target=<high>:<low>;cc=<high>:<low>;stack=<bytes>;returns=<locations>;arguments=<locations>;callee-saved=<register numbers>
```

Each location is `<r|s>,<value index>,<piece offset>,<size>,<alignment>,<register number>,<stack offset>,<flags>`, and multiple locations are separated by `|`. For the built-in path the schema digest is `llvm-<target triple>`; a plugin-registered target supplies its own.

Because register numbers are only meaningful against the schema that defines them, a mismatch is a hard error rather than a silent miscompile:

| Situation | Diagnostic |
|---|---|
| The plan string does not parse | `malformed NeverC calling convention plan` |
| The schema digest differs | `NeverC calling convention plan belongs to a foreign target schema` |
| The target ID differs | `NeverC calling convention plan has a foreign target ID` |
| The convention ID differs | `NeverC calling convention plan has a foreign convention ID` |

This is what makes a plan safe to embed in bitcode and carry through LTO: a plan produced for another target cannot be applied by accident.

## Plugin API

The example plugin uses only the stable IR core table — there is no dedicated calling-convention entry point. Applying a convention to a function is three calls plus call-site synchronization:

```c
NevercIRAttributeHandle Attribute = {0};
Core->CreateStringAttribute(Core->Context, Task, SV("neverc-callconv"), Spec,
                            &Attribute);
Core->AddFunctionAttribute(Core->Context, Task, Function,
                           NEVERC_IR_ATTRIBUTE_LOCATION_FUNCTION, 0, Attribute);
Core->SetFunctionCallingConvention(Core->Context, Task, Function,
                                   NEVERC_IR_CALLING_CONVENTION_NEVER_C_CUSTOM);
```

`NEVERC_IR_CALLING_CONVENTION_NEVER_C_CUSTOM` is the ABI-stable name for `CallingConv::NeverC_Custom` (LLVM value 1000). The plugin then walks the function's uses with `GetValueUseCount` / `GetValueUse`, and for every use that is the callee operand of a `call`, `invoke`, or `callbr` it sets the same convention on the instruction via `SetInstructionProperty` with `NEVERC_IR_PROPERTY_CALLING_CONVENTION`. Any other use means the address escaped, which is where the address-taken warning comes from.

A plugin that registers its own target can instead supply a `PlanCallingConvention` callback on its `NevercCallingConventionDescriptor` and produce plans directly, skipping the spec layer. See [Target, MC, assembly, object](../target-mc-object.md).

## Testing

The GoogleTest suite lives in [`tests/neverc/CustomCallConvTests.cpp`] and holds 26 tests. Each one builds the example plugin, compiles a small program to assembly under a given spec, and asserts the resulting register or stack placement.

```bash
ninja -C build-neverc neverc-tests
build-neverc/bin/neverc-tests --gtest_filter='CustomCallConvTest.*'
```

Coverage:

| Category | Tests |
|---|---|
| x86-64 pool / positional / stack / spill / i64 / sret / byval / fallback | 9 |
| AArch64 GPR / FPR / stack / `csr` / mixed-spec cross-call | 5 |
| Frontend `custom_attr` (GNU / `__declspec` / end-to-end) | 3 |
| Plan materialization and schema rejection | 3 |
| Hardening (`csr`, varargs on both targets, indirect, `rsp`, csr conflict) | 6 |

## Architecture

```
Source attribute              Plugin IR pass
custom_attr(...)              (neverc.ir.pass.post_opt)
       │                            │
       └─────────────┬──────────────┘
                     ▼
   "neverc-callconv" = spec, CallingConv::NeverC_Custom
   on the function and its direct call sites
                     │
                     ▼
   ┌──────────────────────────────────────────┐
   │ materializeCallingConventionPlans        │
   │ (after optimization, before codegen)     │
   │                                          │
   │  spec          → names to physregs       │
   │  plugin CC     → PlanCallingConvention   │
   │  existing plan → validate schema/target  │
   └──────────────────────────────────────────┘
                     │
                     ▼
   "neverc-cc-plan-v1" = validated locations
   spec removed; plan copied to direct call sites
                     │
                     ▼
   ┌──────────────────────────────────────────┐
   │ Backend CCAssignFn (one per target)      │
   │  CC_X86_NeverC     / RetCC_X86_NeverC    │
   │  CC_AArch64_NeverC / RetCC_AArch64_NeverC│
   │                                          │
   │  reads the plan → assigns locations      │
   │  unmatched values → standard convention  │
   │  tail calls disabled                     │
   └──────────────────────────────────────────┘
                     │
                     ▼
   Machine code with the custom register layout
```

The backend executor is a **one-time implementation** — all policy decisions live in the plugin. Adding a new convention never requires rebuilding NeverC.

<!-- reference links -->
[`CustomCallConvPlugin.c`]: ../../../pluginsdk/examples/CustomCallConvPlugin.c
[`llvm/include/llvm/CodeGen/NeverCCallConv.h`]: ../../../llvm/include/llvm/CodeGen/NeverCCallConv.h
[`tests/neverc/CustomCallConvTests.cpp`]: ../../../tests/neverc/CustomCallConvTests.cpp

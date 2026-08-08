# Android Kernel Module Release Strip Implementation Plan

> **Status:** Completed and verified on 2026-08-08. Steps use checkbox syntax to preserve the implementation audit trail.

**Goal:** Let `neverc --strip` safely produce a release Android kernel module while keeping the ELF symbol table, relocations, imports, and module-loader ABI that an `ET_REL` `.ko` requires.

**Architecture:** Keep generic relocatable outputs rejected. Define one shared driver predicate for a final Android module (`Android target + -fandroid-kernel-driver-mode + -r + .ko`), then route strip intent into the verified ELF relocatable merger. The merger applies `llvm-strip --strip-unneeded`-style pruning only after relocation indices and NeverC parallel-codegen symbols have reached their final form; it rebuilds the symbol string table and independently verifies the reduced output. The example exposes this as an explicit `make release` profile; `clean` remains deletion-only.

**Tech Stack:** C++17, LLVM ELF/Object APIs, NeverC embedded linker and object merger, GoogleTest, GNU Make, Markdown.

---

## File map

- `neverc/lib/Invoke/ToolChains/CommonArgs.{h,cpp}`: owns the single semantic predicate that identifies a delivered Android `.ko`.
- `neverc/lib/Invoke/Core/Driver.cpp`: admits `--strip` only for ordinary final images or the narrowly identified `.ko` exception.
- `neverc/lib/Invoke/ToolChains/Gnu.cpp`: uses the shared predicate when populating `LinkerDriverConfig`.
- `neverc/include/neverc/Merge/Merger.h`: declares the relocation-safe unneeded-symbol policy.
- `neverc/lib/Merge/ELF/MergerELF.cpp`: removes debug sections, `.comment`, and relocation-unneeded local/undefined symbols; remaps relocation indices and rebuilds `.strtab`.
- `neverc/lib/Merge/Verify/MergerVerify.cpp`: models the same allowed symbol omissions without weakening relocation and module-ABI checks.
- `neverc/lib/Linker/Backends/ELF/Driver/ElfDriver.cpp`: forwards driver strip intent into only the final Android-module merger path.
- `neverc/lib/Plugin/Link/BuiltinObjectMergeAdapter.{h,cpp}` and `neverc/lib/Plugin/Link/LinkExecutionHooksBridge.cpp`: preserve the same behavior when object-merge plugins are active.
- `tests/neverc/{DriverTests,MergeTests,LTOTests}.cpp`: command-scope, low-level remap, and real `.ko` regressions.
- `examples/android-kernel-hello/Makefile`: adds persistent `debug`/`release` profiles and keeps `clean` side-effect free.
- `examples/android-kernel-hello/README*.md` and `docs/release-builds/README*.md`: document profile use, loader-safe preservation, signing order, and security limits in every maintained locale.

### Task 1: Lock the driver scope with failing tests

- [x] Add a `DriverTests.cpp` case proving ordinary `-r --strip` and Android intermediate `.o --strip` still fail.
- [x] Add the positive scope case `--target=aarch64-linux-android -fandroid-kernel-driver-mode -r --strip -o final.ko`.
- [x] Run `build-neverc/tests/neverc/neverc-driver-tests --gtest_filter='DriverTest.StripOption*'`; expect the new `.ko` case to fail with the current “only applies to final linked” diagnostic.
- [x] Add `tools::isFinalAndroidKernelModule(Triple, Args, OutputFile)` and use it from both `Driver.cpp` and `Gnu.cpp`.
- [x] Re-run the filtered driver tests; expect the scope tests to pass.

The admission rule must remain equivalent to:

```cpp
return Target.isAndroid() &&
       Args.hasArg(options::OPT_fandroid_kernel_driver_mode) &&
       Args.hasArg(options::OPT_r) &&
       llvm::sys::path::extension(OutputFile) == ".ko";
```

### Task 2: Specify module-safe strip in merger tests

- [x] Add a `MergeTests.cpp` object containing an unreferenced local, a relocation-referenced local, an unreferenced undefined symbol, a referenced undefined import, and a global definition.
- [x] Enable the new option and assert that only the unreferenced local/undefined entries disappear.
- [x] Assert `.symtab`, `.strtab`, all retained relocation sections, and Android boundary symbols remain.
- [x] Assert removed names do not survive as stale bytes in `.strtab`.
- [x] Run `build-neverc/tests/neverc/neverc-merge-tests --gtest_filter='MergeELFSemantic.AndroidKernelModuleSafeStrip*'`; expect failure before implementation.

### Task 3: Implement relocation-safe pruning

- [x] Add `Options::stripUnneededSymbols`, documented as an ELF relocatable policy requiring every relocation target to survive.
- [x] Keep collecting and remapping symbols normally, including the existing `.__pcg` demotion.
- [x] After final relocation remap, collect every referenced output symbol index.
- [x] Retain symbol zero, all defined non-local symbols, and every relocation-referenced symbol; remove other local or undefined symbols.
- [x] Build an old-to-new index map, fail closed if any retained relocation lacks a mapping, and update every relocation.
- [x] Rebuild `SymStrTab` from retained entries so removed names are absent from raw bytes.
- [x] When this policy is active for an Android final module, also omit `.comment`; preserve `.codetag.alloc_tags`, `__versions`, `.gnu.linkonce.this_module`, relocation sections, `.symtab`, and `.strtab`.
- [x] Teach the independent verifier which input symbols are intentionally absent while retaining symbol-order, relocation-target, section-range, and Android loader-contract checks.
- [x] Re-run the focused merger test and the complete `neverc-merge-tests` suite.

### Task 4: Route strip intent through native and plugin paths

- [x] In `ElfDriver.cpp`, set `dropDebugInfo` from `driverCfg.stripsDebugInfo()` for the admitted `.ko` and set `stripUnneededSymbols` only when `finalizeAndroidKernelModule && driverCfg.stripsSymbols()`.
- [x] Add equivalent fields to `BuiltinObjectMergeConfig` and forward them from `LinkExecutionHooksBridge.cpp`.
- [x] For a custom object-merge provider, invalidate native-image passthrough after applying the host-owned debug/unneeded-symbol finalization to its typed output graph, so a plugin cannot bypass `--strip`.
- [x] Keep the final output validators fail-closed for profile-contract, debug-section, and unneeded-symbol invariants.
- [x] Run relevant plugin object-merge tests plus the new real `.ko` test.

### Task 5: Add an end-to-end `.ko` release regression

- [x] Extend `LTOTests.cpp` to build a debug `.ko` and a `-g --strip` `.ko` for native, Auto-LTO, and full-LTO modes.
- [x] Assert the release image is parseable `ET_REL`, has exactly one `.symtab` linked to `.strtab`, retains relocations and required imports/globals, has no `.debug*`, and omits a deliberately unneeded local name.
- [x] Assert the normal debug image still contains DWARF and that private symbol.
- [x] Run the filtered LTO test, then the complete `neverc-lto-tests` suite if runtime permits.

### Task 6: Add the example release profile

- [x] Change `examples/android-kernel-hello/Makefile` to persist `PROFILE` beside `KERNEL` in `.nvk-build-flags`.
- [x] Make the first build default to `debug`, with `-g`; make `release` recursively invoke `$(MAKE) PROFILE=release all` with `-O2 --strip`; add an explicit `debug` target for switching back.
- [x] Keep `clean` limited to `rm -f $(MODULE) *.o $(FLAGS_STAMP)`; never strip, sign, or otherwise mutate an existing module from `clean`.
- [x] Run `make -n debug`, `make -n release`, and two real builds with the rebuilt compiler; inspect sections/symbols using `llvm-readelf`.

### Task 7: Synchronize documentation and audit the final diff

- [x] Update all eleven `android-kernel-hello` READMEs with `neverc make release`, profile persistence, and signing-after-strip guidance.
- [x] Update all eleven release-build guides: generic `-r` remains rejected, final Android `.ko` is the only relocatable exception, and module-safe strip is not ELF strip-all.
- [x] State explicitly that required relocation targets and loader metadata may still expose names, and that `.codetag.alloc_tags` must not be removed blindly.
- [x] Run link/locale checks, `git diff --check`, focused tests, and a final Symptom → Source → Consequence → Remedy architecture review.
- [x] Leave changes uncommitted unless the user explicitly requests a commit.

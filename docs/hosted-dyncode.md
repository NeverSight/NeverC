**Languages**: [English](hosted-dyncode.md) | [简体中文](zh-CN/hosted-dyncode.md) | [繁體中文](zh-TW/hosted-dyncode.md) | [日本語](ja/hosted-dyncode.md) | [한국어](ko/hosted-dyncode.md) | [Français](fr/hosted-dyncode.md) | [Deutsch](de/hosted-dyncode.md) | [Español](es/hosted-dyncode.md) | [Italiano](it/hosted-dyncode.md) | [Русский](ru/hosted-dyncode.md) | [العربية](ar/hosted-dyncode.md)

[← DynCode](dyncode-compiler.md)

# Hosted DynCode and C++ translation

Use `-fdyncode-hosted` for a code-only function installed into an existing program. It requires `-fdyncode-keep-obj`, retains compiler CFI/SEH in that object, and excludes metadata from the flat payload. One native code section and an entry at offset zero preserve the correspondence with unwind records.

Hosted mode disables implicit syscall/PEB, heap-arena and string runtime lowering. Mutable/TLS globals and unresolved runtime calls fail; parameters may carry data or callback pointers. Standalone DynCode keeps its existing behavior.

Core C++ translation now accepts `--target` and binds the generated C23, source map and manifest to that ABI. `cpp-core-v2` supports its documented C++17 subset, including templates and ordinary constructor/destructor lifetimes. It does not implement C++ exceptions, RTTI or virtual dispatch; cleanup during foreign exception propagation is not part of that profile.

```sh
neverc translate --from cpp replacement.cpp --profile cpp-core-v2 \
  --target aarch64-unknown-linux-gnu -o replacement.nc -- -std=c++17
neverc --no-default-config --target=aarch64-unknown-linux-gnu \
  -fdyncode -fdyncode-hosted -fdyncode-entry=replacement \
  -fdyncode-keep-obj=replacement.o -fdyncode-report=replacement.audit.json \
  -o replacement.bin replacement.nc
```

The audit names `execution_profile="hosted-v1"`, `keep_unwind=true` and `target_triple`. `input_digest` hashes the kept object; `output_digest` hashes the final relocated and verified payload. A hosted installer must verify both digests and exact linked code/entry equality, then register actual compiler unwind records.

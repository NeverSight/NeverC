**Languages**: [English](../hosted-dyncode.md) | [简体中文](../zh-CN/hosted-dyncode.md) | [繁體中文](../zh-TW/hosted-dyncode.md) | [日本語](../ja/hosted-dyncode.md) | [한국어](../ko/hosted-dyncode.md) | [Français](../fr/hosted-dyncode.md) | [Deutsch](../de/hosted-dyncode.md) | [Español](hosted-dyncode.md) | [Italiano](../it/hosted-dyncode.md) | [Русский](../ru/hosted-dyncode.md) | [العربية](../ar/hosted-dyncode.md)

[← DynCode](dyncode-compiler.md)

# DynCode alojado y traducción C++

Use `-fdyncode-hosted` para instalar funciones de solo código en programas existentes. Requiere `-fdyncode-keep-obj`: conserva CFI/SEH del compilador en el objeto y las excluye de la carga plana. Una sección nativa y una entrada en cero mantienen la relación con los registros de desenrollado.

Desactiva conversiones implícitas syscall/PEB, arena de heap y biblioteca de cadenas. Las globales modificables/TLS y llamadas no resueltas fallan. Los parámetros pueden transportar datos o callbacks. DynCode independiente mantiene su comportamiento.

La traducción C++ básica acepta `--target` y vincula C23, mapa y manifiesto a esa ABI. `cpp-core-v2` admite el subconjunto C++17 documentado, incluidas plantillas y ciclos normales de construcción/destrucción. No admite excepciones C++, RTTI, despacho virtual ni limpieza al propagarse excepciones externas.

```sh
neverc translate --from cpp replacement.cpp --profile cpp-core-v2 \
  --target aarch64-unknown-linux-gnu -o replacement.nc -- -std=c++17
neverc --no-default-config --target=aarch64-unknown-linux-gnu \
  -fdyncode -fdyncode-hosted -fdyncode-entry=replacement \
  -fdyncode-keep-obj=replacement.o -fdyncode-report=replacement.audit.json \
  -o replacement.bin replacement.nc
```

La auditoría registra `execution_profile="hosted-v1"`, `keep_unwind=true` y `target_triple`. `input_digest` identifica el objeto conservado; `output_digest`, la carga final reubicada y verificada. El instalador debe validar ambos hashes, código y entrada antes de registrar el desenrollado real.

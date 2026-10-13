**Languages**: [English](../hosted-dyncode.md) | [简体中文](../zh-CN/hosted-dyncode.md) | [繁體中文](../zh-TW/hosted-dyncode.md) | [日本語](../ja/hosted-dyncode.md) | [한국어](../ko/hosted-dyncode.md) | [Français](../fr/hosted-dyncode.md) | [Deutsch](../de/hosted-dyncode.md) | [Español](../es/hosted-dyncode.md) | [Italiano](hosted-dyncode.md) | [Русский](../ru/hosted-dyncode.md) | [العربية](../ar/hosted-dyncode.md)

[← DynCode](dyncode-compiler.md)

# DynCode ospitato e traduzione C++

Usare `-fdyncode-hosted` per funzioni di solo codice in programmi esistenti. Richiede `-fdyncode-keep-obj`: conserva CFI/SEH del compilatore nell’oggetto e le esclude dal payload piatto. Una sezione nativa con ingresso a offset zero conserva il legame con i record di unwind.

Disabilita conversioni implicite syscall/PEB, arena heap e runtime stringhe. Globali modificabili/TLS e chiamate irrisolte falliscono. Dati e callback possono essere parametri. DynCode autonomo mantiene il comportamento precedente.

La traduzione C++ core accetta `--target` e lega C23, mappa e manifest a quella ABI. `cpp-core-v2` supporta il sottoinsieme C++17 documentato, inclusi template e normali costruttori/distruttori. Eccezioni C++, RTTI, dispatch virtuale e pulizia durante eccezioni esterne sono esclusi.

```sh
neverc translate --from cpp replacement.cpp --profile cpp-core-v2 \
  --target aarch64-unknown-linux-gnu -o replacement.nc -- -std=c++17
neverc --no-default-config --target=aarch64-unknown-linux-gnu \
  -fdyncode -fdyncode-hosted -fdyncode-entry=replacement \
  -fdyncode-keep-obj=replacement.o -fdyncode-report=replacement.audit.json \
  -o replacement.bin replacement.nc
```

L’audit contiene `execution_profile="hosted-v1"`, `keep_unwind=true` e `target_triple`. `input_digest` verifica l’oggetto conservato; `output_digest`, il payload finale rilocato e verificato. L’installatore deve verificare hash, codice e ingresso e registrare i veri record di unwind.

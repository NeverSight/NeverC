**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Documentazione NeverC](../README.it.md)

# L'estensione file `.nc`

## Panoramica

NeverC riconosce `.nc` come estensione nativa per i file sorgente. Quando il compilatore rileva un file di input `.nc`, **abilita automaticamente** tutte le estensioni del linguaggio NeverC — senza bisogno di flag aggiuntivi.

## Funzionalità abilitate automaticamente

| Flag | Effetto |
|------|---------|
| `-fneverc-types` | Alias interi in stile Rust (`u8`, `u16`, `u32`, `u64`, `i8`, `i16`, `i32`, `i64`, `usize`, `isize`) |
| `-fbuiltin-string` | Tipo valore `string` integrato con gestione automatica della memoria, sintassi dot-call e supporto UTF-8 |

## Utilizzo

Basta nominare il file sorgente con l'estensione `.nc`:

```bash
# Automatico — non servono -fbuiltin-string né -fneverc-types
neverc hello.nc -o hello

# Equivalente a:
neverc -fneverc-types -fbuiltin-string hello.c -o hello
```

```c
// hello.nc
#include <stdio.h>

int main(void) {
    string greeting = "Ciao, NeverC!";
    printf("%s (len=%zu)\n", greeting.c_str(), greeting.len);

    u32 x = 42;
    i64 y = -100;

    string msg = greeting + " x=%u, y=%lld".format(x, y);
    printf("%s\n", msg.c_str());
    return 0;
}
```

## Come funziona

Il rilevamento opera su due livelli della pipeline del compilatore:

### 1. Livello Driver / Toolchain

Il driver ispeziona l'estensione di ciascun file di input prima di costruire l'invocazione del compilatore. Per i file `.nc`, `-fneverc-types` e `-fbuiltin-string` vengono iniettati incondizionatamente nella riga di comando — l'utente non deve passarli manualmente.

Per i file `.c`, questi flag rimangono opzionali: l'utente deve passare esplicitamente `-fneverc-types` e/o `-fbuiltin-string`.

### 2. Livello CompilerInvocation

Come rete di sicurezza, il frontend verifica anche le estensioni dei file di input durante l'analisi dell'invocazione. Se un input ha l'estensione `.nc`, `LangOpts.NeverCTypes` e `LangOpts.BuiltinString` vengono impostati a `1`, garantendo che le funzionalità siano attive anche se il livello driver viene bypassato (ad es., invocando direttamente `-cc1`).

## Compatibilità

- I file `.nc` vengono trattati come codice sorgente C — il linguaggio è ancora C (C23 di default), non un nuovo linguaggio
- Tutti i flag C standard (`-std=c11`, `-O2`, `-g`, `-Wall`, ecc.) funzionano in modo identico
- `-fshellcode` si combina naturalmente con `.nc`: la modalità shellcode abilita già `string`, e `.nc` assicura che anche `neverc-types` sia attivo
- La cross-compilazione (`-target aarch64-linux-gnu`, ecc.) funziona allo stesso modo
- I file `.c` non sono interessati — si comportano esattamente come prima a meno che non si passino esplicitamente i flag

## Quando usare `.nc` vs `.c`

| Scenario | Raccomandazione |
|----------|----------------|
| Nuovo progetto NeverC con `string` e tipi Rust | Usare `.nc` |
| Codebase C esistente da mantenere compatibile con altri compilatori | Usare `.c` + flag espliciti |
| Progetto shellcode | Entrambi vanno bene — `-fshellcode` abilita `string` comunque |
| Codebase mista | `.nc` per file NeverC, `.c` per codice portabile |

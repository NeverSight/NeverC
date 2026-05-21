**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Documentazione NeverC](../README.it.md)

# Sistema Runtime Integrato di NeverC

NeverC estende il C standard con runtime integrati opzionali, incorporati direttamente nel binario del compilatore come bitcode LLVM. Una volta attivati tramite flag del compilatore, il runtime corrispondente viene fuso nell'IR dell'utente al momento della compilazione — senza necessità di header esterni, librerie o dipendenze di link.

## Funzionalità Integrate Disponibili

| Integrato | Flag | Predefinito | Descrizione |
|-----------|------|------------|-------------|
| [**`string`**](../builtin-string/README.it.md) | `-fbuiltin-string` | Disattivato | Tipo stringa con semantica di valore, metodi con sintassi a punto, gestione automatica della memoria e UTF-8 nativo |
| [**mimalloc**](mimalloc/README.it.md) | `-fbuiltin-mimalloc` | Disattivato | Allocatore di memoria ad alte prestazioni che sostituisce trasparentemente `malloc`/`free`/`calloc`/`realloc` |

```bash
neverc -fbuiltin-string -fbuiltin-mimalloc main.c -o main
```

---

## Panoramica dell'Architettura

Tutte le funzionalità integrate condividono la stessa architettura a quattro livelli:

1. **Opzioni di linguaggio e flag del driver** — `LangOption` definito in `LangOptions.def`
2. **API Foundation** — fornisce `getEmbeddedBitcode()` e `isSupported()`
3. **Infrastruttura CMake Bootstrap** — generazione bitcode in due fasi
4. **Pass di fusione IR** — fusione bitcode nel modulo utente a `PipelineStartEP`

---

## Differenze di Progettazione tra Integrati

| Aspetto | `string` | `mimalloc` |
|---------|----------|------------|
| **Strategia di fusione** | On-demand (BFS grafo delle chiamate) | Archivio completo (tutti i simboli) |
| **Bitcode per piattaforma** | Singolo (indipendente dall'architettura) | Per SO (Linux / Darwin / Windows) |
| **Gestione simboli** | Tutti internalizzati | Punti di ingresso di override mantengono linkage esterno |
| **Macro preprocessore** | `__NEVERC_BUILTIN_STRING__` | `__NEVERC_MIMALLOC__` |
| **Modalità shellcode** | Auto-attivata, riscrittura arena | Soppressa (nessun heap in shellcode) |

---

## Interblocchi di Sicurezza

| Condizione | Effetto | Motivo |
|------------|---------|--------|
| `-fno-builtin` | Sopprime mimalloc | Nessuno scenario di override CRT |
| `-mkernel` | Sopprime mimalloc | Nessun heap userspace nel kernel |
| `-fshellcode-mode` | Sopprime mimalloc | Nessun heap in shellcode |
| `-ffreestanding` | Sopprime mimalloc | Nessuna libc da sovrascrivere |

---

## Aggiungere una Nuova Funzionalità Integrata

1. Aggiungere `LANGOPT` in `LangOptions.def`
2. Aggiungere flag del driver in `Options.td.h`
3. Creare API Foundation (`BuiltinFoo.h` + `.cpp`)
4. Creare generatore di sorgenti
5. Aggiungere target CMake bootstrap
6. Creare pass IR e registrare a `PipelineStartEP`
7. Definire macro preprocessore
8. Aggiungere controlli di sicurezza
9. Aggiungere test
10. Aggiungere documentazione e traduzioni i18n

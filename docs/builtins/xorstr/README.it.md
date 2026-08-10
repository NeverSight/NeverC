**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Sistema runtime integrato NeverC](../README.it.md)

# Crittografia delle stringhe a tempo di compilazione (`xorstr`)

## Panoramica

NeverC fornisce una crittografia delle stringhe a due livelli a tempo di compilazione per il codice C, progettata per scenari di sicurezza in cui le stringhe in chiaro (nomi API, percorsi del registro) non devono essere visibili nel binario compilato.

- **Livello 1 — Macro esplicita**: `NC_XORSTR("string")` / `NEVERC_XORSTR("string")` per il controllo preciso per stringa
- **Livello 2 — Pass IR automatico**: `-fencrypt-call-strings` per crittografare automaticamente tutti gli argomenti stringa nelle chiamate di funzione

Entrambi i livelli usano buffer allocati sullo stack (nessuna allocazione heap), flussi di chiave per istanza e pulizia volatile. Al confine del codice macchina nativo, le chiamate esplicite al decoder `NC_XORSTR` vengono ricifrate ed espanse direttamente in ogni call site; l'oggetto finale non conserva una funzione decoder condivisa.

---

## Avvio rapido

```c
#include <neverc/xorstr/xorstr.h>
FARPROC addr = GetProcAddress(hModule, NC_XORSTR("NtQuerySystemInformation"));
```

```bash
neverc -fencrypt-call-strings main.c -o main
```

---

## Flusso di protezione

1. **Sema** cifra ogni letterale con una chiave propria. Il seed `0` ottiene nuova entropia dal sistema operativo; `-fstring-encrypt-key=` seleziona un output deterministico a 64 bit.
2. **IR intermedio / input LTO** mantiene una chiamata opaca e non specializzabile al decoder, impedendo alle ottimizzazioni di rimaterializzare il testo in chiaro.
3. **Confine finale del codice macchina** decifra e ricifra il ciphertext lato compilatore, sceglie una forma del loop per call site, la espande sul posto e rimuove decoder, grafo helper, ancora ABI, stato di routing e nomi semantici.
4. **Pulizia** viene installata prima dell'ottimizzazione o del provider e ripetuta nella coda finale; la seconda esecuzione è idempotente e ripara il posizionamento dopo modifiche al CFG.

### Diversità del decoder

Sequenza di stato, costanti, ciphertext ed espressioni equivalenti per byte variano con seed e call site. Una forma possibile è `a + b − 2 × (a & b)`. I caricamenti volatile di stato/ciphertext ostacolano il constant folding e `nooutline` impedisce a Machine Outliner di ricreare un decoder condiviso dopo la finalizzazione IR.

IDA non trova quindi una routine autonoma e stabile da riconoscere o emulare una sola volta. Questo non implica che il testo in chiaro necessario durante l'esecuzione sia irrecuperabile tramite strumentazione dinamica.

---

## Crittografia automatica e pulizia

`-fencrypt-call-strings` viene eseguito prima dell'IPO, dopo l'ottimizzazione ordinaria e di nuovo dopo ogni fase IR tardiva ordinaria o fornita da plugin. LTO applica la stessa sigillatura obbligatoria dopo gli hook del provider e pre-codegen.

Sono elaborati gli argomenti `CallBase` diretti e indiretti provenienti da letterali privati `unnamed_addr` di proprietà del compilatore; GEP, cast, `freeze`, `select`, PHI e slot locali di puntatori promuovibili conservano la semantica. Intrinsic, assembly inline, array visibili esternamente o definiti dall'utente e letterali troppo grandi vengono esclusi. Un letterale protetto passato a `musttail` fa fallire la compilazione in modo sicuro.

`XorStrCleanupPass` azzera l'intero buffer con `memset` volatile prima di ogni `ret`, `resume`, `cleanupret` che esegue unwind al chiamante e unwind `catchswitch` non intercettato. Lo storage non sicuro o non completamente tracciabile viene rifiutato invece di essere cancellato solo in parte.

---

## Riferimento flag del compilatore

| Flag | Descrizione |
|------|-------------|
| `-fencrypt-call-strings` | Abilita la crittografia automatica delle stringhe |
| `-fno-encrypt-call-strings` | Disabilita la crittografia automatica |
| `-fencrypt-call-strings-max-len=N` | Lunghezza massima in byte (predefinito: 1024) |
| `-fstring-encrypt-key=0xHEX` | Sovrascrive il seed completo a 64 bit; `0` usa nuova entropia |

## Confini di output e riproducibilità

- `-fno-lto` finalizza durante la generazione nativa del frontend.
- Auto-LTO e Full LTO mantengono il decoder opaco nel bitcode pre-link, quindi lo ricifrano ed espandono dopo l'ottimizzazione globale e dei plugin.
- Le pipeline sostituite da provider e i pass tardivi dei plugin sono sempre seguiti da crittografia, pulizia e finalizzazione obbligatorie.
- Con il seed predefinito, build native indipendenti differiscono; vengono bypassate le cache whole-link e di partizione che potrebbero riutilizzare vecchio codice protetto.
- Un seed non zero è volutamente deterministico e compatibile con la cache: stesso input e stesso seed completo a 64 bit producono lo stesso codice protetto.
- `-emit-llvm` e il bitcode pre-link sono artefatti intermedi e conservano intenzionalmente l'ABI opaca. La garanzia “nessun decoder condiviso” vale per il codice macchina finale generato con successo.

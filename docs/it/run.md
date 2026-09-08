**Lingue**: [English](../run.md) | [简体中文](../zh-CN/run.md) | [繁體中文](../zh-TW/run.md) | [日本語](../ja/run.md) | [한국어](../ko/run.md) | [Français](../fr/run.md) | [Deutsch](../de/run.md) | [Español](../es/run.md) | [Italiano](run.md) | [Русский](../ru/run.md) | [العربية](../ar/run.md)

[← Indice documentazione](README.md) · [← Progetto NeverC](project.md)

# `neverc run`

Compila un programma C o NeverC in un **eseguibile temporaneo**, lo esegue sull'**host locale**, ne restituisce lo stato di uscita e rimuove l'artefatto dopo. Il flusso è volutamente simile a `go run`.

Quando devi conservare il binario, distribuirlo o debuggarlo, usa l'invocazione normale del compilatore (`neverc ... -o output`).

## Sintassi

```text
neverc run [flag compilatore] file.c [file2.nc ...] [argomenti programma...]
neverc run [argomenti compilatore...] -- [argomenti programma...]
```

Anche `neverc run --help` mostra un riepilogo integrato.

## Parsing degli argomenti

`neverc run` divide gli argomenti in **invocazione compilatore** e **argomenti programma** opzionali con una di due regole.

### Divisione predefinita (stile Go)

1. Scansionare da sinistra a destra fino al primo argomento che termina in `.c` o `.nc` e non inizia con `-`.
2. **Tutto fino alla fine della serie continua di `.c`/`.nc` (inclusa), insieme a ciò che precede la prima sorgente**, va al compilatore.
3. **Tutto dopo** va a `argv` del programma temporaneo.

Esempi:

```bash
neverc run -O2 -fbuiltin-string hello.c
neverc run -O2 main.c helper.nc -- --verbose two words
neverc run -DGENERATED=.c -O2 main.c argument
```

Note:

- Solo `.c` e `.nc` contano come sorgenti run. Un flag `-DGENERATED=.c` resta lato compilatore.
- Più sorgenti producono un solo binario temporaneo, come un link multi-file normale.

### Separatore `--` esplicito

Quando il compilatore ha bisogno di argomenti **dopo** l'elenco sorgenti (flag di link, input non sorgente, `-x c -`, ecc.), metti `--` tra coda compilatore e argomenti programma:

```bash
neverc run hello.c helper.o -lm -- arg.c -x
neverc run hello.c -O1 -- x
```

Tutto prima di `--` viene inoltrato a `neverc` (più un `-o <temp>` interno). Tutto dopo diventa argomenti programma.

## Comportamento a runtime

| Argomento | Comportamento |
|-----------|---------------|
| Directory di lavoro | Il programma temporaneo gira nella **directory corrente** |
| Ambiente | Eredita l'ambiente corrente (`PATH`, variabili esportate, ecc.) |
| I/O standard | stdin/stdout/stderr collegati al processo temporaneo |
| Stato di uscita | In successo, quello del **programma**; se la compilazione fallisce, quello del **compilatore** senza avviare il programma |
| File temporanei | L'eseguibile vive in `neverc-run-*`; la directory viene rimossa dopo l'esecuzione. Un fallimento della pulizia viene segnalato separatamente. |

## Esempi

```bash
neverc run -O2 -fbuiltin-string hello.c
neverc run -fbuiltin-string greet.c -- Alice "two words"
neverc run -O2 main.c util.nc -- --port 8080
neverc run app.c extra.o -lm -- --config prod.json
```

## Limiti e avvertenze

- **Solo esecuzione host.** I flag di cross-compilazione (`-target ...`) possono compilare, ma il binario temporaneo viene sempre eseguito localmente.
- **Nessun artefatto persistente.** Il binario viene eliminato alla fine — usa `neverc ... -o out` per il debug.
- **Stessa toolchain di `neverc`.** Il comando reinvoca lo stesso binario `neverc`.
- **Sorgenti `.nc`.** Stesse regole di `.c`; le estensioni NeverC si applicano automaticamente.

## Comandi correlati

| Comando | Quando usarlo |
|---------|---------------|
| `neverc file.c -o out` | Conservare binario, cross-compilare, script di build |
| [`neverc build` / `neverc make`](build.md) | Build compatibili GNU Make guidati da un Makefile |
| `neverc run --help` | Riepilogo integrato |

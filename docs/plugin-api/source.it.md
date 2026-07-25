**Lingue**: [English](source.md) | [简体中文](source.zh-CN.md) | [繁體中文](source.zh-TW.md) | [日本語](source.ja.md) | [한국어](source.ko.md) | [Français](source.fr.md) | [Deutsch](source.de.md) | [Español](source.es.md) | [Italiano](source.it.md) | [Русский](source.ru.md) | [العربية](source.ar.md)

# API dei plugin per Source e I/O

La prima ABI pubblica dei plugin espone input sorgente, file virtuali,
dipendenze e output del compilatore tramite `PluginSource.h`. Tutti i percorsi
sono percorsi VFS normalizzati e tutti gli handle sono limitati al task
`TranslationUnit` corrente.

## Fasi source

La pipeline source stabile è:

1. `neverc.source.resolve_input` convalida e normalizza l'input richiesto.
2. `neverc.source.open` lo apre attraverso il VFS composto host/plugin.
3. `neverc.source.after_open` pubblica un evento di sola lettura per il
   `SourceUnit` verificato.

`resolve_input` è osservabile e intercettabile; `open` è inoltre sostituibile.
L'host verifica ogni sostituzione prima di pubblicarla come `SourceUnit`. Un
plugin non può sostituire `after_open`.

## Provider VFS

Interrogare `NevercIOAPI` durante la registrazione del plugin e chiamare
`RegisterVFSProvider`. Un provider risponde prima a `MatchesPath`, poi
implementa le operazioni di cui è responsabile. Restituire
`NEVERC_VFS_RESULT_NOT_HANDLED` delega al provider successivo; restituire
`HANDLED` rende uno stato o un contenuto malformato un errore irrecuperabile
anziché un ripiego silenzioso.

I buffer restituiti da un provider sono presi in prestito solo per la durata
della callback. NeverC copia i byte accettati in memoria di proprietà del task.
I provider devono dichiarare se il loro risultato è deterministico e
memorizzabile in cache.

L'esempio compilabile
[`VirtualHeaderPlugin.c`](../../pluginsdk/examples/VirtualHeaderPlugin.c)
fornisce un header in memoria senza aggirare il VFS dell'host.

## Sink di output e dipendenze

Gli output su file e in memoria usano lo stesso sink transazionale:

- scrivere in un candidato;
- chiamare finish per renderlo idoneo alla verifica;
- lasciare che il gate sigillato dell'host lo verifichi;
- eseguire il commit atomico se il task riesce, oppure abortire in caso di
  errore o annullamento.

Un plugin non pubblica mai scrivendo direttamente nel percorso di destinazione.
Le destinazioni in streaming che non possono essere annullate rifiutano le
trasformazioni che richiedono un candidato atomico. I record di dipendenza usano
identità VFS normalizzate, così i file nativi e quelli forniti dai plugin hanno
la stessa provenienza e la stessa semantica di cache.

## Regole di sicurezza

- Non conservare handle di source, file, buffer, sink o task dopo la callback.
- Trattare `NevercStringView` e `NevercByteView` come viste delimitate da una
  lunghezza.
- Usare l'allocatore dell'host quando i dati devono sopravvivere alla callback.
- Non usare le API di filesystem dell'host dietro il contratto VFS.
- Controllare l'annullamento prima di un lavoro oneroso del provider.

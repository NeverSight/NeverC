**Lingue**: [English](prep.md) | [简体中文](prep.zh-CN.md) | [繁體中文](prep.zh-TW.md) | [日本語](prep.ja.md) | [한국어](prep.ko.md) | [Français](prep.fr.md) | [Deutsch](prep.de.md) | [Español](prep.es.md) | [Italiano](prep.it.md) | [Русский](prep.ru.md) | [العربية](prep.ar.md)

# API dei plugin per il preprocessore

`PluginPrep.h` espone schemi stabili di token, identificatori, macro, pragma e
flussi di token senza far trapelare i tipi C++ di NeverC o di LLVM. Lo schema
generato `Schema/PluginPrepSchema.inc` è la fonte autorevole per generi numerici
stabili, categorie, grafie e costruibilità.

## Livelli di estensione

Un plugin può intervenire a tre livelli:

- eventi del preprocessore in sola lettura per inclusioni, espansioni di macro,
  condizionali, pragma e transizioni di file;
- intercettori tipizzati per le fasi di token, inclusione, macro, pragma e query
  sulle funzionalità;
- un provider `neverc.prep.build_token_stream` completo che pubblica un
  `TokenStream` verificato.

La fase dei token supporta sostituzione, cancellazione ed espansione limitate.
L'host applica il budget di espansione e verifica grafia, posizione, flag,
collocazione dell'EOF e proprietà dei token prima di pubblicare una sostituzione.

## Costruttori di token

Creare token sintetizzati con `CreateTokenBuilder`, impostare esattamente un
payload di token, assegnare una posizione valida di proprietà del task e chiamare
`TokenBuilderCommit`. Distruggere il costruttore su ogni percorso. Un costruttore
di cui è stato eseguito il commit è immutabile e un commit fallito non pubblica
alcun token.

I flussi di token sono artefatti di task contigui e immutabili. Un flusso
sostitutivo deve contenere esattamente un token EOF finale e non può superare
`NEVERC_PREP_TOKEN_STREAM_MAX_TOKENS`.

## Regole per osservatori e intercettori

Gli osservatori ricevono dati di evento in sola lettura e non possono influenzare
la pre-elaborazione. Gli intercettori seguono il contratto comune di
continuazione:

- chiamare `InvokeNext` al massimo una volta e poi restituire `CONTINUE`; oppure
- non chiamarlo e pubblicare una sostituzione verificata.

Gli oggetti di continuazione e tutti gli handle del preprocessore sono validi
solo entro l'ambito di callback o di task dichiarato. Un thread creato dal plugin
deve essere unito prima che la callback ritorni, se tocca quei valori.

## Verifica

Dopo aver modificato le definizioni dei token, eseguire i controlli sullo schema
generato e sulla copertura:

```sh
python3 utils/plugin-api/gen-prep-schema.py --check
python3 utils/plugin-api/check-coverage.py docs/plugin-api/coverage.json
```

Con `NEVERC_ENABLE_PLUGIN_FUZZERS=ON`,
`plugin-prep-token-builder-fuzzer` sollecita costruttori di token malformati,
handle di task, capacità di output e query sui flussi di token.

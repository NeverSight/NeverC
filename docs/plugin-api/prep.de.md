**Sprachen**: [English](prep.md) | [简体中文](prep.zh-CN.md) | [繁體中文](prep.zh-TW.md) | [日本語](prep.ja.md) | [한국어](prep.ko.md) | [Français](prep.fr.md) | [Deutsch](prep.de.md) | [Español](prep.es.md) | [Italiano](prep.it.md) | [Русский](prep.ru.md) | [العربية](prep.ar.md)

# Präprozessor-Plugin-API

`PluginPrep.h` stellt stabile Schemata für Token, Bezeichner, Makros, Pragmas und
Token-Ströme bereit, ohne C++-Typen von NeverC oder LLVM nach außen dringen zu
lassen. Das generierte Schema `Schema/PluginPrepSchema.inc` ist die maßgebliche
Quelle für stabile numerische Arten, Kategorien, Schreibweisen und
Konstruierbarkeit.

## Erweiterungsebenen

Plugins können auf drei Ebenen mitwirken:

- Nur-Lese-Präprozessorereignisse für Includes, Makroexpansionen, Bedingungen,
  Pragmas und Dateiübergänge;
- typisierte Interceptors für die Phasen Token, Include, Makro, Pragma und
  Feature-Abfrage;
- ein vollständiger `neverc.prep.build_token_stream`-Provider, der einen
  verifizierten `TokenStream` veröffentlicht.

Die Token-Phase unterstützt begrenztes Ersetzen, Löschen und Expandieren. Der
Host setzt das Expansionsbudget durch und prüft Schreibweise, Position, Flags,
EOF-Platzierung und Token-Eigentümerschaft, bevor er einen Ersatz veröffentlicht.

## Token-Builder

Erzeugen Sie synthetisierte Token mit `CreateTokenBuilder`, setzen Sie genau eine
Token-Nutzlast, weisen Sie eine gültige, der Task gehörende Position zu und rufen
Sie `TokenBuilderCommit` auf. Zerstören Sie den Builder auf jedem Pfad. Ein
committeter Builder ist unveränderlich, und ein fehlgeschlagener Commit
veröffentlicht kein Token.

Token-Ströme sind zusammenhängende, unveränderliche Task-Artefakte. Ein
Ersatzstrom muss genau ein abschließendes EOF-Token enthalten und darf
`NEVERC_PREP_TOKEN_STREAM_MAX_TOKENS` nicht überschreiten.

## Regeln für Observer und Interceptors

Observer erhalten schreibgeschützte Ereignisdaten und können die Vorverarbeitung
nicht beeinflussen. Interceptors folgen dem gemeinsamen Continuation-Vertrag:

- `InvokeNext` höchstens einmal aufrufen und dann `CONTINUE` zurückgeben; oder
- es nicht aufrufen und einen verifizierten Ersatz veröffentlichen.

Continuation-Objekte und alle Präprozessor-Handles sind nur innerhalb ihres
deklarierten Callback- bzw. Task-Bereichs gültig. Ein vom Plugin erzeugter Thread
muss vor der Rückkehr des Callbacks beigetreten werden, wenn er diese Werte
berührt.

## Verifikation

Führen Sie nach Änderungen an Token-Definitionen die Prüfungen für generiertes
Schema und Abdeckung aus:

```sh
python3 utils/plugin-api/gen-prep-schema.py --check
python3 utils/plugin-api/check-coverage.py docs/plugin-api/coverage.json
```

Mit `NEVERC_ENABLE_PLUGIN_FUZZERS=ON` prüft
`plugin-prep-token-builder-fuzzer` fehlerhafte Token-Builder, Task-Handles,
Ausgabekapazitäten und Token-Strom-Abfragen.

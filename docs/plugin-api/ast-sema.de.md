**Sprachen**: [English](ast-sema.md) | [简体中文](ast-sema.zh-CN.md) | [繁體中文](ast-sema.zh-TW.md) | [日本語](ast-sema.ja.md) | [한국어](ast-sema.ko.md) | [Français](ast-sema.fr.md) | [Deutsch](ast-sema.de.md) | [Español](ast-sema.es.md) | [Italiano](ast-sema.it.md) | [Русский](ast-sema.ru.md) | [العربية](ast-sema.ar.md)

# Plugin-APIs für AST, Parser und Semantik

`PluginAST.h` und `PluginSema.h` bieten task-begrenzten Zugriff in reinem C auf
den Frontend-Baum und die semantische Pipeline. Stabile IDs für Knoten,
Eigenschaften und Kind-Slots werden aus den konkreten AST-Definitionen von NeverC
generiert; ein Plugin erhält niemals einen C++-Zeiger auf `Decl`, `Stmt`, `Type`
oder `Sema`.

## AST-Knoten lesen und erzeugen

Verwenden Sie `NevercASTAPI`, um Knoteninformationen, Schema-Eigenschaften,
Kinder, Eltern, Deklarationskontexte, Typen, Attribute und Details gängiger
konkreter Knoten abzufragen. Batch-APIs verlangen eine explizite Elementanzahl,
Kapazität und Schrittweite.

`NevercASTBuilder` konstruiert ausschließlich im Schema deklarierte Knotenarten.
Erforderliche Eigenschaften und Kind-Slots werden beim Commit geprüft. Ein
erfolgreicher Commit veröffentlicht einen Knoten im Besitz der Task; ein
fehlgeschlagener Commit hinterlässt keinen teilweise sichtbaren Knoten. Zerstören
Sie jeden Builder nach Commit oder Fehlschlag.

## Atomare Mutation

AST-Änderungen verwenden `BeginASTMutation`, vorgemerkte Operationen und
`CommitASTMutation`. Der Host prüft Eigentümerschaft, Slot-Kompatibilität,
Kardinalität, Elternverknüpfungen, Zyklen und semantische Invarianten, bevor er
den Baum ändert. `AbortASTMutation` verwirft alle vorgemerkten Operationen.
Native `TreeMutationListener`-Benachrichtigungen werden nur nach einem
erfolgreichen Commit gesendet.

Das baubare Beispiel
[`ASTRewritePlugin.c`](../../pluginsdk/examples/ASTRewritePlugin.c) zeigt einen
Parser-Interceptor, der den eingebauten Parser aufruft, ein Integer-Literal
erzeugt und den Initialisierer einer Variablen atomar ersetzt.

## Parser- und Sema-Ersatz

`neverc.syntax.parse` bildet einen verifizierten Token-Strom auf eine `ASTUnit`
ab. `neverc.sema.analyze` bildet ein AST-Produkt auf eine `SemanticUnit` ab.
Beide Phasen besitzen typisierte Interceptors und Provider. Die feingranularen
Erweiterungsphasen für Deklaration, Anweisung, Ausdruck, Typname, Attribut,
Namenssuche, Konvertierung und Schlüsselwort bleiben verfügbar, wenn nur ein Teil
des Frontends ersetzt wird.

Der eingebaute verschmolzene Parser/Sema-Pfad veröffentlicht dieselben
Artefaktverträge wie ein Ersatz. Die semantische Wiedergabe akzeptiert nur
Knotenarten, für die NeverC Gültigkeitsbereich, Namenssuche, Redeklaration und
Typprüfungszustand rekonstruieren kann. Trifft sie auf eine nicht unterstützte
konkrete Art, gibt sie `NEVERC_STATUS_UNSUPPORTED_AST_KIND` zurück; ein nur
teilweise wiedergegebener Baum wird niemals als semantisch vollständig markiert.

## Lebenszyklus und Aufräumen

Lebenszyklus-Observer für AST und Sema werden über die `TreeConsumer`-Brücke des
Hosts in Quellreihenfolge zugestellt. Begin/End-Ereignisse bleiben auch bei
Syntaxfehlern, Plugin-Fehlern und Abbrüchen paarweise. Task-Handles werden erst
ungültig, nachdem die abschließenden Nur-Lese-End-Ereignisse und die
Aufräum-Callbacks ausgeführt wurden.

## Verifikation

```sh
python3 utils/plugin-api/gen-ast-schema.py --check
ctest --test-dir build-neverc \
  -R 'Plugin(AST|Parser|Sema|Frontend)' --output-on-failure
```

Mit `NEVERC_ENABLE_PLUGIN_FUZZERS=ON` deckt `plugin-ast-mutation-fuzzer` die
Dekodierung von Eigenschaften, fehlerhafte Builder, gefälschte Handles und das
Zurückrollen von Mutationen ab.

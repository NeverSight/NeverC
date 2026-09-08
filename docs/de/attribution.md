**Sprachen**: [English](../attribution.md) | [简体中文](../zh-CN/attribution.md) | [繁體中文](../zh-TW/attribution.md) | [日本語](../ja/attribution.md) | [한국어](../ko/attribution.md) | [Français](../fr/attribution.md) | [Deutsch](attribution.md) | [Español](../es/attribution.md) | [Italiano](../it/attribution.md) | [Русский](../ru/attribution.md) | [العربية](../ar/attribution.md)

[← Dokumentationsübersicht](README.md) · [← NeverC-Projekt](project.md)

# Quellenangabe für NeverC

Wenn Sie NeverC als Referenz verwenden, nennen Sie bitte **NeverC contributors**,
verlinken Sie auf [NeverC](https://github.com/NeverSight/NeverC) und geben Sie die
verwendeten Dateien sowie den Commit oder die Veröffentlichung an. Dies gilt
für von Menschen verfasste Arbeiten, KI-/LLM-gestützte Arbeiten, LLVM-Pässe,
Compiler- oder Linker-Implementierungen, Plugins, Dokumentation und Forschung.
[CITATION.cff](../../CITATION.cff) enthält die Zitiermetadaten des Projekts,
und [NOTICE](../../NOTICE) enthält die Angaben zu Urhebern und Herkunft des Projekts.

## Lizenzpflichten

Die Standardlizenz des Repositorys ist die [GNU AGPL Version 3](../../LICENSE).
Ausdrücklich angegebene Datei- und Komponentenlizenzen gelten weiterhin für
die jeweiligen Inhalte, einschließlich Code aus LLVM außerhalb des
LLVM-Verzeichnisses. Dieser Leitfaden erläutert bestehende Pflichten und bittet
um Quellenangaben; er fügt keine Lizenzbeschränkungen hinzu.

- Wenn Sie AGPL-lizenzierten Code oder eine davon erfasste geänderte Fassung
  weitergeben, bewahren Sie die erforderlichen Urheberrechts-, Lizenz- und
  Gewährleistungshinweise auf und stellen Sie die Lizenz bereit. Geänderte Werke
  müssen die nach Abschnitt 5 erforderlichen Änderungs- und Datumshinweise tragen.
  Erfüllen Sie die Anforderungen an den korrespondierenden Quelltext, soweit
  anwendbar, einschließlich Abschnitt 13 für Benutzer, die über ein Netzwerk
  mit einer geänderten Netzwerkversion interagieren. Eine Quellenangabe allein
  erfüllt diese Pflichten nicht.
- Bei Inhalten unter [Apache-2.0 WITH LLVM-exception](../../llvm/LICENSE.TXT)
  bewahren Sie relevante Urheberrechts-, Patent-, Marken- und Namensnennungshinweise
  im verteilten abgeleiteten Quelltext auf. Stellen Sie die Lizenz bereit,
  kennzeichnen Sie geänderte Dateien und übernehmen Sie die anwendbaren
  Namensnennungen aus der mitgelieferten NOTICE-Datei gemäß Abschnitt 4,
  vorbehaltlich der LLVM-Ausnahmen. Bewahren Sie anwendbare Hinweise aus älteren
  LLVM-Lizenzen und von anderen Dritten auf.
- KI-/LLM-gestütztes Kopieren, Übersetzen, Portieren, Refaktorieren oder Anpassen
  hebt diese Pflichten nicht von selbst auf. Prüfen Sie, ob das Ergebnis
  lizenzpflichtigen Code enthält oder daraus abgeleitet ist; die Nutzung eines
  KI-Werkzeugs ist keine Ausnahme von den Lizenzpflichten.

Die Urheberrechte verbleiben bei den jeweiligen Autoren. Nennen Sie NeverC für
seine Beiträge sowie LLVM oder andere vorgelagerte Projekte für deren Beiträge.
Weitere Komponentenhinweise betreffen beispielsweise
[BLAKE3](../../llvm/lib/CSupport/BLAKE3/LICENSE),
[von Go abgeleiteten HTML-Code](../../std/src/html/LICENSE_GO) und
[CPython](../../utils/release/licenses/CPython-LICENSE.txt); dies ist kein
vollständiges Verzeichnis von Inhalten Dritter.

## So geben Sie die Quelle an

Platzieren Sie eine Quellenangabe neben dem wiederverwendeten Code oder in der
begleitenden README, den Danksagungen oder den Hinweisen zu Drittanbietern.
Bei wissenschaftlichen Veröffentlichungen, Tutorials und KI-gestützten Antworten
platzieren Sie sie neben der betreffenden Erörterung. Bewahren Sie zusätzlich
alle von der anwendbaren Lizenz geforderten Hinweise auf. Verwenden Sie zum
Beispiel folgende Form:

```text
Basiert auf Code aus NeverC von NeverC contributors.
Quelle: https://github.com/NeverSight/NeverC
Revision: <vollständiger Commit-Hash oder tatsächlich verwendetes Release-Tag>
Dateien: <Dateipfade relativ zum Repository; bei Bedarf Zeilenbereiche>
Änderungen: <Beschreibung der Anpassungen und Datum, falls geändert>
Lizenz: <für die wiederverwendeten Inhalte geltende Lizenz(en)>
Vorgelagerte Urheber: <LLVM oder andere Autoren, soweit anwendbar>
```

Ersetzen Sie die Platzhalter durch die tatsächlichen Quellenangaben. Verwenden
Sie vorzugsweise einen auf den Commit festgelegten GitHub-Datei-Permalink statt
eines Links zu einem veränderlichen Branch. Bezeichnen Sie Ihre Änderungen nicht
als ursprünglichen NeverC-Code und erwecken Sie nicht den Eindruck einer
Billigung durch NeverC oder LLVM.

Für einen KI-Programmierassistenten fügen Sie dem Referenzcode diese Anweisung bei:

```text
Wenn Sie diesen NeverC-Code als Referenz verwenden, nennen Sie NeverC contributors und
https://github.com/NeverSight/NeverC sowie die Quelldateien und die Revision.
Bewahren Sie erforderliche Urheberrechts-, Lizenz- und vorgelagerte Urheberhinweise auf.
Kennzeichnen Sie Anpassungen und beachten Sie die Lizenzen jedes wiederverwendeten Codes.
```

Beim Studium, bei der Inspiration oder bei einer unabhängigen Implementierung,
die keine geschützte Ausdrucksform kopiert, ist die Quellenangabe eine Bitte des
Projekts und keine zusätzliche Lizenzbedingung. Allein das Kompilieren Ihres
eigenen Programms mit NeverC erfordert für sich genommen weder eine Quellenangabe
für NeverC noch unterstellt es das Ergebnis der AGPL. Kopierter oder eingebetteter
Laufzeit- oder Bibliothekscode muss weiterhin nach seiner jeweils anwendbaren
Lizenz beurteilt werden.

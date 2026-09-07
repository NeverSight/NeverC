**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Indice della documentazione](../README.it.md) · [← Progetto NeverC](../i18n/README.it.md)

# Attribuzione delle fonti di NeverC

Quando usi NeverC come riferimento, indica **NeverC contributors**, inserisci un
collegamento a [NeverC](https://github.com/NeverSight/NeverC) e identifica i file e
il commit o la versione utilizzati. Questo vale per lavori scritti da persone,
lavori assistiti da IA/LLM, pass LLVM, implementazioni di compilatori o linker,
plugin, documentazione e ricerca. [CITATION.cff](../../CITATION.cff) fornisce i
metadati per citare il progetto e [NOTICE](../../NOTICE) ne indica attribuzione e
provenienza.

## Obblighi di licenza

La licenza predefinita del repository è la [GNU AGPL versione 3](../../LICENSE).
Le licenze esplicite di file e componenti continuano a disciplinare il rispettivo
materiale, incluso il codice proveniente da LLVM al di fuori della directory LLVM.
Questa guida spiega gli obblighi esistenti e richiede citazioni; non aggiunge
restrizioni di licenza.

- Quando trasmetti codice coperto dalla AGPL o una sua versione modificata coperta
  dalla licenza, conserva gli avvisi richiesti relativi a copyright, licenza e
  garanzia e fornisci la licenza. Le opere modificate devono riportare gli avvisi
  sulle modifiche e sulle relative date richiesti dalla sezione 5. Rispetta gli
  obblighi relativi al codice sorgente corrispondente quando applicabili, inclusa
  la sezione 13 per gli utenti che interagiscono da remoto con una versione
  modificata accessibile tramite rete. Una sola citazione non soddisfa questi
  obblighi.
- Per il materiale sotto [Apache-2.0 WITH LLVM-exception](../../llvm/LICENSE.TXT),
  conserva gli avvisi pertinenti relativi a copyright, brevetti, marchi e
  attribuzione nel codice sorgente derivato distribuito. Fornisci la licenza,
  contrassegna i file modificati e riporta le attribuzioni applicabili contenute
  nei file NOTICE forniti, come richiesto dalla sezione 4, fatte salve le eccezioni
  LLVM. Conserva gli avvisi applicabili delle precedenti licenze LLVM e delle
  altre terze parti.
- La copia, traduzione, conversione ad altre piattaforme, ristrutturazione o
  adattamento assistiti da IA/LLM non eliminano di per sé questi obblighi. Valuta
  se il risultato contenga codice coperto dalla licenza o ne derivi; l'uso di uno
  strumento di IA non costituisce un'esenzione dalla licenza.

I diritti d'autore restano ai rispettivi autori. Attribuisci a NeverC i suoi
contributi e a LLVM o agli altri progetti di origine i loro. Per esempio, altri
avvisi sui componenti includono [BLAKE3](../../llvm/lib/CSupport/BLAKE3/LICENSE),
[codice HTML derivato da Go](../../std/src/html/LICENSE_GO) e
[CPython](../../utils/release/licenses/CPython-LICENSE.txt); questo non è un
inventario completo del materiale di terze parti.

## Come citare la fonte

Inserisci una citazione accanto al codice riutilizzato oppure nel README, nei
ringraziamenti o negli avvisi di terze parti che lo accompagnano. Per articoli,
tutorial e risposte assistite da IA, inseriscila accanto alla discussione
pertinente. Conserva anche gli eventuali avvisi richiesti dalla licenza
applicabile. Usa una forma come questa:

```text
Basato su codice di NeverC, di NeverC contributors.
Fonte: https://github.com/NeverSight/NeverC
Revisione: <hash completo del commit o tag della versione effettivamente utilizzata>
File: <percorsi dei file relativi al repository; intervalli di righe se utili>
Modifiche: <descrizione degli adattamenti e data, se modificato>
Licenza: <licenza o licenze applicabili al materiale riutilizzato>
Attribuzioni ai progetti di origine: <LLVM o altri autori, se applicabile>
```

Sostituisci i segnaposto con i dettagli effettivi della fonte. Preferisci un
collegamento permanente al file su GitHub vincolato al commit rispetto a un
collegamento a un ramo che può cambiare. Non presentare le tue modifiche come
codice originale del progetto NeverC né lasciar intendere che NeverC o LLVM le
approvino.

Per un assistente di programmazione basato su IA, includi questa istruzione con
il codice di riferimento:

```text
Quando usi questo codice di NeverC come riferimento, cita NeverC contributors e
https://github.com/NeverSight/NeverC, indicando i file sorgente e la revisione.
Conserva gli avvisi richiesti relativi a copyright, licenza e attribuzione ai progetti di origine.
Identifica gli adattamenti e rispetta le licenze di tutto il codice riutilizzato.
```

Per lo studio, l'ispirazione o un'implementazione indipendente che non copi forme
espressive protette, la citazione è una richiesta del progetto, non una
condizione aggiuntiva della licenza. Il semplice fatto di compilare il proprio
programma con NeverC non richiede di per sé una citazione di NeverC né rende il
risultato soggetto alla AGPL. Il codice di runtime o di libreria copiato o
incorporato deve comunque essere valutato in base alla propria licenza
applicabile.

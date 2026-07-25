**Lingue**: [English](ast-sema.md) | [简体中文](ast-sema.zh-CN.md) | [繁體中文](ast-sema.zh-TW.md) | [日本語](ast-sema.ja.md) | [한국어](ast-sema.ko.md) | [Français](ast-sema.fr.md) | [Deutsch](ast-sema.de.md) | [Español](ast-sema.es.md) | [Italiano](ast-sema.it.md) | [Русский](ast-sema.ru.md) | [العربية](ast-sema.ar.md)

# API dei plugin per AST, parser e semantica

`PluginAST.h` e `PluginSema.h` forniscono un accesso in C puro, limitato al task,
all'albero del frontend e alla pipeline semantica. Gli ID stabili di nodi,
proprietà e slot figli sono generati dalle definizioni concrete dell'AST di
NeverC; un plugin non riceve mai un puntatore C++ a `Decl`, `Stmt`, `Type` o
`Sema`.

## Leggere e costruire nodi dell'AST

Usare `NevercASTAPI` per interrogare informazioni sui nodi, proprietà di schema,
figli, genitori, contesti di dichiarazione, tipi, attributi e dettagli dei nodi
concreti più comuni. Le API batch richiedono numero di elementi, capacità e passo
espliciti.

`NevercASTBuilder` costruisce soltanto i generi di nodo dichiarati nello schema.
Le proprietà e gli slot figli obbligatori sono verificati al commit. Un commit
riuscito pubblica un nodo di proprietà del task; un commit fallito non lascia
alcun nodo parzialmente visibile. Distruggere ogni costruttore dopo il commit o
il fallimento.

## Mutazione atomica

Le modifiche all'AST usano `BeginASTMutation`, operazioni in stage e
`CommitASTMutation`. L'host convalida proprietà, compatibilità degli slot,
cardinalità, collegamenti al genitore, cicli e invarianti semantiche prima di
modificare l'albero. `AbortASTMutation` scarta tutte le operazioni in stage. Le
notifiche native di `TreeMutationListener` vengono inviate solo dopo un commit
riuscito.

L'esempio compilabile
[`ASTRewritePlugin.c`](../../pluginsdk/examples/ASTRewritePlugin.c) mostra un
intercettore del parser che chiama il parser integrato, costruisce un letterale
intero e sostituisce atomicamente l'inizializzatore di una variabile.

## Sostituzione di parser e Sema

`neverc.syntax.parse` mappa un flusso di token verificato su un `ASTUnit`.
`neverc.sema.analyze` mappa un prodotto dell'AST su un `SemanticUnit`. Entrambe le
fasi dispongono di intercettori tipizzati e provider. Le fasi di estensione a
grana fine — dichiarazione, istruzione, espressione, nome di tipo, attributo,
lookup, conversione e parola chiave — restano disponibili quando si sostituisce
solo una parte del frontend.

Il percorso integrato fuso parser/Sema pubblica gli stessi contratti di artefatto
di una sostituzione. Il replay semantico accetta solo i generi di nodo per i quali
NeverC può ricostruire scope, lookup, ridichiarazione e stato del controllo dei
tipi. Incontrare un genere concreto non supportato restituisce
`NEVERC_STATUS_UNSUPPORTED_AST_KIND`; un albero riprodotto solo in parte non viene
mai contrassegnato come semanticamente completo.

## Ciclo di vita e pulizia

Gli osservatori del ciclo di vita di AST e Sema vengono consegnati nell'ordine del
sorgente tramite il ponte `TreeConsumer` dell'host. Gli eventi di inizio e fine
restano accoppiati in caso di errori di sintassi, errori del plugin e
annullamenti. Gli handle di task diventano invalidi solo dopo l'esecuzione degli
eventi finali di sola lettura e delle callback di pulizia.

## Verifica

```sh
python3 utils/plugin-api/gen-ast-schema.py --check
ctest --test-dir build-neverc \
  -R 'Plugin(AST|Parser|Sema|Frontend)' --output-on-failure
```

Con `NEVERC_ENABLE_PLUGIN_FUZZERS=ON`, `plugin-ast-mutation-fuzzer` copre la
decodifica delle proprietà, i costruttori malformati, gli handle contraffatti e il
rollback delle mutazioni.

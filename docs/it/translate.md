**Lingue**: [English](../translate.md) | [简体中文](../zh-CN/translate.md) | [繁體中文](../zh-TW/translate.md) | [日本語](../ja/translate.md) | [한국어](../ko/translate.md) | [Français](../fr/translate.md) | [Deutsch](../de/translate.md) | [Español](../es/translate.md) | [Italiano](translate.md) | [Русский](../ru/translate.md) | [العربية](../ar/translate.md)

[← Documentazione](README.md)

# Tradurre C++ in NeverC

Il comando sperimentale `neverc translate` genera sorgenti `.nc` verificabili con `cpp-core-v1`, `cpp-core-v2`, `cpp-project-v1` e `cpp-math-v1`.

**Attualmente è implementata solo la traduzione di codice C++.** Il supporto per E Language (易语言, `.e`), Python, Go, Rust, TypeScript e JavaScript è previsto per il futuro; i relativi traduttori non sono ancora disponibili.

## Installazione e traduzione scalare

Usare una normale installazione di NeverC con le risorse standard. Il frontend C++ e gli header SDK approvati sono integrati; non occorre installare Clang separatamente. Vedere le [note di compilazione del frontend](../../utils/translate-frontends/cpp/README.md).

```sh
neverc translate --from cpp input.cpp -o output.nc
neverc output.nc -c -o output.o
```

Il profilo `cpp-core-v1` accetta un singolo file C++17 autonomo senza include. Supporta `int`, `unsigned int`, `bool`, `void`, tipi aggregati semplici, funzioni non membro, spazi dei nomi, sovraccarichi e il flusso di controllo documentato. Ogni dichiarazione in ingresso viene controllata, incluso il codice inutilizzato.

Selezionare `--profile cpp-core-v2` per aggiungere alias `typedef`/`using` verificati, enumerazioni con tipi interi sottostanti supportati, `static_assert`, puntatori a oggetti e riferimenti lvalue entro un ambito limitato. Parametri e risultati per riferimento mantengono gli alias; sono supportati puntatori nulli e qualificazioni `const` annidate. Restano i vincoli di un solo file sorgente e nessun include. Vedere il [contratto core v2](../../utils/translate-frontends/docs/cpp-core-v2.md).

Core v2 aggiunge array locali di dimensione fissa e campi array, indicizzazione multidimensionale e puntatori o riferimenti ad array. L’inizializzazione parziale azzera gli elementi restanti e mantiene l’ordine e gli alias. Dimensioni ed espansione dell’inizializzazione sono limitate. Restano esclusi gli array globali, quelli di lunghezza variabile e gli elementi con gestione della durata non banale.

Core v2 supporta `switch`/`case`/`default`, con istruzioni di inizializzazione C++17, passaggio al caso successivo e annotazioni `[[fallthrough]]` convalidate. Il selettore viene valutato una sola volta; switch e cicli annidati mantengono le destinazioni di `break`/`continue`. Restano esclusi gli intervalli case GNU e altri attributi di istruzione.

Core v2 supporta interi con e senza segno a 8, 16, 32 e 64 bit, tipi e letterali carattere e interrogazioni costanti `sizeof`/`alignof`. Promozioni e risoluzione degli overload precedono la normalizzazione; `long`, `wchar_t` e il tipo delle dimensioni seguono la piattaforma. Anche le enumerazioni ammettono tipi sottostanti più stretti o più ampi. Stringhe a runtime e STL sono ancora in sviluppo.

Core v2 supporta anche spostamenti e differenze tra puntatori a oggetti, incrementi/decrementi e assegnazioni composte, inclusi attraversamenti di array e passi multidimensionali. Le funzioni ausiliarie generate preservano le regole C++17 per un puntatore nullo più o meno zero e per la differenza tra due puntatori nulli. Restano esclusi i confronti d’ordine tra puntatori, le conversioni puntatore/intero e i contenitori e algoritmi STL.

Core v2 confronta dimensioni e allineamenti ABI dei tipi sorgente con il modello di destinazione di NeverC, comprese le dimensioni delle strutture e gli offset dei campi. Le asserzioni statiche nel codice generato verificano nuovamente la disposizione in compilazione; il manifest ne registra i dati.

## Progetti con più file

Selezionare esplicitamente le unità di traduzione da un database di compilazione e specificare la directory radice del progetto. Il frontend integrato analizza ogni unità separatamente; la fusione verifica definizioni, collegamento, tipi condivisi e il rispetto della regola di definizione unica (ODR) mediante verifiche conservative.

```sh
neverc translate --from cpp --profile cpp-project-v1 \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/a.cpp src/b.cpp --out-dir generated
```

L’output del progetto comprende `translated.nc` e `translated.h`. Sono ammessi solo gli header del progetto contenuti in tale directory. Se un file ha più configurazioni, selezionare l’indice a base zero con `--compdb-entry src/a.cpp=4`. I comandi del compilatore memorizzati vengono analizzati come dati e mai eseguiti.

## Matematica limitata a doppia precisione

`cpp-math-v1` aggiunge ai progetti `double`, le conversioni/i confronti documentati ed esattamente `std::fabs(double)` e `std::floor(double)`. Usa gli header integrati di Clang 20.1.8 / libc++ 200100 / macOS 15.5 e richiede un target macOS 15.0 esplicito (arm64 o x86_64). L’aritmetica generale in virgola mobile resta esclusa. Le eccezioni devono essere mascherate e l’azzeramento dei subnormali disattivato; sono testate tutte e quattro le modalità standard di arrotondamento.

```sh
neverc translate --from cpp --profile cpp-math-v1 \
  --target arm64-apple-macosx15.0.0 \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/math.cpp --out-dir generated-math
```

La traduzione matematica verifica anche gli header NeverC installati, l’identità delle implementazioni incorporate e un collegamento effettivo. I moduli generati usano il runtime NeverC. Con `-fno-builtin-std`, la traduzione fallisce prima di scrivere l’output solo se il codice finale richiede le mappature `fabs`/`floor`. Il codice matematico che non le richiede può comunque essere tradotto.

## Convalida e output

Usare `--check` al posto di un’opzione di output per eseguire la stessa analisi, generazione e convalida sintattica e degli oggetti senza conservare i file generati. `--report PATH` scrive diagnostica strutturata. La traduzione non esegue mai il programma sorgente.

Output e file accessori non vengono sovrascritti. `--out-dir` richiede una nuova directory con una directory padre esistente; `-o` richiede un nuovo percorso `.nc`. Il manifesto registra requisiti del target, hash di input e output e procedura di compilazione; la mappa sorgente collega le righe generate alle posizioni originali.

I risultati CI, le verifiche di esecuzione e installazione e i test saltati sono documentati per piattaforma. macOS arm64 nativo e macOS x86_64 tramite Rosetta restano ambienti di convalida distinti. L’ambito dichiarato non comprende il supporto completo di C++/STL, né eccezioni, template, stringhe o `std::vector`. Consultare la [matrice di supporto](../../utils/translate-frontends/docs/support-matrix.md), il [protocollo e le regole di ripristino](../../utils/translate-frontends/docs/protocol.md) e il [progetto di esempio](../../tests/neverc/Inputs/translate/cpp/project).

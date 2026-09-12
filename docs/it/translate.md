**Lingue**: [English](../translate.md) | [简体中文](../zh-CN/translate.md) | [繁體中文](../zh-TW/translate.md) | [日本語](../ja/translate.md) | [한국어](../ko/translate.md) | [Français](../fr/translate.md) | [Deutsch](../de/translate.md) | [Español](../es/translate.md) | [Italiano](translate.md) | [Русский](../ru/translate.md) | [العربية](../ar/translate.md)

[← Documentazione](README.md)

# Tradurre C++ in NeverC

Il comando sperimentale `neverc translate` genera sorgenti `.nc` verificabili con `cpp-core-v1`, `cpp-core-v2`, `cpp-project-v1` e `cpp-math-v1`.

**Attualmente è implementata solo la traduzione di codice C++.** Il supporto per E Language (易语言, `.e`), Python, Go, Rust, TypeScript e JavaScript è previsto per il futuro; i relativi traduttori non sono ancora disponibili.

Core v2 supporta variabili globali modificabili di tipo intero, booleano ed enumerazione nell’ambito di un namespace, con inizializzazione a zero o mediante una costante completamente verificata. Memoria e indirizzo persistono tra le chiamate; riferimenti, puntatori e argomenti predefiniti accedono alla stessa variabile. Una dichiarazione `extern` richiede una definizione nella stessa unità sorgente. Le globali const restano di sola lettura. Restano le restrizioni specifiche per inizializzazione dinamica, record/array/puntatori/riferimenti globali, memoria locale al thread e variabili locali statiche.

Core v2 supporta i cicli `for` su intervalli di C++17 per gli array fissi ammessi e gli intervalli definiti nel sorgente, con chiamate risolte a `begin/end` tramite membri o ADL. Variabili per valore o riferimento, iteratori di tipo record e sentinelle di tipo diverso mantengono le normali regole di chiamata e durata. L’intervallo e `begin/end` vengono inizializzati una sola volta; gli oggetti di ogni iterazione vengono distrutti prima dell’incremento o dell’uscita, anche con `continue`, `break` e `return`. Template, header standard, contenitori STL, binding strutturati e istruzioni di inizializzazione C++20 restano fuori da questa estensione. La verifica nativa richiede il CI della revisione che la implementa.

Core v2 supporta membri dati privati e protetti nelle classi a layout standard che rispettano le altre restrizioni. Clang integrato controlla gli accessi prima della traduzione; metodi, costruttori, factory, argomenti predefiniti e operazioni di copia/spostamento autorizzati usano la stessa memoria tipizzata dei membri. Gli accessi esterni non consentiti restano diagnostici C++. Gli specificatori di accesso sono regole del linguaggio sorgente, senza garanzie di segretezza durante l’esecuzione. Restano le restrizioni per classi senza layout standard con accessi misti, friend template, ereditarietà e tipi di campo non supportati. I risultati nativi richiedono il CI della revisione che li implementa.

Core v2 supporta funzioni friend non template e tipi friend risolti nelle classi ammesse. ADL dei friend nascosti, operatori, argomenti predefiniti, definizioni nei namespace, classi friend e membri friend specifici mantengono i controlli di accesso di Clang e le normali chiamate tipizzate. Le ridichiarazioni conservano una sola identità della funzione. Le forme friend non supportate o dipendenti e i friend template restano rifiutati; tutti i corpi delle funzioni friend vengono verificati. I risultati nativi richiedono il CI della revisione che li implementa.

Core v2 supporta record annidati con nome e non template, compresi tipi privati o protetti esposti tramite alias o factory consentiti. Ogni oggetto conserva memoria e ricevitore propri; riferimenti espliciti all’oggetto esterno, identità di tipo per ambito, copia e spostamento di membri e array, distruzione e iteratori annidati mantengono la normale semantica. Le dipendenze per valore sono emesse per prime. Record annidati anonimi, template, ereditarietà e STL completa restano da implementare; la verifica nativa richiede il CI della revisione corrispondente.

Core v2 supporta membri statici definiti di tipo intero, booleano o enumerazione, incluse definizioni inline/constexpr e fuori classe con inizializzazione costante o a zero. Tutte le istanze condividono la stessa memoria tipizzata. Gli effetti del ricevitore e la distruzione dei temporanei sono preservati; i riferimenti ai membri statici sopravvivono ai ricevitori temporanei. Attualmente serve una definizione nella stessa unità sorgente anche per leggere soltanto il valore di una costante non inline. Inizializzazione dinamica, altri tipi statici e STL completa restano incompleti; la verifica nativa richiede il CI della revisione corrispondente.

## Installazione e traduzione scalare

Usare una normale installazione di NeverC con le risorse standard. Il frontend C++ e gli header SDK approvati sono integrati; non occorre installare Clang separatamente. Vedere le [note di compilazione del frontend](../../utils/translate-frontends/cpp/README.md).

```sh
neverc translate --from cpp input.cpp -o output.nc
neverc output.nc -c -o output.o
```

Il profilo `cpp-core-v1` accetta un singolo file C++17 autonomo senza include. Supporta `int`, `unsigned int`, `bool`, `void`, tipi aggregati semplici, funzioni non membro, spazi dei nomi, sovraccarichi e il flusso di controllo documentato. Ogni dichiarazione in ingresso viene controllata, incluso il codice inutilizzato.

Selezionare `--profile cpp-core-v2` per aggiungere alias `typedef`/`using` verificati, enumerazioni con tipi interi sottostanti supportati, `static_assert`, puntatori a oggetti e riferimenti lvalue entro un ambito limitato. Parametri e risultati per riferimento mantengono gli alias; sono supportati puntatori nulli e qualificazioni `const` annidate. Restano i vincoli di un solo file sorgente e nessun include. Vedere il [contratto core v2](../../utils/translate-frontends/docs/cpp-core-v2.md).

Core v2 aggiunge array locali di dimensione fissa e campi array, indicizzazione multidimensionale e puntatori o riferimenti ad array. L’inizializzazione parziale azzera gli elementi scalari omessi; i record seguono l’inizializzazione selezionata e mantiene l’ordine e gli alias. Dimensioni ed espansione dell’inizializzazione sono limitate. Restano esclusi gli array globali e quelli di lunghezza variabile.

Core v2 supporta `switch`/`case`/`default`, con istruzioni di inizializzazione C++17, passaggio al caso successivo e annotazioni `[[fallthrough]]` convalidate. Il selettore viene valutato una sola volta; switch e cicli annidati mantengono le destinazioni di `break`/`continue`. Restano esclusi gli intervalli case GNU e altri attributi di istruzione.

Core v2 supporta interi con e senza segno a 8, 16, 32 e 64 bit, tipi e letterali carattere e interrogazioni costanti `sizeof`/`alignof`. Promozioni e risoluzione degli overload precedono la normalizzazione; `long`, `wchar_t` e il tipo delle dimensioni seguono la piattaforma. Anche le enumerazioni ammettono tipi sottostanti più stretti o più ampi. Stringhe a runtime e STL sono ancora in sviluppo.

Core v2 supporta anche spostamenti e differenze tra puntatori a oggetti, incrementi/decrementi e assegnazioni composte, inclusi attraversamenti di array e passi multidimensionali. Le funzioni ausiliarie generate preservano le regole C++17 per un puntatore nullo più o meno zero e per la differenza tra due puntatori nulli. Restano esclusi i confronti d’ordine tra puntatori, le conversioni puntatore/intero e i contenitori e algoritmi STL.

Core v2 confronta dimensioni e allineamenti ABI dei tipi sorgente con il modello di destinazione di NeverC, comprese le dimensioni delle strutture e gli offset dei campi. Le asserzioni statiche nel codice generato verificano nuovamente la disposizione in compilazione; il manifest ne registra i dati.

Core v2 supporta le funzioni membro nominate non virtuali degli tipi record ammessi, inclusi overload const, metodi qualificati lvalue, metodi statici e `this`. Le chiamate preservano l’identità dell’oggetto originale e valutano il ricevente prima degli argomenti.

Core v2 supporta costruttori ordinari definiti dall’utente per record con layout standard e operazioni di copia supportate. Oggetti locali, campi ed elementi di array vengono costruiti nella memoria finale; i campi seguono l’ordine di dichiarazione. Restano esclusi i costruttori deleganti e le eccezioni.

Core v2 crea un oggetto distinto per ogni parametro record passato per valore e scrive il risultato nella destinazione del chiamante. Costruttori e metodi seguono le stesse regole; le copie richieste e gli alias dei riferimenti sono conservati. Per i tipi triviali idonei, altre implementazioni C++17 possono aggiungere copie di argomenti o risultati.

Core v2 supporta distruttori ordinari definiti dall’utente e distruzione implicita dei membri nelle uscite normali. Oggetti locali, campi ed elementi di array vengono distrutti in ordine inverso; i temporanei al termine dell’espressione completa, dopo aver salvato i valori necessari. Ritorni, rami, cicli, break e continue eseguono la pulizia prevista. I parametri per valore vengono distrutti all’uscita dalla funzione chiamata; gli oggetti restituiti appartengono al chiamante. Chiamate esplicite ai distruttori, distruzione statica e unwinding delle eccezioni restano esclusi.

Core v2 esegue costruttori di copia e operatori di assegnazione per copia ordinari definiti dall’utente, con parametro sorgente `R&` o `const R&`. La copia usa la destinazione effettiva e conserva gli effetti collaterali e il riferimento restituito. La sintassi di assegnazione valuta prima l’operando destro; una chiamata esplicita a `operator=` valuta prima il ricevente.

Core v2 supporta anche costruttori predefiniti generati o dichiarati default e distruttori default, inclusi membri record e array annidati. La costruzione usa la destinazione effettiva e l’ordine di dichiarazione; l’inizializzazione per valore azzera solo quando richiesto da C++. Restano valide le diverse regole per default definito fuori dalla classe. I costruttori inutilizzati o presenti solo in espressioni non valutate non richiedono un corpo artificiale.

Core v2 supporta costruttori di copia impliciti ed esplicitamente default, inclusi record annidati e array multidimensionali. Le copie selezionate dei membri usano la destinazione effettiva nell’ordine di dichiarazione e degli elementi; l’indirizzo dell’array sorgente viene valutato una volta. Le copie triviali conservano i puntatori memorizzati. Parametri per valore e ritorni da oggetti sorgente mantengono gli effetti della copia; il trasferimento diretto di prvalue non aggiunge copie.

Core v2 supporta anche l’assegnazione per copia implicita o esplicitamente default. Membri ed elementi degli array mantengono operazioni selezionate e ordine; le copie triviali generate diventano assegnazioni tipizzate senza chiamate esterne di copia della memoria. L’assegnazione triviale conserva i puntatori e restituisce il ricevente effettivo. La sintassi dell’operatore acquisisce prima il riferimento sorgente, la chiamata esplicita al membro prima il ricevente; i valori sorgente vengono letti dopo questi effetti. L’assegnazione non costruisce né distrugge oggetti aggiuntivi.

Core v2 supporta inizializzatori predefiniti dei campi ammessi, con accesso ai membri precedenti, chiamate ordinarie, record annidati e array. Il valore selezionato usa l’oggetto effettivo come `this`; le clausole esplicite dell’aggregato conservano il `this` del chiamante. L’inizializzazione esplicita sostituisce il valore predefinito del membro. Copia e assegnazione implicite/default non lo rieseguono; un costruttore di copia utente può selezionarlo per i membri omessi. Inizializzazione dei membri del costruttore e degli aggregati mantengono i rispettivi confini di distruzione dei temporanei. Template e STL completa restano in sviluppo.

Core v2 supporta riferimenti rvalue a oggetti già esistenti: alias di scalari, puntatori, record e array, parametri e risultati per riferimento, xvalue condizionali e normali metodi qualificati `&&`. `static_cast<R&&>(live)` conserva lo stesso oggetto; le variabili di riferimento rvalue con nome restano lvalue. Overload e copie esistenti seguono la scelta di Clang. I riferimenti non creano responsabilità di distruzione aggiuntive.

Core v2 esegue costruttori e assegnazioni di spostamento ordinari definiti dall’utente da una sorgente viva `R&&` o `const R&&`. La costruzione usa la memoria finale; l’assegnazione conserva modifiche alla sorgente, ordine di valutazione e alias `R&` restituito, anche con ricevitore qualificato `&`/`&&`. I riferimenti rvalue con nome scelgono ancora gli overload lvalue; i costruttori espliciti mantengono le regole di inizializzazione. Lo spostamento non termina la vita della sorgente: sorgente e destinazione vengono distrutte normalmente.

Core v2 supporta anche costruzione e assegnazione di spostamento implicite o esplicitamente default, con record annidati e array multidimensionali. I membri eseguono la copia o lo spostamento scelto da C++ nell’ordine di dichiarazione e degli elementi. Gli spostamenti generati non ripetono gli inizializzatori predefiniti; le operazioni triviali conservano i valori dei puntatori. Le sorgenti vengono distrutte normalmente e le operazioni generate inutilizzate o triviali non richiedono un corpo inventato.

Core v2 supporta dichiarazioni standard risolte `noexcept`, `noexcept(true/false)` e `throw()` di C++17, oltre alle query costanti `noexcept(expression)`. Le query rispettano le specifiche di funzioni e distruttori selezionati senza eseguire gli operandi. Tutti gli operandi e le specifiche scritte vengono controllati, anche nel codice inutilizzato. Lancio e cattura delle eccezioni, unwinding dello stack, template e STL completa restano in sviluppo.

Core v2 supporta operatori ordinari sovraccaricati, membri o liberi: aritmetica, confronti, indicizzazione, dereferenziazione, incrementi, oggetti funzione e firme generali di assegnazione. Conserva funzioni selezionate, alias di riferimenti e oggetti risultato. La notazione operatore rispetta l’ordine C++17; gli operatori logici sovraccaricati valutano entrambi gli operandi. Gli operatori liberi di assegnazione inizializzano i parametri da destra a sinistra e li distruggono in ordine inverso. Template, allocazione e STL completa restano in sviluppo.

Core v2 supporta anche funzioni di conversione ordinarie su oggetti vivi: conversioni intere, di enumerazioni e puntatori implicite o esplicite, `bool` esplicito nelle condizioni e risultati come riferimento o oggetto. Ogni conversione chiama una volta il membro selezionato. I riferimenti conservano gli alias; i prvalue oggetto inizializzano direttamente la destinazione effettiva. Le conversioni da riferimento a valore mantengono la copia o lo spostamento selezionato. Qualificatori const e di riferimento, constexpr e noexcept seguono C++17.

Core v2 supporta ora ricevitori temporanei e argomenti temporanei scalari o record passati per riferimento fino alla fine dell’espressione completa, per costruttori, metodi, operatori e conversioni. Ogni valutazione usa memoria effettiva; gli oggetti risultato conservano la destinazione finale e i riferimenti gli alias. Dopo la chiamata e la distruzione dei parametri, i temporanei vengono distrutti nell’ordine inverso di costruzione, anche nelle condizioni e nei cicli. Sono supportati i sotto-oggetti array dei record temporanei.

Core v2 estende ora la durata dei temporanei legati a normali riferimenti locali automatici, inclusi `const T& r{T{...}}` e `T&& r = T{...}`. Scalari, enumerazioni, puntatori e record hanno memoria effettiva. I riferimenti a membri o elementi di array mantengono vivo il record completo quando C++ consente l’estensione. L’inizializzazione con parentesi graffe, anche dopo un segno uguale, conserva lo stesso oggetto. La distruzione segue l’ambito del riferimento, incluse condizioni, cicli e uscite anticipate; gli altri temporanei nell’inizializzatore terminano con la propria espressione completa. Gli alias non aggiungono proprietari e copie o spostamenti successivi conservano le operazioni selezionate. Riferimenti statici/globali/locali al thread, campi riferimento, template e STL completa restano in sviluppo.

Core v2 supporta anche array temporanei autonomi a dimensione fissa nelle chiamate, conversioni a puntatore, indicizzazione, espressioni scartate e riferimenti locali automatici. Ogni array ha una destinazione effettiva; valori scalari, costruttori e inizializzatori predefiniti condivisi inizializzano direttamente gli elementi in ordine, senza proprietari aggiuntivi. Quando C++ estende la durata, i riferimenti a righe o elementi multidimensionali conservano l’array completo. Gli elementi vengono distrutti in ordine inverso alla fine dell’espressione completa o dell’ambito del riferimento. Restano gli indirizzi tipizzati e i limiti esistenti di dimensione, memoria ed espansione. C++/STL completa resta in sviluppo.

Core v2 supporta classi vuote a layout standard e oggetti funzione o conversione senza stato. Gli oggetti vuoti mantengono dimensione e allineamento C++ di un byte, memoria distinta quando richiesta, costruttori e operatori selezionati e distruzione normale. Le copie banali valutano comunque gli operandi; gli elementi vuoti di array e record contenitori rientrano nei limiti di memoria. Il codice NC generato usa un byte interno e la lista dei campi sorgente resta vuota. Ereditarietà, template e STL completa sono ancora in sviluppo.

Core v2 supporta conversioni a void, `void()` e `void{}`, insieme ad alias void compatibili, chiamate, ritorni, espressioni virgola e rami condizionali. Scartare un lvalue non volatile conserva gli effetti del ricevitore e dell’indice senza leggerne il valore memorizzato. Gli oggetti temporanei vengono comunque costruiti e distrutti alla fine dell’espressione completa originale. Non vengono generate variabili o valori void. Gli operandi costanti e noexcept restano verificati. Oggetti volatile, tipi non supportati, template e STL completa restano fuori da questa fase.

Core v2 supporta argomenti predefiniti per funzioni, metodi, operatori di chiamata e costruttori utente ammessi, inclusi parametri finali dei costruttori di copia e spostamento. I nomi vengono risolti alla dichiarazione e le espressioni valutate a ogni chiamata che omette l’argomento. Identità e durata di riferimenti e valori restano invariate. Gli elementi di array senza inizializzatore e le copie generate distruggono i temporanei degli argomenti predefiniti prima dell’elemento successivo; le clausole esplicite conservano il limite dell’espressione completa esterna. Anche i valori inutilizzati o sostituiti vengono verificati. Template e STL completa restano incompleti.

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

**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← ABI dei plugin NeverC](../README.it.md)

# Convenzioni di chiamata personalizzate

NeverC supporta **convenzioni di chiamata personalizzate guidate dai dati**: puoi assegnare registri fisici arbitrari agli argomenti e ai valori di ritorno di qualsiasi funzione, interamente da un plugin esterno o da attributi a livello di sorgente, senza modificare il compilatore né alcuna definizione TableGen.

## Panoramica

Le convenzioni di chiamata tradizionali di LLVM sono cementate nel backend tramite file `.td` / `.inc`. Aggiungerne o modificarne una impone di editare i sorgenti del compilatore e rieseguire TableGen. NeverC sostituisce tutto questo con un modello **guidato dai dati a runtime**, costruito su due livelli:

- Una **spec** — una stringa breve e scrivibile a mano come `gpr:rcx,rdx;ret:rax` — viene allegata a una funzione come attributo stringa `"neverc-callconv"`, da un plugin oppure da un attributo a livello di sorgente.
- Prima della generazione del codice l'host **materializza** quella spec in un attributo `"neverc-cc-plan-v1"`: una tabella di posizioni esatta, immutabile e validata, legata a uno schema di target preciso. Il backend consuma soltanto il piano.

La spec è ciò che scrivi; il piano è ciò di cui il backend si fida. Le convenzioni di chiamata passano così da «cablate nel backend a tempo di compilazione» a «guidate dai dati di una politica esterna a runtime», senza rinunciare alla verifica.

## Formato della spec

Una spec è una stringa delimitata da punti e virgola. Ogni segmento ha una chiave e un elenco di nomi di registro separati da virgole (maiuscole/minuscole indifferenti, spazi tollerati):

```
gpr:rcx,rdx,r8,r9; xmm:xmm0,xmm1; ret:rax; ret_xmm:xmm0
```

| Segmento | Alias | Significato |
|---|---|---|
| `args` | | **Modalità posizionale**: ogni token è un nome di registro oppure `stack`/`mem`, assegnato agli argomenti per indice |
| `gpr` | `arg_gpr` | **Modalità pool**: registri per argomenti interi/puntatore, consumati in ordine; l'eccedenza finisce sullo stack |
| `xmm` | `arg_xmm` | **Modalità pool**: registri per argomenti in virgola mobile/vettoriali |
| `fpr` | | Alias neutro rispetto al target per `xmm` |
| `ret_gpr` | `ret` | Registri di ritorno interi/puntatore |
| `ret_xmm` | | Registri di ritorno in virgola mobile/vettoriali |
| `ret_fpr` | | Alias neutro rispetto al target per `ret_xmm` |
| `csr` | | Insieme personalizzato di registri callee-saved (predefinito: l'insieme ABI standard) |

Qualsiasi segmento può essere omesso e i segmenti sconosciuti vengono ignorati. Le chiavi sono definite una sola volta in [`llvm/include/llvm/CodeGen/NeverCCallConv.h`], così produttori e parser non possono divergere.

### Due modalità per gli argomenti

**Modalità pool** (`gpr:` / `xmm:`): gli argomenti interi prendono i registri dal pool `gpr` in ordine; quelli in virgola mobile e vettoriali attingono da `xmm`. Quando un pool si esaurisce, gli argomenti restanti finiscono sullo stack.

**Modalità posizionale** (`args:`): l'argomento *i* usa l'*i*-esimo token. Ogni token è un nome di registro oppure `stack` / `mem`, che forza quell'argomento sullo stack:

```
args:rcx,stack,r8;ret:rax   # arg0→rcx, arg1→stack, arg2→r8, ritorno→rax
```

Quando `args` è presente, ha la precedenza su `gpr` / `xmm`. Un token che nomina la classe di registro sbagliata per il tipo dell'argomento, un indice oltre l'elenco dei token e un registro già assegnato ripiegano tutti su uno slot di stack, invece di far fallire la compilazione.

### Architetture supportate

I nomi dei registri sono risolti attraverso una tabella specifica per target, unica fonte di verità su ciò che una spec può nominare.

| Architettura | Nomi GPR | Nomi SIMD | Scelta della larghezza |
|---|---|---|---|
| **x86-64** | `rax`, `rbx`, `rcx`, `rdx`, `rsi`, `rdi`, `rbp`, `r8`–`r15` | `xmm0`–`xmm15` | i32 → sottoregistro a 32 bit, i64/puntatore → 64 bit |
| **AArch64** | `x0`–`x28` | `v0`–`v31` | i32→`w`, i64→`x`, f16→`h`, f32→`s`, f64→`d`, f128/vettore→`q` |

I GPR si scrivono sempre nella forma a 64 bit; il backend li restringe al sottoregistro corrispondente al tipo di ciascun valore. Su AArch64 i nomi vettoriali si scrivono `v0`–`v31` e il backend sceglie la forma `H`/`S`/`D`/`Q` in base al tipo.

### Vincoli

- **Registri riservati**: lo stack pointer è assente da entrambe le tabelle (`rsp` su x86-64, `sp`/`x31` su AArch64), così come `x29`/`x30` (FP/LR) su AArch64. Una spec che ne nomini uno si limita a saltarlo e il valore va alla posizione valida successiva.
- **Frame pointer**: `rbp` *è* selezionabile su x86-64 perché è un legittimo registro callee-saved, ma usarlo come registro di argomento è corretto solo con `-fomit-frame-pointer`. Usalo a tuo rischio.
- **Callee-saved**: per impostazione predefinita l'insieme ABI standard. `csr:r12,r13` dichiara un insieme personalizzato e il chiamante costruisce una maschera di registri preservati coerente, così sa quali sopravvivono alla chiamata. Supportato sia su x86-64 sia su AArch64.
- **Conflitti csr**: se un registro compare sia in `csr` sia in un elenco di argomenti o di ritorno, il plugin emette un avviso — il chiamato lo ripristinerebbe, distruggendo il suo ruolo di trasporto del valore. La compilazione riesce comunque.
- **Funzioni variadiche**: non supportate. Il compilatore emette una diagnostica chiara su entrambi i backend invece di passare male la parte variadica in silenzio.
- **Chiamate indirette**: una chiamata tramite puntatore a funzione non può trasportare una convenzione personalizzata. Il plugin avvisa quando viene presa in indirizzo una funzione con convenzione personalizzata; le chiamate indirette ripiegano sulla convenzione standard.
- **Tail call**: disabilitate non appena uno dei due lati di una chiamata usa la convenzione personalizzata, su entrambi i backend.
- **Valori non coperti**: ogni argomento o valore di ritorno che il piano non copre ripiega sulla convenzione standard del target (SysV su x86-64, AAPCS su AArch64).

## Utilizzo

### 1. Guidato dal plugin (consigliato)

Il plugin di riferimento [`CustomCallConvPlugin.c`] è distribuito in `pluginsdk/examples/`. Registra un passo IR a livello di modulo nella fase `neverc.ir.pass.post_opt`.

**Compilare il plugin:**

```bash
cd pluginsdk/examples && make CustomCallConvPlugin.dylib   # oppure .so / .dll
```

**Modalità attributo** (predefinita): sono interessate solo le funzioni che portano un'annotazione `custom_attr` nel sorgente:

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib input.c -o output.o
```

**Modalità globale**: applica un'unica spec a tutte le funzioni definite (richiede un `cc-all` esplicito):

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib \
       -fplugin-arg=org.neverc.example.custom-callconv:cc-all \
       -fplugin-arg=org.neverc.example.custom-callconv:ccspec="gpr:r10,r11,rsi;ret:rdx" \
       input.c -o output.o
```

**Filtrare per prefisso del nome:**

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib \
       -fplugin-arg=org.neverc.example.custom-callconv:cc-all \
       -fplugin-arg=org.neverc.example.custom-callconv:ccprefix=secret_ \
       -fplugin-arg=org.neverc.example.custom-callconv:ccspec="gpr:r9,r8;ret:rax" \
       input.c -o output.o
```

**Diversificare**: alterna quattro disposizioni integrate perché le funzioni non ne condividano una sola (anti-reverse engineering):

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib \
       -fplugin-arg=org.neverc.example.custom-callconv:cc-all \
       -fplugin-arg=org.neverc.example.custom-callconv:ccshuffle \
       input.c -o output.o
```

Le quattro opzioni registrate dal plugin sono `cc-all` e `ccshuffle` (flag, quindi `=1` o `=true` è facoltativo) più `ccspec` e `ccprefix` (valori stringa). Senza `ccspec`, la modalità globale usa il valore predefinito `gpr:r10,r11,rsi,rdi;ret:rdx`.

### 2. Attributi a livello di sorgente

Annota le funzioni direttamente in C con l'attributo `custom_attr`, in sintassi GNU o Microsoft:

```c
// Sintassi GNU
__attribute__((custom_attr("neverc-callconv", "gpr:r10,r11,rsi;ret:rdx")))
int add3(int a, int b, int c) { return a + b + c; }

// Sintassi Microsoft
__declspec(custom_attr("neverc-callconv", "gpr:r10;ret:rdx"))
int msfunc(int a) { return a; }
```

`custom_attr("key", "value")` produce un attributo stringa pulito sulla funzione (`"key"="value"`), **senza** avvisi e **senza** `llvm.global.annotations`. È un meccanismo **generico**: funziona qualsiasi coppia chiave/valore, non solo le convenzioni di chiamata. I passi IR e MIR lo rileggono con `F.getFnAttribute("key")`.

### 3. Combinati

Attributi del sorgente e argomenti del plugin lavorano insieme. Una funzione che porta un `custom_attr` è gestita dal percorso in modalità attributo del plugin; `cc-all` copre il resto. Ogni funzione viene elaborata al più una volta.

## Piani materializzati

Una spec nomina registri; non dice dove risiede ogni byte di ogni valore. Dopo la pipeline di ottimizzazione e prima della generazione del codice, l'host esegue `materializeCallingConventionPlans`, che trasforma ogni funzione `CallingConv::NeverC_Custom` in un piano esatto e validato:

- Una funzione che ha già un attributo `"neverc-cc-plan-v1"` viene **validata, non rigenerata**: la sua impronta di schema, il suo ID di target e il suo ID di convenzione devono corrispondere al target corrente.
- Per una funzione con una spec `"neverc-callconv"`, i nomi dei registri vengono risolti contro la tabella dei registri del target. Il piano risultante sostituisce la spec, che viene poi rimossa dall'IR.
- Una funzione priva di entrambi, ma il cui target registra una convenzione di chiamata tramite l'ABI dei plugin, viene pianificata dalla callback `PlanCallingConvention` di quella convenzione.

Ogni punto di chiamata diretto eredita il piano del proprio chiamato: è questo che tiene d'accordo chiamante e chiamato sul layout attraverso le unità di traduzione. Il piano è una stringa piatta:

```
neverc-cc-plan-v1;schema=<impronta>;target=<high>:<low>;cc=<high>:<low>;stack=<byte>;returns=<posizioni>;arguments=<posizioni>;callee-saved=<numeri di registro>
```

Ogni posizione si scrive `<r|s>,<indice del valore>,<offset del frammento>,<dimensione>,<allineamento>,<numero di registro>,<offset di stack>,<flag>`, e più posizioni sono separate da `|`. Per il percorso integrato l'impronta di schema è `llvm-<triple del target>`; un target registrato da un plugin fornisce la propria.

Poiché i numeri di registro hanno senso solo rispetto allo schema che li definisce, una discordanza è un errore netto anziché una compilazione silenziosamente sbagliata:

| Situazione | Diagnostica |
|---|---|
| La stringa del piano non si analizza | `malformed NeverC calling convention plan` |
| L'impronta di schema differisce | `NeverC calling convention plan belongs to a foreign target schema` |
| L'ID di target differisce | `NeverC calling convention plan has a foreign target ID` |
| L'ID di convenzione differisce | `NeverC calling convention plan has a foreign convention ID` |

È questo che rende sicuro incorporare un piano nel bitcode e trasportarlo attraverso l'LTO: un piano prodotto per un altro target non può essere applicato per sbaglio.

## API del plugin

Il plugin di esempio usa soltanto la tabella stabile IR core: non esiste un punto d'ingresso dedicato alle convenzioni di chiamata. Applicare una convenzione a una funzione sono tre chiamate più la sincronizzazione dei punti di chiamata:

```c
NevercIRAttributeHandle Attribute = {0};
Core->CreateStringAttribute(Core->Context, Task, SV("neverc-callconv"), Spec,
                            &Attribute);
Core->AddFunctionAttribute(Core->Context, Task, Function,
                           NEVERC_IR_ATTRIBUTE_LOCATION_FUNCTION, 0, Attribute);
Core->SetFunctionCallingConvention(Core->Context, Task, Function,
                                   NEVERC_IR_CALLING_CONVENTION_NEVER_C_CUSTOM);
```

`NEVERC_IR_CALLING_CONVENTION_NEVER_C_CUSTOM` è il nome stabile a livello di ABI di `CallingConv::NeverC_Custom` (valore LLVM 1000). Il plugin percorre poi gli usi della funzione con `GetValueUseCount` / `GetValueUse` e, per ogni uso che sia l'operando chiamato di un `call`, `invoke` o `callbr`, imposta la stessa convenzione sull'istruzione tramite `SetInstructionProperty` con `NEVERC_IR_PROPERTY_CALLING_CONVENTION`. Qualsiasi altro uso significa che l'indirizzo è sfuggito, ed è da lì che nasce l'avviso sull'indirizzo preso.

Un plugin che registra un proprio target può invece fornire una callback `PlanCallingConvention` sul suo `NevercCallingConventionDescriptor` e produrre piani direttamente, saltando il livello della spec. Vedi [Target, MC, assembly, oggetti](../target-mc-object.it.md#abi-e-convenzioni-di-chiamata).

## Test

La suite GoogleTest si trova in [`tests/neverc/CustomCallConvTests.cpp`] e conta 26 test. Ciascuno compila il plugin di esempio, traduce un piccolo programma in assembly sotto una data spec e verifica il posizionamento in registro o sullo stack ottenuto.

```bash
ninja -C build-neverc neverc-tests
build-neverc/bin/neverc-tests --gtest_filter='CustomCallConvTest.*'
```

Copertura:

| Categoria | Test |
|---|---|
| x86-64 pool / posizionale / stack / eccedenza / i64 / sret / byval / ripiego | 9 |
| AArch64 GPR / FPR / stack / `csr` / chiamata incrociata fra spec diverse | 5 |
| Frontend `custom_attr` (GNU / `__declspec` / end-to-end) | 3 |
| Materializzazione del piano e rifiuto dello schema | 3 |
| Irrobustimento (`csr`, variadiche su entrambi i target, indiretta, `rsp`, conflitto csr) | 6 |

## Architettura

```
Attributo sorgente            Passo IR del plugin
custom_attr(...)              (neverc.ir.pass.post_opt)
       │                            │
       └─────────────┬──────────────┘
                     ▼
   "neverc-callconv" = spec, CallingConv::NeverC_Custom
   sulla funzione e sui suoi punti di chiamata diretti
                     │
                     ▼
   ┌──────────────────────────────────────────┐
   │ materializeCallingConventionPlans        │
   │ (dopo ottimizzazione, prima di codegen)  │
   │                                          │
   │  spec            → nomi verso i physreg  │
   │  CC del plugin   → PlanCallingConvention │
   │  piano esistente → valida schema/target  │
   └──────────────────────────────────────────┘
                     │
                     ▼
   "neverc-cc-plan-v1" = posizioni validate
   spec rimossa; piano copiato sulle chiamate dirette
                     │
                     ▼
   ┌──────────────────────────────────────────┐
   │ CCAssignFn del backend (uno per target)  │
   │  CC_X86_NeverC     / RetCC_X86_NeverC    │
   │  CC_AArch64_NeverC / RetCC_AArch64_NeverC│
   │                                          │
   │  legge il piano → assegna le posizioni   │
   │  valori scoperti → convenzione standard  │
   │  tail call disabilitate                  │
   └──────────────────────────────────────────┘
                     │
                     ▼
   Codice macchina con il layout di registri scelto
```

L'esecutore nel backend è un'**implementazione fatta una volta per tutte**: tutte le decisioni di politica vivono nel plugin. Aggiungere una nuova convenzione non richiede mai di ricostruire NeverC.

Vedere [`PluginIR.h`] per la tabella di base usata sopra, [`PluginTarget.h`] per `NevercCallingConventionDescriptor` e [`Schema/PhaseSchema.json`] per la fase `neverc.ir.pass.post_opt` a cui il pass si aggancia.

<!-- reference links -->
[`CustomCallConvPlugin.c`]: ../../../pluginsdk/examples/CustomCallConvPlugin.c
[`llvm/include/llvm/CodeGen/NeverCCallConv.h`]: ../../../llvm/include/llvm/CodeGen/NeverCCallConv.h
[`PluginIR.h`]: ../../../neverc/include/neverc/Plugin/PluginIR.h
[`PluginTarget.h`]: ../../../neverc/include/neverc/Plugin/PluginTarget.h
[`Schema/PhaseSchema.json`]: ../../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`tests/neverc/CustomCallConvTests.cpp`]: ../../../tests/neverc/CustomCallConvTests.cpp

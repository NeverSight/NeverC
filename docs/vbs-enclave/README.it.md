**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Indice documentazione](../README.it.md) · [← Progetto NeverC](../i18n/README.it.md)

# DLL di enclave VBS su Windows

NeverC può collegare DLL di enclave VBS compatibili con Microsoft per target Windows a 64 bit. Il contratto del linker supportato è:

```text
/DLL /INCREMENTAL:NO /INTEGRITYCHECK /ENCLAVE /GUARD:MIXED
```

Passa le opzioni del linker Microsoft tramite il driver Windows con `-Xmslink` o `-Wl,`:

```powershell
neverc.exe --target=x86_64-pc-windows-msvc -fno-lto -shared -nostdlib `
  enclave.obj guarded.obj legacy.obj `
  -lvertdll -lbcrypt -llibcmt -llibvcruntime -lucrt `
  -Xmslink /INCREMENTAL:NO `
  -Xmslink /NODEFAULTLIB `
  -Xmslink /ENCLAVE `
  -Xmslink /INTEGRITYCHECK `
  -Xmslink /GUARD:MIXED `
  -Xmslink /DYNAMICBASE `
  -Xmslink /MACHINE:X64 `
  -o game-security-enclave.dll
```

Questo esempio seleziona esplicitamente con `-l` le varianti per enclave delle librerie CRT di MSVC e UCRT. Ogni selezione esplicita di `-vctoolsdir` o `-winsysroot` mantiene la consueta priorità. Senza queste sostituzioni, ogni collegamento con `/ENCLAVE` su macOS, Linux o Windows risolve le librerie Windows esclusivamente dal runtime di destinazione incluso con NeverC; il driver non rileva automaticamente né usa come fallback un toolset Visual Studio o un Windows SDK installati sull’host.

## Build tra host con il runtime incluso

La compilazione e il collegamento COFF sono indipendenti dall’host. Lo stesso comando può essere eseguito su macOS, Linux o Windows dopo aver installato il runtime del target:

```text
neverc runtime install windows-x64
neverc runtime install windows-arm64
```

Il pacchetto del target contiene gli header Windows, il CRT per enclave, l’UCRT per enclave, `vertdll.lib`, `bcrypt.lib` e le altre librerie di importazione Windows richieste. Quando la risoluzione usa il runtime incluso, NeverC passa dalle normali directory CRT/UCRT incluse alle directory CRT/UCRT per enclave soltanto se `/ENCLAVE` esplicito è combinato con `/NODEFAULTLIB` globale. In questa modalità, prima del collegamento, il driver verifica che siano presenti tutti i file inclusi `libcmt.lib`, `libvcruntime.lib`, `ucrt.lib`, `vertdll.lib` e `bcrypt.lib`. Le librerie vengono comunque selezionate esplicitamente con `-l...`. `/ENCLAVE` da solo non abilita le directory CRT/UCRT per enclave né ne seleziona le librerie; restano in uso i normali percorsi di ricerca del runtime incluso.

La fase di collegamento tra host produce la DLL di enclave non firmata e non elaborata. L’elaborazione VEIID, la firma con SignTool e il caricamento effettivo tramite `CreateEnclave`/`LoadEnclaveImage` restano disponibili soltanto su Windows; sposta quindi una DLL collegata su macOS o Linux su una macchina Windows di packaging o test per le ultime tre fasi. Consulta [Runtime di destinazione](../runtime/README.it.md) per l’installazione e il rilevamento dei runtime.

## Input dell’immagine richiesti

Un collegamento di enclave deve fornire entrambe queste definizioni di dati dell’immagine:

- `__enclave_config`, contenente i dati `IMAGE_ENCLAVE_CONFIG` dell’immagine.
- `_load_config_used`, con una struttura load-config abbastanza grande da contenere `EnclaveConfigurationPointer`.

NeverC mantiene attivo `__enclave_config` durante l’eliminazione del codice inutilizzato, lo estrae da un archivio quando necessario e verifica che il puntatore load-config rilocato finale sia uguale all’indirizzo virtuale di quell’oggetto di configurazione. Una definizione mancante, assoluta, eliminata, troncata o rilocata in modo errato causa un errore di collegamento.

`/GUARD:MIXED` abilita l’output CFG per una combinazione di file oggetto protetti e legacy. Genera voci GFID e GIAT di cinque byte: un RVA di quattro byte seguito da un byte di metadati, pari a zero per i normali target correnti. I `GuardFlags` contengono i bit per CFG, delay-IAT protetto e dimensione della voce. Gli oggetti legacy forniscono i target il cui indirizzo viene acquisito mediante una scansione conservativa delle rilocazioni, escludendo i metadati di unwind.
Quando `/GUARD:MIXED` è combinato con `/GUARD:EHCONT`, anche la tabella dei target di continuazione EH usa voci di cinque byte: un RVA di quattro byte seguito da un byte di metadati pari a zero.

Una richiesta esplicita di collegamento incrementale è incompatibile con `/ENCLAVE` e viene rifiutata. Viene usata l’ultima opzione `/INCREMENTAL` effettiva, comprese le opzioni provenienti dalle direttive dei file oggetto.

`/ENCLAVE` non seleziona implicitamente l’output DLL, CFG, il controllo di integrità, le librerie CRT per enclave, l’elaborazione VEIID o la firma. Mantieni esplicite queste scelte nella pipeline di build. In modalità runtime incluso, i percorsi di ricerca CRT/UCRT per enclave e la convalida delle cinque librerie descritti sopra si attivano soltanto con `/NODEFAULTLIB` globale esplicito; senza questa opzione restano in uso i normali percorsi del runtime Windows incluso. Le sostituzioni esplicite della toolchain utente mantengono la consueta priorità.

## Flusso di build e distribuzione

1. Compila i sorgenti sensibili alla sicurezza con CFG abilitato, ad esempio con `-fms-guard=cf`. Gli oggetti legacy possono rimanere non strumentati quando il collegamento finale usa `/GUARD:MIXED`.
2. Definisci la configurazione e il punto di ingresso dell’enclave, quindi collega con CRT/UCRT per enclave e con le librerie di importazione Vertdll e BCrypt richieste.
3. Ispeziona l’immagine PE non firmata e verificane la directory load-config, le tabelle CFG, il puntatore alla configurazione dell’enclave e le rilocazioni di base.
4. Su Windows, esegui lo strumento VEIID del Windows SDK sull’immagine completata.
5. Su Windows, firma con SignTool l’immagine elaborata da VEIID. La firma deve essere l’ultima modifica del file.
6. Nell’host Windows, verifica `IsEnclaveTypeSupported(ENCLAVE_TYPE_VBS)`, alloca l’enclave con `CreateEnclave`, carica la DLL con `LoadEnclaveImage` e chiama `InitializeEnclave`.

Nei sistemi anti-cheat, l’enclave è adatto a un piccolo componente di verifica o gestione delle chiavi il cui codice e stato privato richiedano un confine più forte rispetto al normale processo del gioco. Mantieni ridotta l’interfaccia dell’enclave e convalida tutti i dati forniti dall’host: l’host controlla comunque input, scheduling, archiviazione e disponibilità. Un enclave VBS integra l’autorità lato server, la telemetria, le difese del driver e il normale hardening del processo; non li sostituisce.

## Convalida

Il workflow `VBS enclave differential CI` viene eseguito su Windows. Il suo gate statico:

- compila il linker NeverC e i test COFF mirati;
- crea DLL di enclave equivalenti collegate da Microsoft e da NeverC;
- confronta la semantica pubblica PE/load-config/CFG;
- esegue test di mutazione sul verificatore PE;
- prepara immagini elaborate da VEIID per una verifica differenziale a runtime.

La verifica a runtime esegue prima l’immagine Microsoft. Se il runner ospitato non dispone di VBS o di un ambiente di firma utilizzabile, il risultato viene indicato esplicitamente come salto dovuto all’ambiente. Dopo il corretto caricamento dell’immagine di riferimento Microsoft, il fallimento di uno dei candidati NeverC costituisce un errore di test definitivo. Un runner VBS self-hosted configurato può rendere obbligatorio il successo a runtime.

Il linker supporta immagini di enclave COFF x86-64 e ARM64. Convalida il puntatore di configurazione pubblicato e ricava quindi, dall’insieme finale delle importazioni DLL ordinarie, una sequenza contigua di voci `IMAGE_ENCLAVE_IMPORT` da 80 byte. Inizialmente le voci contengono soltanto il nome di importazione e campi identità a zero, che VEIID deve associare; il linker riscrive conteggio, elenco e dimensione della voce. Le importazioni con caricamento ritardato attive vengono rifiutate. Il linker non impone criteri aggiuntivi sui campi con versione all’interno di `IMAGE_ENCLAVE_CONFIG`.

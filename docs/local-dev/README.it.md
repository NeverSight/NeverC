**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Indice della documentazione](../README.it.md)

# Sviluppo locale

Guida per compilare NeverC dal codice sorgente e configurare un ambiente di sviluppo locale.

---

## Prerequisiti

- CMake 3.20+
- Ninja
- Un compilatore C++17 host (GCC, Clang o MSVC)

---

## Compilazione

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake
cmake --build build-neverc --target neverc
```

`ccache` / `sccache` viene rilevato e attivato automaticamente se presente.

`--target neverc` è la build quotidiana stage-1 (runtime incorporati vuoti) ed
è sufficiente per la maggior parte del lavoro locale. Per incorporare string /
mimalloc / std / NVK nel binario (o allinearsi alla CI), eseguire il target
ombrello stage-2:

```bash
cmake --build build-neverc --target neverc-embed-runtime-bitcode
```

I dettagli del bootstrap a due fasi sono in [Builtins](../builtins/README.it.md).

### Compilazione con test

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake -DNEVERC_INCLUDE_TESTS=ON
cmake --build build-neverc --target check-neverc
```

`check-neverc` dipende da `neverc-embed-runtime-bitcode`, quindi la prima
esecuzione dei test esegue automaticamente bootstrap e rilink. Non serve
invocare a mano il target embed.

---

## Configurazione del PATH (macOS / Linux)

Dopo la compilazione, il binario `neverc` si trova in `build-neverc/bin/neverc`. Utilizzate lo script di supporto per aggiungerlo al `PATH` senza dover digitare ogni volta il percorso completo:

```bash
source ./utils/build/neverc-env.sh
```

Ora potete eseguire `neverc` direttamente:

```bash
neverc --version
neverc -c hello.c -o hello.o
```

### Rimozione dal PATH

Per rimuovere la compilazione locale dal `PATH` nella sessione shell corrente:

```bash
source ./utils/build/neverc-env.sh --remove   # oppure -r
```

### Configurazione permanente

Scrivere automaticamente la riga `source` nel file rc della shell (`~/.zshrc`, `~/.bashrc` o `~/.profile`):

```bash
source ./utils/build/neverc-env.sh --install
```

Annullare:

```bash
source ./utils/build/neverc-env.sh --uninstall
```

### Passare tra sviluppo locale e release

Se avete sia un'installazione release (predefinita: `~/.neverc`) sia una compilazione nell'albero sorgente, usate `neverc-env.sh` per cambiare il `neverc` attivo nella shell corrente senza sovrascrivere nessuna installazione:

```bash
source ./utils/build/neverc-env.sh              # sviluppo locale (build-neverc/bin)
source ./utils/build/neverc-env.sh --local      # come sopra
source ./utils/build/neverc-env.sh --release    # release (~/.neverc/bin)
source ./utils/build/neverc-env.sh --status     # mostra il neverc attivo
source ./utils/build/neverc-env.sh --remove     # rimuove entrambi dal PATH
```

Il cambio imposta `NEVERC_ENV` su `local` o `release`:

```bash
echo "$NEVERC_ENV"
neverc --version
which neverc
```

Se la release è installata in un altro prefisso, indicate la stessa directory usata da `install.sh`:

```bash
NEVERC_INSTALL_DIR=$HOME/.neverc-v3389.1.2 source ./utils/build/neverc-env.sh --release
```

Opzionale — alias nella configurazione della shell (sostituire con il percorso assoluto del repository):

```bash
alias neverc-dev='source /path/to/NeverC/utils/build/neverc-env.sh --local'
alias neverc-rel='source /path/to/NeverC/utils/build/neverc-env.sh --release'
```

---

## Windows (CMD)

Su Windows, utilizzate lo script `.bat` (nessun privilegio di amministratore richiesto):

```cmd
utils\build\neverc-env.bat             &REM aggiungere al PATH (sessione corrente)
utils\build\neverc-env.bat --remove    &REM rimuovere dal PATH (sessione corrente)
utils\build\neverc-env.bat --global    &REM persistere nel PATH utente tramite setx
utils\build\neverc-env.bat --global -r &REM rimuovere dal PATH utente tramite setx
```

A differenza dello script Unix, non è necessario `source` — il `.bat` modifica direttamente la sessione `cmd` corrente. `--global` scrive nel registro utente tramite `setx` (nessun privilegio di amministratore richiesto).

---

## Binari macOS precompilati

Il rilascio è firmato con un certificato Apple Developer ID e notarizzato da Apple. Estraete l'archivio e usatelo direttamente.

---

## Cross-compilazione verso Windows

NeverC include gli SDK di ogni piattaforma in `runtime/` (Windows SDK/WDK, sysroot Linux, sysroot macOS, Android NDK); nessuna configurazione esterna necessaria.

```bash
neverc --target=x86_64-pc-windows-msvc \
  -fbuiltin-string -o hello.exe hello.c -lkernel32
```

Per dyncode Windows (`-fdyncode`, risoluzione import PEB, ecc.), consultate la [documentazione del compilatore dyncode](../dyncode-compiler/README.it.md).

---

## Verifica

```bash
neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
neverc -c /tmp/hello.c -o /tmp/hello.o
```

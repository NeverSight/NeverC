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

### Compilazione con test

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake -DNEVERC_INCLUDE_TESTS=ON
cmake --build build-neverc --target check-neverc
```

---

## Configurazione del PATH (macOS / Linux)

Dopo la compilazione, il binario `neverc` si trova in `build-neverc/bin/neverc`. Utilizzate lo script di supporto per aggiungerlo al `PATH` senza dover digitare ogni volta il percorso completo:

```bash
source ./tools/neverc-env.sh
```

Ora potete eseguire `neverc` direttamente:

```bash
neverc --version
neverc -c hello.c -o hello.o
```

### Rimozione dal PATH

Per rimuovere la compilazione locale dal `PATH` nella sessione shell corrente:

```bash
source ./tools/neverc-env.sh --remove   # oppure -r
```

### Configurazione permanente

Scrivere automaticamente la riga `source` nel file rc della shell (`~/.zshrc`, `~/.bashrc` o `~/.profile`):

```bash
source ./tools/neverc-env.sh --install
```

Annullare:

```bash
source ./tools/neverc-env.sh --uninstall
```

---

## Windows (CMD)

Su Windows, utilizzate lo script `.bat` (nessun privilegio di amministratore richiesto):

```cmd
tools\neverc-env.bat             &REM aggiungere al PATH (sessione corrente)
tools\neverc-env.bat --remove    &REM rimuovere dal PATH (sessione corrente)
tools\neverc-env.bat --global    &REM persistere nel PATH utente tramite setx
tools\neverc-env.bat --global -r &REM rimuovere dal PATH utente tramite setx
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

Per shellcode Windows (`-fshellcode`, risoluzione import PEB, ecc.), consultate la [documentazione del compilatore shellcode](../shellcode-compiler/README.it.md).

---

## Verifica

```bash
neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
neverc -c /tmp/hello.c -o /tmp/hello.o
```

**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Dokumentationsindex](../README.de.md)

# Lokale Entwicklung

Anleitung zum Kompilieren von NeverC aus dem Quellcode und Einrichten einer lokalen Entwicklungsumgebung.

---

## Voraussetzungen

- CMake 3.20+
- Ninja
- Ein C++17-Host-Compiler (GCC, Clang oder MSVC)

---

## Kompilieren

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake
cmake --build build-neverc --target neverc
```

`ccache` / `sccache` wird automatisch erkannt und aktiviert, falls vorhanden.

`--target neverc` ist der tägliche Stage-1-Build (eingebettete Runtimes sind
leere Platzhalter) und reicht für die meisten lokalen Compile-/Debug-Arbeiten.
Wenn string / mimalloc / std / NVK im Binär selbst liegen sollen (oder ein
CI-ähnlicher Compiler gewünscht ist), den Stage-2-Umbrella ausführen:

```bash
cmake --build build-neverc --target neverc-embed-runtime-bitcode
```

Details zum Zwei-Stufen-Bootstrap stehen in [Builtins](../builtins/README.de.md).

### Kompilieren mit Tests

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake -DNEVERC_INCLUDE_TESTS=ON
cmake --build build-neverc --target check-neverc
```

`check-neverc` hängt von `neverc-embed-runtime-bitcode` ab; beim ersten Testlauf
werden Bootstrap und Relink automatisch ausgeführt. Das Embed-Ziel muss nicht
manuell angestoßen werden.

---

## PATH einrichten (macOS / Linux)

Nach dem Kompilieren befindet sich die `neverc`-Binärdatei unter `build-neverc/bin/neverc`. Verwenden Sie das Hilfsskript, um sie zum `PATH` hinzuzufügen, ohne jedes Mal den vollständigen Pfad eingeben zu müssen:

```bash
source ./utils/build/neverc-env.sh
```

Nun können Sie `neverc` direkt ausführen:

```bash
neverc --version
neverc -c hello.c -o hello.o
```

### Aus dem PATH entfernen

Um den lokalen Build aus dem `PATH` zu entfernen, führen Sie in derselben Shell-Sitzung aus:

```bash
source ./utils/build/neverc-env.sh --remove   # oder -r
```

### Dauerhafte Einrichtung

Die `source`-Zeile automatisch in die Shell-RC-Datei (`~/.zshrc`, `~/.bashrc` oder `~/.profile`) schreiben:

```bash
source ./utils/build/neverc-env.sh --install
```

Rückgängig machen:

```bash
source ./utils/build/neverc-env.sh --uninstall
```

### Zwischen lokalem Build und Release wechseln

Wenn sowohl eine Release-Installation (Standard: `~/.neverc`) als auch ein In-Tree-Build vorhanden sind, können Sie mit `neverc-env.sh` das aktive `neverc` in der aktuellen Shell umschalten, ohne eine Installation zu überschreiben:

```bash
source ./utils/build/neverc-env.sh              # lokaler Build (build-neverc/bin)
source ./utils/build/neverc-env.sh --local      # wie oben
source ./utils/build/neverc-env.sh --release    # Release (~/.neverc/bin)
source ./utils/build/neverc-env.sh --status     # aktives neverc anzeigen
source ./utils/build/neverc-env.sh --remove     # beide aus dem PATH entfernen
```

Beim Wechsel wird `NEVERC_ENV` auf `local` oder `release` gesetzt:

```bash
echo "$NEVERC_ENV"
neverc --version
which neverc
```

Wurde die Release in ein anderes Präfix installiert, geben Sie dasselbe Verzeichnis wie bei `install.sh` an:

```bash
NEVERC_INSTALL_DIR=$HOME/.neverc-v3389.1.2 source ./utils/build/neverc-env.sh --release
```

Optional — Aliase in der Shell-Konfiguration (Pfad durch Ihr Repository ersetzen):

```bash
alias neverc-dev='source /path/to/NeverC/utils/build/neverc-env.sh --local'
alias neverc-rel='source /path/to/NeverC/utils/build/neverc-env.sh --release'
```

---

## Windows (CMD)

Unter Windows verwenden Sie das `.bat`-Skript (keine Administratorrechte erforderlich):

```cmd
utils\build\neverc-env.bat             &REM zum PATH hinzufügen (aktuelle Sitzung)
utils\build\neverc-env.bat --remove    &REM aus dem PATH entfernen (aktuelle Sitzung)
utils\build\neverc-env.bat --global    &REM dauerhaft im Benutzer-PATH über setx speichern
utils\build\neverc-env.bat --global -r &REM aus dem Benutzer-PATH über setx entfernen
```

Anders als beim Unix-Skript ist kein `source` nötig — das `.bat` ändert die aktuelle `cmd`-Sitzung direkt. `--global` schreibt über `setx` in die Benutzer-Registry (keine Administratorrechte erforderlich).

---

## Vorkompilierte macOS-Binärdateien

Das Release ist mit einem Apple Developer ID-Zertifikat signiert und von Apple notarisiert. Entpacken und direkt verwenden.

---

## Cross-Kompilierung nach Windows

NeverC enthält Plattform-SDKs in `runtime/` (Windows SDK/WDK, Linux-Sysroot, macOS-Sysroot, Android NDK); kein externes SDK-Setup erforderlich.

```bash
neverc --target=x86_64-pc-windows-msvc \
  -fbuiltin-string -o hello.exe hello.c -lkernel32
```

Für Windows-DynCode (`-fdyncode`, PEB-Import-Auflösung usw.) siehe die [DynCode-Compiler-Dokumentation](../dyncode-compiler/README.de.md).

---

## Überprüfung

```bash
neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
neverc -c /tmp/hello.c -o /tmp/hello.o
```

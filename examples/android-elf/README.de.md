**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Beispiele](../../docs/examples/README.de.md)

# Android ELF Beispiel

Eine native ARM64-ELF-Binärdatei, cross-kompiliert für Android mit NeverC. Entwickelt für die direkte Ausführung auf gerooteten Android-Geräten über `adb shell`. Kann von macOS, Windows oder Linux aus gebaut werden — kein Android NDK oder CMake erforderlich.

NeverC enthält ein Android-Sysroot (NDK r26c, API 21+) in `runtime/android/`, sodass Vorverarbeitung, Kompilierung, Optimierung (Auto-LTO) und Linking in einem einzigen Aufruf erledigt werden.

## Build

Aus dem Repository:

```bash
cd examples/android-elf
neverc make
```

Mit einer eigenständigen NeverC-Version:

```bash
neverc make NEVERC=/path/to/neverc
```

## Manueller Build (ohne Make)

```bash
neverc --target=aarch64-linux-android -Wall -fPIE -lm -ldl -llog -o android-elf main.c
```

## Bereitstellung und Ausführung

Über adb auf das Gerät übertragen und ausführen:

```bash
neverc make run
```

Oder manuell:

```bash
adb push android-elf /data/local/tests/
adb shell chmod 755 /data/local/tests/android-elf
adb shell /data/local/tests/android-elf
```

## Funktionen

- Zeigt Geräteinformationen (`uname`) und Kernel-Version an
- Prüft Root-/Berechtigungsstatus (`uid`/`euid`, `su`-Pfade)
- Lädt dynamisch `liblog.so` und ruft `__android_log_print` auf
- Liest `/proc/self/maps` zur Anzeige des Speicherlayouts
- Demonstriert `dlopen`/`dlsym`, `readlink`, `fopen` auf Android

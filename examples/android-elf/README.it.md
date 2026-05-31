**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Esempio Android ELF

Un binario ELF nativo ARM64 cross-compilato per Android con NeverC. Progettato per essere eseguito direttamente su dispositivi Android rootati tramite `adb shell`. Compilabile da macOS, Windows o Linux — senza Android NDK o CMake.

NeverC include un sysroot Android (NDK r26c, API 21+) in `runtime/android/`, quindi una singola invocazione gestisce preprocessing, compilazione, ottimizzazione (LTO automatico) e linking.

## Compilazione

Dal repository:

```bash
cd examples/android-elf
make
```

Con una release standalone di NeverC:

```bash
make NEVERC=/path/to/neverc
```

## Compilazione manuale (senza Make)

```bash
neverc --target=aarch64-linux-android21 -Wall -fPIE -lm -ldl -llog -o android-elf main.c
```

## Deploy ed esecuzione

Trasferire sul dispositivo ed eseguire via adb:

```bash
make run
```

O manualmente:

```bash
adb push android-elf /data/local/tmp/
adb shell chmod 755 /data/local/tmp/android-elf
adb shell /data/local/tmp/android-elf
```

## Funzionalità

- Mostra informazioni del dispositivo (`uname`) e versione del kernel
- Verifica lo stato root/privilegi (`uid`/`euid`, percorsi `su`)
- Carica dinamicamente `liblog.so` e chiama `__android_log_print`
- Legge `/proc/self/maps` per visualizzare il layout della memoria
- Dimostra `dlopen`/`dlsym`, `readlink`, `fopen` su Android

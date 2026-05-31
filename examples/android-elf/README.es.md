**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Ejemplo Android ELF

Un binario ELF nativo ARM64 compilado de forma cruzada para Android usando NeverC. Diseñado para ejecutarse directamente en dispositivos Android rooteados a través de `adb shell`. Se puede compilar desde macOS, Windows o Linux — sin necesidad de Android NDK ni CMake.

NeverC incluye un sysroot Android (NDK r26c, API 21+) en `runtime/android/`, por lo que una sola invocación maneja preprocesamiento, compilación, optimización (LTO automático) y enlazado.

## Compilación

Desde el repositorio:

```bash
cd examples/android-elf
make
```

Con una versión independiente de NeverC:

```bash
make NEVERC=/path/to/neverc
```

## Compilación manual (sin Make)

```bash
neverc --target=aarch64-linux-android -Wall -fPIE -lm -ldl -llog -o android-elf main.c
```

## Despliegue y ejecución

Enviar al dispositivo y ejecutar via adb:

```bash
make run
```

O manualmente:

```bash
adb push android-elf /data/local/tmp/
adb shell chmod 755 /data/local/tmp/android-elf
adb shell /data/local/tmp/android-elf
```

## Funcionalidades

- Muestra información del dispositivo (`uname`) y versión del kernel
- Comprueba el estado root/privilegios (`uid`/`euid`, rutas `su`)
- Carga dinámicamente `liblog.so` y llama a `__android_log_print`
- Lee `/proc/self/maps` para mostrar el mapa de memoria
- Demuestra `dlopen`/`dlsym`, `readlink`, `fopen` en Android

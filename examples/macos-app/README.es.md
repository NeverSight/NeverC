**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Ejemplo de aplicación macOS

Un ejecutable nativo macOS Mach-O compilado de forma cruzada con NeverC. Demuestra sysctl, uname y las API del kernel Mach para la introspección del sistema y procesos. Compilación desde macOS, Windows o Linux — sin Xcode.

## Compilación

Desde el repositorio (objetivo por defecto: `arm64-apple-macos`):

```bash
cd examples/macos-app
neverc make
```

Compilar para Intel:

```bash
neverc make TARGET=x86_64-apple-macos
```

Con una versión independiente de NeverC:

```bash
neverc make NEVERC=/path/to/neverc
```

## Compilación manual (sin Make)

```bash
neverc --target=arm64-apple-macos -Wall -o macos-app main.c
```

## Ejecución

```bash
./macos-app
```

## Funcionalidades

- Consulta de información del kernel mediante `uname`
- Lectura de detalles de hardware mediante `sysctl` (modelo, número de CPUs, tamaño de memoria, tamaño de página)
- Información de identidad del proceso (`getpid`, `getppid`, `getuid`)
- Obtención de información del host Mach (`host_info`) y estadísticas de memoria de tarea (`task_info`)

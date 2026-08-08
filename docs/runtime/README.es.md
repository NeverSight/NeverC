**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Índice de documentación](../README.es.md) · [← Proyecto NeverC](../../README.md)

# `neverc runtime`

Gestiona paquetes **runtime de cross-compilación** (sysroots / SDK) desde
[GitHub Releases](https://github.com/NeverSight/NeverC/releases). Viven en
`<NeverC-root>/runtime/` (suele ser `~/.neverc/runtime/`).

Prefiera `neverc runtime install …` a descomprimir a mano
`neverc-runtime-<target>.zip`.

## Sintaxis

```text
neverc runtime install <target> [--version <tag>]
neverc runtime install all [--version <tag>]
neverc runtime update <target> [--version <tag>]
neverc runtime remove <target>
neverc runtime list
neverc runtime --help
```

Alias: `upgrade` → `update`; `uninstall` → `remove`; `ls` → `list`.

## Destinos disponibles

| Destino | Diseño bajo `runtime/` |
|--------|-------------------------|
| `windows-x64` | `windows/x64` (+ shared `windows/shared`) |
| `windows-arm64` | `windows/arm64` (+ shared `windows/shared`) |
| `linux-x64` | `linux/x64` |
| `linux-arm64` | `linux/arm64` |
| `macos-arm64` | `macos/arm64` |
| `android-arm64` | `android/arm64` |
| `android-kernel-arm64` | `android/kernel` |

## Ejemplos

```bash
neverc runtime install windows-x64
neverc runtime install all
neverc runtime update linux-arm64 --version v3389.1.2
neverc runtime remove macos-arm64
neverc runtime list
```

## Subcomandos

- **`install`**: instala un destino con la **etiqueta del compilador** por defecto (o `--version`). Misma etiqueta → éxito; distinta → confirmación `[Y/n]`.
- **`install all`**: instala todos los destinos **faltantes**; los ya instalados se omiten.
- **`update` / `upgrade`**: fuerza la descarga sin pregunta. Predeterminado: **latest**.
- **`remove` / `uninstall`**: borra el directorio y actualiza `manifest.json`.
- **`list` / `ls`**: estado de instalación y etiqueta del compilador.

## Reglas de versión

| Comando | Predeterminado sin `--version` |
|---------|--------------------------------|
| `install` / `install all` | Etiqueta release del compilador |
| `update` | Última release que publica ese asset runtime |

Etiquetas `vMAJOR.MINOR.PATCH`; verificación con `SHA256SUMS` antes de extraer.

## Relación con `neverc update`

- `neverc runtime …` solo cambia **sysroots**.
- [`neverc update`](../update/README.es.md) alinea **compilador + runtimes ya instalados**.

Tras actualizar el compilador, solo instale destinos **nuevos** con `runtime install`.

## Comandos relacionados

| Comando | Uso |
|---------|-----|
| [`neverc update`](../update/README.es.md) | Compilador y runtimes instalados juntos |
| [`neverc build` / `make`](../build/README.es.md) | Ejemplos de cross-compilación |
| [Examples](../examples/README.es.md) | Makefiles con `--target=…` |
| `neverc runtime --help` | Ayuda integrada |

**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Índice de documentación](../README.es.md) · [← Proyecto NeverC](../../README.md)

# `neverc runtime`

Gestiona paquetes de **runtime de compilación cruzada** (sysroots / SDK) de
[GitHub Releases](https://github.com/NeverSight/NeverC/releases). Viven bajo
`<NeverC-root>/runtime/` junto al compilador (instalación por defecto:
`~/.neverc/runtime/`).

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
|---------|------------------------|
| `windows-x64` | `windows/x64` (+ compartido `windows/shared`) |
| `windows-arm64` | `windows/arm64` (+ compartido `windows/shared`) |
| `linux-x64` | `linux/x64` |
| `linux-arm64` | `linux/arm64` |
| `macos-arm64` | `macos/arm64` |
| `android-arm64` | `android/arm64` |
| `android-kernel-arm64` | `android/kernel` |

## Subcomandos

### `install`

Instala un destino con la **etiqueta de release del compilador** por defecto (o
`--version <tag>`). Nombre del activo: `neverc-runtime-<target>.zip`.

```bash
neverc runtime install windows-x64
neverc runtime install linux-arm64 --version v3389.1.2
```

Si el destino ya está instalado:

- Misma etiqueta → informar y salir con éxito.
- Etiqueta distinta / desconocida → confirmar `[Y/n]` antes de reinstalar.

### `install all`

Instala **todos los destinos faltantes** del catálogo en la versión del
compilador (o `--version`). Los ya instalados se omiten; para cambiar el pin,
vuelva a ejecutar `install` en un solo destino.

```bash
neverc runtime install all
```

### `update` / `upgrade`

Fuerza la descarga de un destino sin pregunta interactiva. La versión por
defecto es **latest** (a diferencia de `install`, que sigue la etiqueta del
compilador). Use `--version` para fijarla.

```bash
neverc runtime update windows-x64
neverc runtime update android-arm64 --version v3389.1.2
```

### `remove` / `uninstall`

Elimina el directorio de un destino instalado y actualiza
`runtime/manifest.json`.

```bash
neverc runtime remove linux-x64
```

### `list` / `ls`

Muestra cada destino del catálogo como instalado (con etiqueta registrada) o no
instalado, más la etiqueta viva del compilador.

```bash
neverc runtime list
```

## Reglas de versión

| Comando | Predeterminado sin `--version` |
|---------|--------------------------------|
| `install` / `install all` | Etiqueta de release del compilador |
| `update` | Último release que publica ese activo runtime |

Las etiquetas tienen forma `vMAJOR.MINOR.PATCH`. Los archivos se verifican con
`SHA256SUMS` del release antes de extraerse.

## Relación con `neverc update`

- `neverc runtime …` cambia **solo sysroots**.
- [`neverc update`](../update/README.es.md) mueve el **compilador y todos los
  runtimes ya instalados** a una etiqueta en una sola transacción.

Tras actualizar el compilador con `neverc update`, los runtimes instalados ya
están alineados; solo necesita `runtime install` para destinos **nuevos**.

## Comandos relacionados

| Comando | Cuándo usarlo |
|---------|---------------|
| [`neverc update`](../update/README.es.md) | Subir/bajar compilador + runtimes instalados juntos |
| [`neverc build` / `make`](../build/README.es.md) | Construir ejemplos de compilación cruzada contra estos sysroots |
| [Examples](../examples/README.es.md) | `Makefile`s de ejemplo con `--target=…` |
| `neverc runtime --help` | Resumen de uso integrado |

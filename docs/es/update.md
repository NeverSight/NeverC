**Idiomas**: [English](../update.md) | [简体中文](../zh-CN/update.md) | [繁體中文](../zh-TW/update.md) | [日本語](../ja/update.md) | [한국어](../ko/update.md) | [Français](../fr/update.md) | [Deutsch](../de/update.md) | [Español](update.md) | [Italiano](../it/update.md) | [Русский](../ru/update.md) | [العربية](../ar/update.md)

[← Índice de documentación](README.md) · [← Proyecto NeverC](project.md)

# `neverc update`

Actualiza una **instalación release** para que el compilador y cada runtime de
cross-compilación **ya instalado** pasen juntos a **una etiqueta de release concreta**.
`neverc upgrade` es un alias.

Pensado para installs con `install.sh` o `install.ps1` (suele ser `~/.neverc`). **No** actualiza
un árbol CMake/Ninja de fuentes — cambie PATH y reconstruya; ver
[Desarrollo local](local-dev.md).

## Sintaxis

```text
neverc update
neverc update <version>
neverc update --version <version>
neverc update --help
```

Ejemplos:

```bash
neverc update                 # release completa más reciente para este host
neverc update v3389.1.2       # etiqueta exacta (subir o bajar)
neverc update 3389.1.2        # la «v» inicial es opcional
neverc upgrade                # igual que neverc update
```

`-y` / `--yes` se aceptan por compatibilidad con scripts; la actualización no es interactiva.

## Alcance

| Componente | Comportamiento |
|------------|----------------|
| Compilador (`bin/`, `lib/`, `pluginsdk/`) | Se reemplaza si la etiqueta objetivo difiere |
| Runtimes ya en `runtime/` | Solo se vuelven a obtener los destinos **ya instalados** |
| Runtimes ausentes | **No** se instalan solos — [`neverc runtime install`](runtime.md) |

## Modelo de seguridad

1. Bloqueo exclusivo en `<install>/.neverc-update.lock`.
2. Resolver la etiqueta objetivo.
3. Descargar y verificar `SHA256SUMS` y archivos.
4. Staging, validación y commit; si falla, rollback.

Si un runtime sale mal, indique una etiqueta anterior:

```bash
neverc update v3389.0.1
```

## Restricciones

- Solo raíz de instalación release (normalmente `~/.neverc`). Rechaza raíces FS y árboles CMake.
- El host debe coincidir con un asset de compilador publicado.
- En Windows, un proceso auxiliar corto puede sustituir `neverc.exe` al salir.

## Comandos relacionados

| Comando | Uso |
|---------|-----|
| [`neverc runtime`](runtime.md) | Sysroots individuales sin cambiar el compilador |
| [`neverc run`](run.md) | Compilar y ejecutar un binario temporal |
| [`neverc build` / `make`](build.md) | Ejecutar Makefiles de ejemplos/proyectos |
| `neverc update --help` | Ayuda integrada |

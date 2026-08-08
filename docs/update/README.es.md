**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Índice de documentación](../README.es.md) · [← Proyecto NeverC](../../README.md)

# `neverc update`

Actualiza una **instalación release** para que el compilador y cada runtime de
cross-compilación **ya instalado** pasen juntos a **una etiqueta de release concreta**.
`neverc upgrade` es un alias.

Pensado para installs con `install.sh` (suele ser `~/.neverc`). **No** actualiza
un árbol CMake/Ninja de fuentes — cambie PATH y reconstruya; ver
[Desarrollo local](../local-dev/README.es.md).

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
| Runtimes ausentes | **No** se instalan solos — [`neverc runtime install`](../runtime/README.es.md) |

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
| [`neverc runtime`](../runtime/README.es.md) | Sysroots individuales sin cambiar el compilador |
| [`neverc run`](../run/README.es.md) | Compilar y ejecutar un binario temporal |
| [`neverc build` / `make`](../build/README.es.md) | Ejecutar Makefiles de ejemplos/proyectos |
| `neverc update --help` | Ayuda integrada |

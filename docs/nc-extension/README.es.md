**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Documentación NeverC](../README.es.md)

# La extensión de archivo `.nc`

## Descripción general

NeverC reconoce `.nc` como su extensión de archivo fuente nativa. Cuando el compilador detecta un archivo de entrada `.nc`, **habilita automáticamente** todas las extensiones del lenguaje NeverC — sin necesidad de flags adicionales.

## Funcionalidades habilitadas automáticamente

| Flag | Efecto |
|------|--------|
| `-fneverc-types` | Alias de enteros estilo Rust (`u8`, `u16`, `u32`, `u64`, `i8`, `i16`, `i32`, `i64`, `usize`, `isize`) |
| `-fbuiltin-string` | Tipo valor `string` integrado con gestión automática de memoria, sintaxis dot-call y soporte UTF-8 |

## Uso

Simplemente nombre su archivo fuente con la extensión `.nc`:

```bash
# Automático — no se necesitan flags adicionales
neverc hello.nc -o hello

# Equivalente a:
neverc -fneverc-types -fbuiltin-string hello.c -o hello
```

```c
// hello.nc
#include <stdio.h>

int main(void) {
    string greeting = "¡Hola, NeverC!";
    printf("%s (len=%zu)\n", greeting.c_str(), greeting.len);

    u32 x = 42;
    i64 y = -100;

    string msg = greeting + " x=%u, y=%lld".format(x, y);
    printf("%s\n", msg.c_str());
    return 0;
}
```

## Cómo funciona

La detección opera en dos niveles del pipeline del compilador:

### 1. Capa Driver / Toolchain

El driver inspecciona la extensión de cada archivo de entrada antes de construir la invocación del compilador. Para archivos `.nc`, `-fneverc-types` y `-fbuiltin-string` se inyectan incondicionalmente en la línea de comandos — el usuario no necesita pasarlos manualmente.

Para archivos `.c`, estos flags permanecen opcionales: pase explícitamente los flags que necesite (`-fneverc-types`, `-fbuiltin-string`).

### 2. Capa CompilerInvocation

Como red de seguridad, el frontend también verifica las extensiones de los archivos de entrada al analizar la invocación. Si alguna entrada tiene la extensión `.nc`, `LangOpts.NeverCTypes` y `LangOpts.BuiltinString` se establecen en `1`, asegurando que las funcionalidades estén activas incluso si se omite la capa driver (por ej., al invocar `-cc1` directamente).

## Compatibilidad

- Los archivos `.nc` se tratan como código fuente C — el lenguaje sigue siendo C (C23 por defecto), no un lenguaje nuevo
- Todos los flags estándar de C (`-std=c11`, `-O2`, `-g`, `-Wall`, etc.) funcionan de manera idéntica
- `-fshellcode` se combina naturalmente con `.nc`: el modo shellcode ya habilita `string`, y `.nc` asegura que `neverc-types` también esté activo
- La compilación cruzada (`-target aarch64-linux-gnu`, etc.) funciona de la misma manera
- Los archivos `.c` no se ven afectados — se comportan exactamente como antes a menos que pase los flags explícitamente

## Cuándo usar `.nc` vs `.c`

| Escenario | Recomendación |
|-----------|--------------|
| Nuevo proyecto NeverC usando `string` y tipos estilo Rust | Usar `.nc` |
| Base de código C existente que debe seguir siendo compatible con otros compiladores | Usar `.c` + flags explícitos |
| Proyecto shellcode | Ambos sirven — `-fshellcode` habilita `string` de todas formas |
| Base de código mixta | `.nc` para archivos específicos de NeverC, `.c` para código portable |

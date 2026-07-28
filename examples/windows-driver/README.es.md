**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Ejemplos NeverC](../../docs/examples/README.es.md)

# Ejemplo de controlador de kernel Windows

Un controlador de kernel WDM mínimo construido con NeverC. Apunta a **x64** por
defecto y también puede compilarse para ARM64. Compilación cruzada desde macOS / Linux.

NeverC es un compilador todo-en-uno — una sola invocación maneja preprocesamiento,
compilación, optimización (auto-LTO) y enlazado a través del enlazador integrado.

## Compilación

Desde el repositorio:

```bash
cd examples/windows-driver
neverc make
```

Esto genera `ExampleDriver-x64.sys`. Para compilar para ARM64, o para ambas:

```bash
neverc make ARCH=arm64
neverc make all-arch
```

Desde una versión independiente de NeverC:

```bash
neverc make NEVERC=/path/to/neverc
```

La salida es `ExampleDriver-<arq>.sys` (optimizado con auto-LTO).
La compilación por defecto incluye `-g` para depuración; **las versiones de
producción deben eliminar `-g`** para quitar los símbolos de depuración y reducir
el tamaño del binario (~38 KB → ~3 KB).

## Compilación manual (sin Make)

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fms-kernel \
  -Wall -nostdlib -shared \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -lntoskrnl -lhal \
  -o ExampleDriver-x64.sys driver.c
```

Para ARM64, cambie el destino a `aarch64-pc-windows-msvc`; nada más cambia.
`-fms-kernel` selecciona los encabezados y bibliotecas de importación del WDK
correspondientes al destino y define las macros de arquitectura que el WDK
espera, por lo que nunca hay que pasarlas a mano.

> `-g` incrusta información de depuración DWARF en el PE; inspecciónela con
> `llvm-dwarfdump`. Omita esta opción en versiones de producción para reducir
> el tamaño del binario.

## Funcionalidades

- Crea un objeto de dispositivo en `\Device\ExampleDriver`
- Crea un enlace simbólico en `\DosDevices\ExampleDriver`
- Maneja `IRP_MJ_CREATE`, `IRP_MJ_CLOSE`, `IRP_MJ_DEVICE_CONTROL`
- Imprime mensajes de carga/descarga a través de `DbgPrint`

## Carga (en una máquina de prueba Windows)

```cmd
sc create ExampleDriver type= kernel binPath= C:\path\to\ExampleDriver-x64.sys
sc start ExampleDriver
sc stop ExampleDriver
sc delete ExampleDriver
```

Active la firma de prueba o utilice un certificado de firma de código para producción.

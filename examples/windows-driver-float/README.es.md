**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Controlador de kernel Windows con coma flotante

Un controlador de kernel WDM construido con NeverC que demuestra el **uso
seguro de operaciones de coma flotante / SIMD en modo kernel**. Compilación
cruzada desde macOS / Linux.

## Compilación

```bash
cd examples/windows-driver-float
make
```

Desde una versión independiente de NeverC:

```bash
make NEVERC=/path/to/neverc
```

La salida es `FloatDriver.sys` (optimizado con auto-LTO).
La compilación por defecto incluye `-g` para depuración; elimine `-g` para
versiones de producción.

---

## Dos problemas a manejar

La coma flotante en modo kernel tiene dos problemas distintos:

### Problema 1 — el marcador ABI `_fltused` (tiempo de compilación/enlace)

El compilador MSVC emite una referencia no definida al símbolo `_fltused`
cada vez que una unidad de traducción realiza alguna operación de coma
flotante. En programas de modo usuario, `libcmt.lib` proporciona este
símbolo, satisfaciendo al enlazador e incluyendo algunas partes del CRT
específicas de FP.

Los controladores de kernel **no** se enlazan contra `libcmt` (pasamos
`-nostdlib` y `-Xlinker --nodefaultlib`), por lo que un `_fltused` sin
resolver causaría un error de enlace.

**Cómo lo resuelve NeverC**: con `-fms-kernel`, el backend X86 de LLVM
define `_fltused` localmente como 0. Puede verlo en el ensamblado generado:

```asm
# Objetivo modo usuario:
    .globl  _fltused              # referencia externa -- necesita libcmt
```

```asm
# Objetivo -fms-kernel:
    .globl  _fltused
    .set    _fltused, 0           # ¡definición local! no se requiere símbolo externo
```

Por lo tanto, **nunca tiene que escribir manualmente `int _fltused = 0;`** en su controlador.

### Problema 2 — el kernel NO preserva los registros FP/SIMD (tiempo de ejecución)

El kernel de Windows **no** guarda/restaura los registros x87 / XMM / YMM / ZMM
en los cambios de contexto por defecto. Si un controlador toca cualquiera
de estos desde código de kernel arbitrario, corromperá silenciosamente el
estado SIMD del hilo de modo usuario que esté ejecutándose en la CPU.

**Solución**: encierre cada región de coma flotante / SIMD con
[`KeSaveExtendedProcessorState`](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kesaveextendedprocessorstate)
y `KeRestoreExtendedProcessorState`:

```c
XSTATE_SAVE save;
NTSTATUS status = KeSaveExtendedProcessorState(XSTATE_MASK_LEGACY, &save);
if (!NT_SUCCESS(status))
    return status;

// ... su código FP / SIMD aquí ...

KeRestoreExtendedProcessorState(&save);
```

### Máscaras XSTATE

| Máscara | Cubre |
|---------|-------|
| `XSTATE_MASK_LEGACY_FLOATING_POINT` (bit 0) | pila x87 |
| `XSTATE_MASK_LEGACY_SSE` (bit 1) | XMM0–15 |
| `XSTATE_MASK_LEGACY` | bit 0 \| bit 1 (cubre la mayoría del código `double` / SSE simple) |
| `XSTATE_MASK_GSSE` / AVX (bit 2) | mitades superiores de YMM0–15 |
| `XSTATE_MASK_AVX512` | registros ZMM AVX-512 |

Pase la máscara combinada con OR que coincida con los registros más amplios que usa su código.

---

## Qué hace este controlador

- Crea un objeto de dispositivo en `\Device\FloatDriver` y un enlace simbólico en `\DosDevices\FloatDriver`
- En `DriverEntry`, llama a `ComputeAreaSafe()` (que envuelve `ComputeArea()`
  con guardado/restauración de estado FP) dos veces con `radius=1.0` y `radius=5.0`
- Imprime los bits brutos del double a través de `DbgPrint` (porque `%f` no
  está soportado por `DbgPrint` — usamos `RtlCopyMemory` para extraer el patrón de 64 bits)
- Define `_fltused` implícitamente a través de `-fms-kernel`

## Verificación de la emisión de `_fltused`

Compare la salida del compilador con y sin `-fms-kernel`:

```bash
# Modo usuario (necesitaría libcmt):
neverc --target=x86_64-pc-windows-msvc -S /tmp/foo.c -o - | grep fltused
#  .globl  _fltused

# Kernel (definido localmente como 0):
neverc --target=x86_64-pc-windows-msvc -fms-kernel -S /tmp/foo.c -o - | grep fltused
#  .globl  _fltused
#  .set    _fltused, 0
```

## Carga (en una máquina de prueba Windows)

```cmd
sc create FloatDriver type= kernel binPath= C:\path\to\FloatDriver.sys
sc start FloatDriver
sc stop FloatDriver
sc delete FloatDriver
```

Active la firma de prueba o utilice un certificado de firma de código para producción.

## Advertencias

- **`%f` no funciona con `DbgPrint`** — la rutina de impresión de depuración
  del kernel no tiene formato de coma flotante. Convierta su double a entero
  de coma fija para mostrar, o imprima los bits brutos como hace este ejemplo.
- **No use coma flotante en IRQL ≥ DISPATCH_LEVEL** a menos que sea absolutamente
  necesario. `KeSaveExtendedProcessorState` documenta las restricciones IRQL.
- **Rendimiento**: guardar/restaurar estado no es gratis; para rutas críticas
  considere agrupar el trabajo FP en una sola región encerrada.

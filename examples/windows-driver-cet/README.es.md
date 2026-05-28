# Controlador de kernel Windows con CET Shadow Stack

Un controlador de kernel WDM mínimo construido con NeverC, con Intel CET
(Control-flow Enforcement Technology) Kernel Shadow Stack habilitado.
Compilación cruzada desde macOS / Linux.

## Compilación

```bash
cd examples/windows-driver-cet
make
```

Desde una versión independiente de NeverC:

```bash
make NEVERC=/path/to/neverc
```

La salida es `CetDriver.sys` (optimizado con auto-LTO).
La compilación por defecto incluye `-g` para depuración; **las versiones de
producción deben eliminar `-g`** para quitar símbolos de depuración y reducir
el tamaño del binario.

## Flags específicos de CET

| Flag | Capa | Propósito |
|------|------|-----------|
| `-fcf-protection=return` | Compilador | Generar código compatible con Shadow Stack |
| `-Xlinker --cetcompat` | Enlazador | Establecer `IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT` en PE |

## Compilación manual (sin Make)

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fcf-protection=return \
  -fms-kernel -fms-extensions -fms-compatibility \
  -D_AMD64_ -DNTDDI_VERSION=0x06010000 -D_WIN32_WINNT=0x0601 \
  -Wall -nostdlib -shared \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -Xlinker --cetcompat \
  -lntoskrnl -lhal \
  -o CetDriver.sys driver.c
```

## Funcionalidades

- Crea un objeto de dispositivo en `\Device\CetDriver`
- Crea un enlace simbólico en `\DosDevices\CetDriver`
- Ejecuta llamadas indirectas (puntero de función `ComputeFn`) para validar la compatibilidad CET — Shadow Stack protege las direcciones de retorno de estas llamadas
- Imprime mensajes de carga/descarga a través de `DbgPrint`

---

## Detalles técnicos del CET

CET tiene **dos mecanismos de protección independientes**:

### 1. Shadow Stack — protección del borde posterior (RET)

El hardware mantiene una segunda pila (shadow stack) que refleja las operaciones CALL/RET.
**No se necesitan instrucciones especiales en la entrada de la función** — es completamente transparente:

```
┌─ CALL target ─────────────────────────────────┐
│                                                │
│  Pila normal:   PUSH return_addr  (RSP)        │
│  Shadow stack:  PUSH return_addr  (SSP, HW)    │
│                                                │
└────────────────────────────────────────────────┘

┌─ RET ─────────────────────────────────────────┐
│                                                │
│  Pila normal:   POP return_addr_A  (RSP)       │
│  Shadow stack:  POP return_addr_B  (SSP, HW)   │
│                                                │
│  Comparar: return_addr_A == return_addr_B ?     │
│    ✓ coincide → retorno normal                  │
│    ✗ no coincide → excepción #CP                │
│                                                │
└────────────────────────────────────────────────┘
```

### 2. Indirect Branch Tracking (IBT) — protección del borde anterior (CALL/JMP indirecto)

Requiere una instrucción `ENDBR64` (`F3 0F 1E FA`, 4 bytes) en cada objetivo válido de llamada/salto indirecto. En CPUs sin CET, `ENDBR64` es un NOP.

### Elección del kernel de Windows

| Protección | Mecanismo | ¿Usado por el kernel de Windows? |
|------------|-----------|----------------------------------|
| Borde posterior (RET) | CET Shadow Stack | **Sí** (KCET) |
| Borde anterior (CALL/JMP indirecto) | CET IBT (ENDBR) | **No** — se usa CFG en su lugar |

### Comparación de ensamblador: modos `-fcf-protection`

Código fuente:

```c
unsigned long rotate13(unsigned long val) {
    return (val << 13) | (val >> 19);
}
```

#### `-fcf-protection=none` (sin CET)

```asm
rotate13:
    mov  eax, ecx
    rol  eax, 13
    ret
```

#### `-fcf-protection=return` (solo Shadow Stack — este ejemplo usa este modo)

```asm
rotate13:
    mov  eax, ecx      ; ¡idéntico a "none"!
    rol  eax, 13        ; Shadow Stack es completamente transparente
    ret
```

#### `-fcf-protection=full` (Shadow Stack + IBT)

```asm
rotate13:
    endbr64             ; ← marcador IBT (F3 0F 1E FA)
    mov  eax, ecx
    rol  eax, 13
    ret
```

---

## Compilador vs bin2bin: ¿quién es amigable con CET?

CET traza una línea clara entre los **compiladores a nivel de código fuente**
y las **herramientas bin2bin** (empaquetadores, ofuscadores, hookers,
dump+rebuild). El Shadow Stack por hardware impone tres reglas que
remodelan toda la industria de protección / ofuscación:

> 1. **No modificar direcciones de retorno.**
> 2. **No auto-parchear código** (HVCI impone W^X en páginas de código).
> 3. **Buscar transformaciones de ofuscación fuertes** que respeten 1 y 2.

### ¿Puede un compilador "cifrar direcciones de retorno"?

**No.** Este es un malentendido común. Shadow Stack es impuesto por la CPU,
no por el SO, y es invisible al código en modo usuario. Si XOR-cifra la
dirección de retorno en la pila regular en el epílogo de su función:

```c
void my_func() {
    // ... cuerpo de función ...
    // el epílogo intenta cifrar la dirección de retorno:
    // XOR [rsp], 0xDEADBEEF
    // RET           <- el hardware compara pila regular vs shadow stack
                     //   ya no coinciden -> excepción #CP -> BUGCHECK
}
```

El shadow stack aún contiene la dirección de retorno original. RET dispara
una comparación de hardware; el desajuste dispara `#CP` y bugcheck del kernel.
El compilador **no puede** alcanzar el shadow stack:

- Modo usuario: ninguna instrucción puede escribir el shadow stack
- Modo kernel: `WRSSQ` es privilegiado, solo `ntoskrnl` lo usa

### Ofuscaciones amigables con CET que el compilador PUEDE hacer

| Transformación | Por qué es seguro con CET |
|----------------|---------------------------|
| **Aplanamiento de flujo de control** | El despachador switch usa CALL/JMP directo; los casos reciben ENDBR64 si es necesario |
| **Virtualización basada en VM** | Manejadores conectados vía JMP indirecto (con ENDBR64), no push+ret |
| **Cifrado de cadenas / constantes** | Pura transformación de datos, sin impacto en flujo de control |
| **Expresiones MBA** | `x + y → (x ^ y) + 2*(x & y)` — solo datos |
| **Predicados opacos** | Ramas condicionales vía saltos directos |
| **Clonación / inlining de funciones** | Sin cambio de semántica de pila de llamadas |
| **Sustitución de instrucciones** | `MOV → XOR + ADD` — sin efectos en la pila |

### Patrones hostiles a CET (mueren bajo KCET)

| Patrón | Por qué se rompe |
|--------|------------------|
| **Cifrado de dirección de retorno** | Desajuste de shadow stack → `#CP` |
| **PUSH addr; RET despachador** (estilo VMProtect / Themida clásico) | Igual — shadow stack no tiene entrada para `addr` |
| **Stack pivoting** (cadenas de gadgets ROP) | Shadow stack no puede seguir el pivote |
| **Código auto-modificable** | HVCI bloquea escrituras en páginas ejecutables |
| **Generación de código en tiempo de ejecución** | Igual — violación HVCI W^X |
| **Hooks inline basados en trampolín** | Modificar el prólogo de función dispara HVCI; incluso eludiendo HVCI, el shadow stack se rompe en el RET del trampolín |

### Por qué las herramientas bin2bin tienen una desventaja estructural

Un compilador emite código correcto para CET desde IR semántico. Una herramienta
bin2bin debe **redescubrir** la semántica desde bytes compilados:

1. **Ambigüedad de límite de instrucciones** — x86 es de longitud variable. Agregar ENDBR64 (4 bytes) en el offset incorrecto rompe todo el direccionamiento relativo a RIP y las reubicaciones.
2. **Identificación de objetivos indirectos** — bin2bin no siempre puede saber qué direcciones en `.data` son entradas de tabla de saltos vs datos brutos. O sobre-marca (inflado de código, nuevas semillas de gadgets ROP) o sub-marca (`#CP` en tiempo de ejecución).
3. **Peligro de auto-atestación** — Establecer `IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT` es una promesa. Si la salida bin2bin contiene cualquier patrón hostil a CET, el controlador cargará bien en máquinas no-CET pero BSOD al instante en hosts KCET.
4. **Completitud de CFG** — Los compiladores ven todo el grafo de llamadas; bin2bin debe inferirlo, y las llamadas indirectas sin objetivos precisos fuerzan una colocación conservadora de ENDBR.

### Estado de la industria

| Herramienta / clase | Estado CET |
|--------------------|-----------|
| **NeverC / Clang / MSVC (compiladores)** | Nativamente amigable con CET vía `-fcf-protection` + flag de enlazador |
| **OLLVM / Tigress / passes de NeverC** | Transformaciones a nivel de IR → naturalmente seguras con CET |
| **Microsoft Detours (4.0+)** | Actualizado para ser compatible con CET |
| **VMProtect / Themida (versiones antiguas)** | El despachador Push+RET mata el controlador en hosts KCET |
| **VMProtect / Themida (versiones nuevas)** | Agregando despachadores conscientes de ENDBR, soporte mixto |
| **Cargadores manual map / dump+rebuild** | Deben reconstruir todos los marcadores ENDBR — propenso a errores |

### Ángulo de seguridad de juegos

Los controladores anti-trampa (EAC, BattlEye, FACEIT AC, Vanguard) salen con
`--cetcompat` configurado, por lo que funcionan limpiamente en máquinas con
KCET activado. Los controladores de trampa — típicamente empaquetados,
hookeados o inyectados con trampolín vía herramientas bin2bin — luchan por
mantenerse conformes con CET. KCET + HVCI forman una **muralla de hardware
"amigable con compiladores, hostil a bin2bin"** que beneficia asimétricamente
al software de seguridad bien diseñado frente al código de estilo malware.

Esta es la razón más profunda por la que Microsoft impulsa KCET tan
fuertemente para software de kernel: hace que el código de kernel legítimo
sea más fácil de endurecer, mientras que hace el oficio de los atacantes
progresivamente más difícil.

---

## Activar KCET en la máquina de destino

```cmd
reg add HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity /v Enabled /t REG_DWORD /d 1 /f
reg add HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\KernelShadowStacks /v Enabled /t REG_DWORD /d 1 /f
```

Se requiere reinicio. Verifique con `msinfo32.exe`.

**Requisitos:** HVCI habilitado, Windows build 21389+, CPU con soporte CET (Intel Tiger Lake+ / AMD Zen 3+).

## Carga

```cmd
sc create CetDriver type= kernel binPath= C:\path\to\CetDriver.sys
sc start CetDriver
sc stop CetDriver
sc delete CetDriver
```

Active la firma de prueba o utilice un certificado de firma de código para producción.

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

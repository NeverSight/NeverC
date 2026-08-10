**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Sistema de ejecución integrado de NeverC](../README.es.md)

# Cifrado de cadenas en tiempo de compilación (`xorstr`)

## Descripción general

NeverC proporciona cifrado de cadenas de dos capas en tiempo de compilación para código C, diseñado para escenarios de seguridad donde las cadenas en texto plano (nombres de API, rutas del registro) no deben ser visibles en el binario compilado.

- **Capa 1 — Macro explícita**: `NC_XORSTR("string")` / `NEVERC_XORSTR("string")` para control preciso por cadena
- **Capa 2 — Pase IR automático**: `-fencrypt-call-strings` para cifrar automáticamente todos los argumentos de cadena en llamadas a funciones

Ambas capas usan búferes asignados en la pila (sin asignación en el montón), flujos de clave por instancia y limpieza volátil. En el límite de código máquina nativo, las llamadas al descodificador explícito de `NC_XORSTR` se recifran y se expanden directamente en cada punto de llamada; el objeto final no conserva una función descodificadora compartida.

---

## Inicio rápido

```c
#include <neverc/xorstr/xorstr.h>
FARPROC addr = GetProcAddress(hModule, NC_XORSTR("NtQuerySystemInformation"));
```

```bash
neverc -fencrypt-call-strings main.c -o main
```

---

## Flujo de protección

1. **Sema** cifra cada literal con una clave propia. La semilla `0` obtiene entropía nueva del sistema operativo; `-fstring-encrypt-key=` selecciona una salida determinista de 64 bits.
2. **IR intermedio / entrada LTO** conserva una llamada opaca y no especializable al descodificador para impedir que las optimizaciones vuelvan a materializar el texto plano.
3. **Límite final de código máquina** descifra y recifra el ciphertext del compilador, elige una forma de bucle por punto de llamada, lo expande allí y elimina el descodificador, su grafo auxiliar, el ancla ABI, el estado de ruta y los nombres semánticos.
4. **Limpieza** se instala antes de la optimización o del provider y otra vez en la cola final; la segunda ejecución es idempotente y repara la colocación tras cambios del CFG.

### Diversidad del descodificador

La secuencia de estado, las constantes, el ciphertext y las expresiones equivalentes por byte varían según la semilla y el punto de llamada. Una forma posible es `a + b − 2 × (a & b)`. Las cargas volátiles de estado/ciphertext dificultan el plegado de constantes y `nooutline` impide que Machine Outliner reconstruya un descodificador compartido después de la finalización IR.

Esto elimina una rutina única y estable que IDA pueda identificar o emular una sola vez. No implica que el texto plano necesario durante la ejecución sea irrecuperable mediante instrumentación dinámica.

---

## Cifrado automático y limpieza

`-fencrypt-call-strings` se ejecuta antes de IPO, después de la optimización ordinaria y de nuevo tras cada fase IR tardía ordinaria o proporcionada por plugins. LTO aplica el mismo sellado obligatorio después de los hooks de provider y pre-codegen.

Se procesan argumentos `CallBase` directos e indirectos procedentes de literales privados `unnamed_addr` propiedad del compilador; se conservan GEP, casts, `freeze`, `select`, PHI y slots locales de puntero promovibles. Se omiten intrínsecos, ensamblador inline, arrays visibles externamente o definidos por el usuario y literales demasiado grandes. Un literal protegido pasado por `musttail` hace fallar la compilación de forma segura.

`XorStrCleanupPass` borra el búfer completo con `memset` volátil antes de cada `ret`, `resume`, `cleanupret` que desenrolle al llamador y desenrollado `catchswitch` no capturado. El almacenamiento inseguro o no trazable por completo se rechaza en vez de limpiarse parcialmente.

---

## Referencia de flags del compilador

| Flag | Descripción |
|------|-------------|
| `-fencrypt-call-strings` | Habilitar cifrado automático de cadenas |
| `-fno-encrypt-call-strings` | Deshabilitar cifrado automático |
| `-fencrypt-call-strings-max-len=N` | Longitud máxima en bytes (predeterminado: 1024) |
| `-fstring-encrypt-key=0xHEX` | Sobrescribir la semilla completa de 64 bits; `0` usa entropía nueva |

## Límites de salida y reproducibilidad

- `-fno-lto` finaliza durante la generación nativa del frontend.
- Auto-LTO y Full LTO conservan el descodificador opaco en el bitcode pre-link y lo recifran/expanden después de la optimización global y de plugins.
- Las pipelines sustituidas por providers y los pases tardíos de plugins siempre terminan con cifrado, limpieza y finalización obligatorios.
- Con la semilla predeterminada, compilaciones nativas independientes difieren; se evitan las cachés whole-link y de partición que pudieran reutilizar código protegido anterior.
- Una semilla distinta de cero es deliberadamente determinista y cacheable: la misma entrada y la misma semilla completa de 64 bits producen el mismo código protegido.
- `-emit-llvm` y el bitcode pre-link son artefactos intermedios y conservan intencionadamente la ABI opaca. La garantía de «sin descodificador compartido» se aplica al código máquina final generado correctamente.

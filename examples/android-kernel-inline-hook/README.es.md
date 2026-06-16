**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Hook inline kernel Android

Hook inline en `do_faccessat`. Por defecto: reemplazo simple con trampolín. Con `-DNVK_CONTEXT_HOOK`: hook de contexto que recibe el estado completo de registros `nvk_reg_ctx`. Demuestra parcheo seguro BTI/PAC, reubicación relativa al PC y trampolín coherente D-cache→I-cache.

## Modos de hook

| | Simple Hook (predeterminado) | Context Hook (`-DNVK_CONTEXT_HOOK`) |
|---|---|---|
| **Firma** | Typedef exacto requerido | No necesario — vía `ctx->regs[0..7]` |
| **Guardia de reentrada** | Manual (`nvk_hook_enter`/`leave`) | Integrado (`guard_task`) |
| **Activar/desactivar** | Manual (`WRITE_ONCE`) | Verificación rápida integrada en stub |
| **Llamar original** | Vía puntero `orig` | Automático (después del handler) |
| **Omitir original** | No llamar a `orig` | `NVK_CTX_SKIP(ctx, ret)` |
| **Redirigir** | N/A | `NVK_CTX_REDIRECT(ctx, addr)` |
| **Modificar args** | Cambiar parámetros antes de `orig` | `NVK_CTX_SET_ARG(ctx, n, val)` |
| **Seguridad FP** | Convención caller-save | `NVK_CTX_FP_GUARD_BEGIN`/`END` |
| **Costo** | Bajo (4 insns patch + trampoline) | Mayor (116 insns stub + guardado completo) |
| **Ideal para** | Firmas conocidas, rendimiento crítico | Monitoreo, ABI inestable, prototipado rápido |

**Recomendación**: prefiera context hook a menos que necesite interceptar el valor de retorno o tenga restricciones de rendimiento estrictas.

## Compilación

```bash
cd examples/android-kernel-inline-hook
neverc make
```

Cambie `KERNEL` a `515`, `601`, `606` o `612` para otras versiones.

## Despliegue y ejecución

```bash
neverc make run
```

O manualmente:

```bash
adb push nvk_inline_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_inline_hook.ko'
adb shell su -c 'dmesg | grep nvk_inline_hook'
```

## Descarga

```bash
neverc make rmmod
```

O manualmente:

```bash
adb shell su -c 'rmmod nvk_inline_hook'
```

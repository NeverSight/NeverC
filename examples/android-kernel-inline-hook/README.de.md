**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Inline Hook

Inline Hook auf `do_faccessat`. Standard: einfache Ersetzung mit Trampoline. Mit `-DNVK_CONTEXT_HOOK`: Kontext-Hook mit vollständigem `nvk_reg_ctx` Registerzustand. Demonstriert BTI/PAC-sicheres Patching, PC-relative Relokation und D-Cache→I-Cache-kohärentes Trampoline.

## Hook-Modi

| | Simple Hook (Standard) | Context Hook (`-DNVK_CONTEXT_HOOK`) |
|---|---|---|
| **Signatur** | Exakte Typedef erforderlich | Nicht nötig — über `ctx->regs[0..7]` |
| **Reentrance-Guard** | Manuell (`nvk_hook_enter`/`leave`) | Eingebaut (`guard_task`) |
| **Aktivieren/Deaktivieren** | Manuell (`WRITE_ONCE`) | Eingebauter Schnellcheck im Stub |
| **Original aufrufen** | Über `orig`-Funktionszeiger | Automatisch (nach Handler) |
| **Original überspringen** | `orig` nicht aufrufen | `NVK_CTX_SKIP(ctx, ret)` |
| **Umleitung** | N/A | `NVK_CTX_REDIRECT(ctx, addr)` |
| **Argumente ändern** | Parameter vor `orig`-Aufruf ändern | `NVK_CTX_SET_ARG(ctx, n, val)` |
| **FP-Sicherheit** | Caller-Save-Konvention | `NVK_CTX_FP_GUARD_BEGIN`/`END` |
| **Overhead** | Niedrig (4 Insns Patch + Trampoline) | Höher (116 Insns Stub + vollst. Reg.-Sicherung) |
| **Geeignet für** | Bekannte Signaturen, perf.-kritisch | Monitoring, instabile ABI, Rapid Prototyping |

**Empfehlung**: Context Hook bevorzugen, es sei denn, Sie müssen den Rückgabewert abfangen oder haben strenge Performance-Anforderungen.

## Kompilierung

```bash
cd examples/android-kernel-inline-hook
neverc make
```

Ändern Sie `KERNEL` auf `515`, `601`, `606` oder `612` für andere Versionen.

## Bereitstellung und Ausführung

```bash
neverc make run
```

Oder manuell:

```bash
adb push nvk_inline_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_inline_hook.ko'
adb shell su -c 'dmesg | grep nvk_inline_hook'
```

## Entladen

```bash
neverc make rmmod
```

Oder manuell:

```bash
adb shell su -c 'rmmod nvk_inline_hook'
```

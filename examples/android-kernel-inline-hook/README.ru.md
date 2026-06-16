**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Инлайн-хук ядра Android

Инлайн-хук на `do_faccessat`. По умолчанию: простая замена с трамплином. С `-DNVK_CONTEXT_HOOK`: контекстный хук с полным состоянием регистров `nvk_reg_ctx`. Демонстрирует BTI/PAC-безопасный патчинг, PC-относительную релокацию и когерентный D-cache→I-cache трамплин.

## Режимы хука

| | Simple Hook (по умолчанию) | Context Hook (`-DNVK_CONTEXT_HOOK`) |
|---|---|---|
| **Сигнатура** | Требуется точный typedef | Не требуется — через `ctx->regs[0..7]` |
| **Защита от реентера** | Ручная (`nvk_hook_enter`/`leave`) | Встроенная (`guard_task`) |
| **Вкл./выкл.** | Вручную (`WRITE_ONCE`) | Встроенная быстрая проверка в stub |
| **Вызов оригинала** | Через указатель `orig` | Автоматически (после обработчика) |
| **Пропуск оригинала** | Не вызывать `orig` | `NVK_CTX_SKIP(ctx, ret)` |
| **Перенаправление** | Н/Д | `NVK_CTX_REDIRECT(ctx, addr)` |
| **Изменение аргументов** | Изменить параметры перед `orig` | `NVK_CTX_SET_ARG(ctx, n, val)` |
| **Безопасность FP** | Соглашение caller-save | `NVK_CTX_FP_GUARD_BEGIN`/`END` |
| **Накладные расходы** | Низкие (4 insns patch + trampoline) | Выше (116 insns stub + полное сохранение) |
| **Подходит для** | Известные сигнатуры, критичная произв. | Мониторинг, нестабильный ABI, прототипирование |

**Рекомендация**: используйте context hook, если не требуется перехватывать возвращаемое значение или нет жёстких требований к производительности.

## Сборка

```bash
cd examples/android-kernel-inline-hook
neverc make
```

Измените `KERNEL` на `515`, `601`, `606` или `612` для других версий.

## Развёртывание и запуск

```bash
neverc make run
```

Или вручную:

```bash
adb push nvk_inline_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_inline_hook.ko'
adb shell su -c 'dmesg | grep nvk_inline_hook'
```

## Выгрузка

```bash
neverc make rmmod
```

Или вручную:

```bash
adb shell su -c 'rmmod nvk_inline_hook'
```

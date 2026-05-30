**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Драйвер ядра Windows с плавающей точкой

Драйвер ядра WDM, собранный с помощью NeverC, демонстрирующий **безопасное
использование операций с плавающей точкой / SIMD в режиме ядра**.
Кросс-компиляция с macOS / Linux.

## Сборка

```bash
cd examples/windows-driver-float
make
```

Из автономной сборки NeverC:

```bash
make NEVERC=/path/to/neverc
```

Результат — `FloatDriver.sys` (оптимизирован auto-LTO).
Сборка по умолчанию включает `-g` для отладки; удалите `-g` для релизных сборок.

---

## Две проблемы для решения

Плавающая точка в режиме ядра имеет две независимые проблемы:

### Проблема 1 — ABI-маркер `_fltused` (время компиляции/линковки)

Компилятор MSVC выдаёт неопределённую ссылку на символ `_fltused` всякий
раз, когда единица трансляции выполняет любую операцию с плавающей точкой.
В программах пользовательского режима `libcmt.lib` предоставляет этот
символ, удовлетворяя линкер и подтягивая некоторые специфичные для FP
части CRT.

Драйверы ядра **не** линкуются с `libcmt` (мы передаём `-nostdlib` и
`-Xlinker --nodefaultlib`), поэтому неразрешённый `_fltused` вызвал бы
ошибку линковки.

**Как NeverC это решает**: с `-fms-kernel` X86-бэкенд LLVM определяет
`_fltused` локально как 0. Это видно в сгенерированном ассемблере:

```asm
# Цель пользовательского режима:
    .globl  _fltused              # внешняя ссылка -- требуется libcmt
```

```asm
# Цель -fms-kernel:
    .globl  _fltused
    .set    _fltused, 0           # локальное определение! внешний символ не требуется
```

Таким образом, вам **никогда не нужно вручную писать `int _fltused = 0;`** в драйвере.

### Проблема 2 — ядро НЕ сохраняет регистры FP/SIMD (время выполнения)

Ядро Windows **не** сохраняет/восстанавливает регистры x87 / XMM / YMM / ZMM
при переключении контекста по умолчанию. Если драйвер трогает любой из них
из произвольного кода ядра, он молча испортит SIMD-состояние потока
пользовательского режима, который оказался на CPU.

**Решение**: оборачивайте каждую область с плавающей точкой / SIMD с помощью
[`KeSaveExtendedProcessorState`](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kesaveextendedprocessorstate)
и `KeRestoreExtendedProcessorState`:

```c
XSTATE_SAVE save;
NTSTATUS status = KeSaveExtendedProcessorState(XSTATE_MASK_LEGACY, &save);
if (!NT_SUCCESS(status))
    return status;

// ... ваш FP / SIMD код здесь ...

KeRestoreExtendedProcessorState(&save);
```

### Маски XSTATE

| Маска | Покрытие |
|-------|---------|
| `XSTATE_MASK_LEGACY_FLOATING_POINT` (бит 0) | стек x87 |
| `XSTATE_MASK_LEGACY_SSE` (бит 1) | XMM0–15 |
| `XSTATE_MASK_LEGACY` | бит 0 \| бит 1 (покрывает большую часть обычного `double` / SSE кода) |
| `XSTATE_MASK_GSSE` / AVX (бит 2) | верхние половины YMM0–15 |
| `XSTATE_MASK_AVX512` | регистры AVX-512 ZMM |

Передайте OR-комбинированную маску, соответствующую самым широким регистрам, которые использует ваш код.

---

## Что делает этот драйвер

- Создаёт объект устройства в `\Device\FloatDriver` и символическую ссылку в `\DosDevices\FloatDriver`
- В `DriverEntry` вызывает `ComputeAreaSafe()` (которая оборачивает
  `ComputeArea()` сохранением/восстановлением состояния FP) дважды с
  `radius=1.0` и `radius=5.0`
- Выводит сырые биты double через `DbgPrint` (так как `%f` не поддерживается
  `DbgPrint` — мы используем `RtlCopyMemory` для извлечения 64-битного шаблона)
- Неявно определяет `_fltused` через `-fms-kernel`

## Проверка генерации `_fltused`

Сравните вывод компилятора с и без `-fms-kernel`:

```bash
# Пользовательский режим (потребовался бы libcmt):
neverc --target=x86_64-pc-windows-msvc -S /tmp/foo.c -o - | grep fltused
#  .globl  _fltused

# Ядро (локально определено как 0):
neverc --target=x86_64-pc-windows-msvc -fms-kernel -S /tmp/foo.c -o - | grep fltused
#  .globl  _fltused
#  .set    _fltused, 0
```

## Загрузка (на тестовой машине Windows)

```cmd
sc create FloatDriver type= kernel binPath= C:\path\to\FloatDriver.sys
sc start FloatDriver
sc stop FloatDriver
sc delete FloatDriver
```

Включите тестовую подпись или используйте сертификат подписи кода для продакшена.

## Предостережения

- **`%f` не работает с `DbgPrint`** — процедура отладочного вывода ядра
  не имеет форматирования с плавающей точкой. Преобразуйте double в целое
  с фиксированной точкой для отображения, или выведите сырые биты, как в этом примере.
- **Не используйте плавающую точку при IRQL ≥ DISPATCH_LEVEL**, если это
  не абсолютно необходимо. `KeSaveExtendedProcessorState` документирует
  ограничения IRQL.
- **Производительность**: сохранение/восстановление состояния не бесплатно;
  для горячих путей рассмотрите возможность объединения FP-работы в одну
  обёрнутую область.

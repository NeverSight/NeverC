**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Пользовательские соглашения о вызовах

NeverC поддерживает **управляемые данными пользовательские соглашения о вызовах** — вы можете назначать произвольные физические регистры аргументам и возвращаемым значениям любой функции, полностью из внешнего плагина или атрибутов исходного кода, без изменения компилятора или определений TableGen.

## Обзор

Традиционные соглашения о вызовах LLVM жёстко закодированы в бэкенде через файлы `.td` / `.inc`. NeverC заменяет это подходом, **управляемым данными во время выполнения**:

- **Спецификация назначения регистров** (строка) прикрепляется к каждой функции как строковый атрибут.
- Бэкенд читает эту спецификацию и назначает параметры/возвращаемые значения указанным физическим регистрам.
- Спецификация может быть предоставлена **внешним плагином** (IR-проход), **атрибутами исходного кода** (`__attribute__` / `__declspec`) или обоими способами.

## Формат спецификации

Строка, разделённая точками с запятой. Каждый сегмент содержит ключ и список имён регистров через запятую (регистронезависимый, пробелы допускаются):

```
gpr:rcx,rdx,r8,r9; xmm:xmm0,xmm1; ret:rax; ret_xmm:xmm0
```

| Сегмент | Псевдоним | Значение |
|---|---|---|
| `args` | | **Позиционный режим**: каждый токен — имя регистра или `stack`/`mem` |
| `gpr` | `arg_gpr` | **Режим пула**: регистры целочисленных/указательных аргументов |
| `xmm` | `arg_xmm` | **Режим пула**: регистры аргументов с плавающей точкой/векторные |
| `ret_gpr` | `ret` | Регистры возвращаемого значения целочисленные/указательные |
| `csr` | | Пользовательский набор callee-saved регистров |

### Поддерживаемые архитектуры

| Архитектура | GPR | SIMD | Выбор ширины |
|---|---|---|---|
| **x86-64** | `rax`, `rcx`, `rdx`, `rsi`, `rdi`, `r8`–`r11` | `xmm0`–`xmm15` | i32→32 бит, i64→64 бит |
| **AArch64** | `x0`–`x28` | `v0`–`v31` | i32→`w`, i64→`x`, f32→`s`, f64→`d` |

## Использование

### 1. Управление плагином (рекомендуется)

```bash
cd pluginsdk/examples && make CustomCallConvPlugin.dylib
# Режим атрибутов (по умолчанию)
neverc -fplugin-pass=./CustomCallConvPlugin.dylib input.c -o output.o
# Глобальный режим
neverc -fplugin-pass=./CustomCallConvPlugin.dylib \
       -fplugin-pass-arg=cc-all=1 \
       -fplugin-pass-arg=ccspec="gpr:r10,r11,rsi;ret:rdx" \
       input.c -o output.o
```

### 2. Атрибуты исходного кода

```c
__attribute__((custom_attr("neverc-callconv", "gpr:r10,r11,rsi;ret:rdx")))
int add3(int a, int b, int c) { return a + b + c; }

__declspec(custom_attr("neverc-callconv", "gpr:r10;ret:rdx"))
int msfunc(int a) { return a; }
```

## Поддержка LTO

Плагин регистрируется в `NEVERC_HOOK_POST_OPT` и `NEVERC_HOOK_LTO_POST_OPT`, обеспечивая применение пользовательских соглашений после объединения LTO.

## API плагина

```c
API->FunctionSetCustomCallConv(F, "gpr:r10,r11,rsi;ret:rdx");
```

Устанавливает `CallingConv::NeverC_Custom` (CC 1000), записывает атрибут и **синхронизирует все прямые точки вызова**. Передача `NULL` или `""` очищает соглашение.

## Тесты

Набор GoogleTest (18 тестов, все PASS):

```bash
ninja -C build-neverc neverc-tests
build-neverc/bin/neverc-tests --gtest_filter='CustomCallConvTest.*'
```

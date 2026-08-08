**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Примеры NeverC](../../docs/examples/README.ru.md)

# Пример Windows Ring3 DLL

DLL Windows пользовательского режима, кросс-компилированная с NeverC.

## Сборка

```bash
cd examples/windows-dll
neverc make          # debug: -g (по умолчанию при первой сборке)
neverc make release  # release: -O2 --strip
neverc make debug    # вернуться к debug
```

Makefile сохраняет `PROFILE`, поэтому последующие `neverc make`
оставляют тот же выбор debug/release. Release использует встроенный
`--strip` NeverC: удаляет отладочные метаданные и ненужные статические
имена символов, сохраняя нужные имена загрузчика/динамического ABI.
См. [Релизные сборки](../../docs/release-builds/README.ru.md).

## Ручная сборка

```bash
neverc --target=x86_64-pc-windows-msvc -Wall -shared -Xlinker --entry=DllMain -Xlinker --subsystem=windows -lkernel32 -luser32 -o example.dll dllmain.c
```

## Функциональность

- Экспортирует обёртки для кросс-процессного доступа к памяти
- Перечисление процессов/модулей
- Хелпер XOR-шифрования буфера


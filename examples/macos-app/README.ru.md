**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Пример приложения macOS

Нативный исполняемый файл macOS Mach-O, кросс-скомпилированный с помощью NeverC. Демонстрирует sysctl, uname и API ядра Mach для получения информации о системе и процессах. Сборка из macOS, Windows или Linux — Xcode не требуется.

## Сборка

Из репозитория (цель по умолчанию: `arm64-apple-macos`):

```bash
cd examples/macos-app
neverc make
```

Сборка для Intel:

```bash
neverc make TARGET=x86_64-apple-macos
```

С использованием автономной версии NeverC:

```bash
neverc make NEVERC=/path/to/neverc
```

## Ручная сборка (без Make)

```bash
neverc --target=arm64-apple-macos -Wall -o macos-app main.c
```

## Запуск

```bash
./macos-app
```

## Возможности

- Получение информации о ядре через `uname`
- Чтение данных об оборудовании через `sysctl` (модель, количество CPU, объём памяти, размер страницы)
- Отображение идентификации процесса (`getpid`, `getppid`, `getuid`)
- Получение информации о хосте Mach (`host_info`) и статистики памяти задачи (`task_info`)

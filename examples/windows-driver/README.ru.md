**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Примеры NeverC](../../docs/examples/README.ru.md)

# Пример драйвера ядра Windows

Минимальный драйвер ядра WDM, собранный с помощью NeverC. По умолчанию нацелен
на **x64**, также может быть собран для ARM64. Кросс-компиляция с macOS / Linux.

NeverC — это универсальный компилятор: один вызов выполняет препроцессинг,
компиляцию, оптимизацию (auto-LTO) и линковку через встроенный линкер.

## Сборка

Из репозитория:

```bash
cd examples/windows-driver
neverc make
```

Это создаёт `ExampleDriver-x64.sys`. Чтобы собрать для ARM64 или для обеих архитектур:

```bash
neverc make ARCH=arm64
neverc make all-arch
```

Из автономной сборки NeverC:

```bash
neverc make NEVERC=/path/to/neverc
```

Результат — `ExampleDriver-<арх>.sys` (оптимизирован auto-LTO).
Сборка по умолчанию включает `-g` для отладки; **в релизных сборках следует убрать
`-g`**, чтобы удалить отладочные символы и уменьшить размер бинарного файла
(~38 КБ → ~3 КБ).

## Ручная сборка (без Make)

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fms-kernel \
  -Wall -nostdlib -shared \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -lntoskrnl -lhal \
  -o ExampleDriver-x64.sys driver.c
```

Для ARM64 достаточно заменить цель на `aarch64-pc-windows-msvc`; остальное не
меняется. `-fms-kernel` подбирает заголовки и библиотеки импорта WDK,
соответствующие цели, и определяет ожидаемые WDK макросы архитектуры, так что
передавать их вручную не нужно.

> `-g` встраивает отладочную информацию DWARF в PE; проверяйте с помощью
> `llvm-dwarfdump`. В релизных сборках опускайте эту опцию для уменьшения
> размера бинарного файла.

## Тестовая подпись

Windows отказывается загружать неподписанный драйвер ядра. `-ftest-sign`
добавляет подпись Authenticode, чтобы образ прошёл эту проверку на тестовой
машине:

```bash
neverc make TESTSIGN=1
neverc make ARCH=arm64 TESTSIGN=1
```

либо добавьте `-ftest-sign` при ручном вызове. Опция принимается только вместе
с `-fms-kernel`, поскольку тестовая подпись ничего не значит для бинарного
файла пользовательского режима.

Подписывающая личность встроена в компилятор — самоподписанный сертификат,
закрытый ключ которого публичен по построению. Он не даёт никакой
подлинности; он лишь удовлетворяет проверку целостности кода на машине,
которую вы намеренно открыли. Настройте эту машину один раз, от имени
администратора:

```cmd
bcdedit /set testsigning on
certutil -addstore Root neverc-test-signing.cer
certutil -addstore TrustedPublisher neverc-test-signing.cer
```

затем перезагрузитесь. Сертификат находится в `utils/neverc-test-signing.cer`.

**Никогда не используйте это для того, что покидает тестовую машину.** Для
продакшена подписывайте настоящим сертификатом подписи кода (а для Windows 10
1607 и новее — ещё и аттестационной подписью Microsoft Hardware Dev Center).

## Функциональность

- Создаёт объект устройства в `\Device\ExampleDriver`
- Создаёт символическую ссылку в `\DosDevices\ExampleDriver`
- Обрабатывает `IRP_MJ_CREATE`, `IRP_MJ_CLOSE`, `IRP_MJ_DEVICE_CONTROL`
- Выводит сообщения о загрузке/выгрузке через `DbgPrint`

## Загрузка (на тестовой машине Windows)

```cmd
sc create ExampleDriver type= kernel binPath= C:\path\to\ExampleDriver-x64.sys
sc start ExampleDriver
sc stop ExampleDriver
sc delete ExampleDriver
```

Включите тестовую подпись или используйте сертификат подписи кода для продакшена.

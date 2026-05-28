# Пример драйвера ядра Windows

Минимальный драйвер ядра WDM, собранный с помощью NeverC. Кросс-компиляция с macOS / Linux.

NeverC — это универсальный компилятор: один вызов выполняет препроцессинг,
компиляцию, оптимизацию (auto-LTO) и линковку через встроенный линкер.

## Сборка

Из репозитория:

```bash
cd examples/windows-driver
make
```

Из автономной сборки NeverC:

```bash
make NEVERC=/path/to/neverc
```

Результат — `ExampleDriver.sys` (оптимизирован auto-LTO).
Сборка по умолчанию включает `-g` для отладки; **в релизных сборках следует убрать
`-g`**, чтобы удалить отладочные символы и уменьшить размер бинарного файла
(~38 КБ → ~3 КБ).

## Ручная сборка (без Make)

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fms-kernel -fms-extensions -fms-compatibility \
  -D_AMD64_ -DNTDDI_VERSION=0x06010000 -D_WIN32_WINNT=0x0601 \
  -Wall -nostdlib -shared \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -lntoskrnl -lhal \
  -o ExampleDriver.sys driver.c
```

> `-g` встраивает отладочную информацию DWARF в PE; проверяйте с помощью
> `llvm-dwarfdump`. В релизных сборках опускайте эту опцию для уменьшения
> размера бинарного файла.

## Функциональность

- Создаёт объект устройства в `\Device\ExampleDriver`
- Создаёт символическую ссылку в `\DosDevices\ExampleDriver`
- Обрабатывает `IRP_MJ_CREATE`, `IRP_MJ_CLOSE`, `IRP_MJ_DEVICE_CONTROL`
- Выводит сообщения о загрузке/выгрузке через `DbgPrint`

## Загрузка (на тестовой машине Windows)

```cmd
sc create ExampleDriver type= kernel binPath= C:\path\to\ExampleDriver.sys
sc start ExampleDriver
sc stop ExampleDriver
sc delete ExampleDriver
```

Включите тестовую подпись или используйте сертификат подписи кода для продакшена.

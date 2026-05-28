# Драйвер ядра Windows с CET Shadow Stack

Минимальный драйвер ядра WDM, собранный с помощью NeverC, с включённым
Intel CET (Control-flow Enforcement Technology) Kernel Shadow Stack.
Кросс-компиляция с macOS / Linux.

## Сборка

```bash
cd examples/windows-driver-cet
make
```

Из автономной сборки NeverC:

```bash
make NEVERC=/path/to/neverc
```

Результат — `CetDriver.sys` (оптимизирован auto-LTO).
Сборка по умолчанию включает `-g` для отладки; **в релизных сборках следует убрать
`-g`**, чтобы удалить отладочные символы и уменьшить размер бинарного файла.

## Флаги, специфичные для CET

| Флаг | Уровень | Назначение |
|------|---------|-----------|
| `-fcf-protection=return` | Компилятор | Генерация кода, совместимого с Shadow Stack |
| `-Xlinker --cetcompat` | Линкер | Установка `IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT` в PE |

## Ручная сборка (без Make)

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fcf-protection=return \
  -fms-kernel -fms-extensions -fms-compatibility \
  -D_AMD64_ -DNTDDI_VERSION=0x06010000 -D_WIN32_WINNT=0x0601 \
  -Wall -nostdlib -shared \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -Xlinker --cetcompat \
  -lntoskrnl -lhal \
  -o CetDriver.sys driver.c
```

## Функциональность

- Создаёт объект устройства в `\Device\CetDriver`
- Создаёт символическую ссылку в `\DosDevices\CetDriver`
- Выполняет непрямые вызовы (указатель на функцию `ComputeFn`) для проверки совместимости с CET — Shadow Stack защищает адреса возврата этих вызовов
- Выводит сообщения о загрузке/выгрузке через `DbgPrint`

---

## Технические детали CET

CET имеет **два независимых механизма защиты**:

### 1. Shadow Stack — защита обратного края (RET)

Аппаратное обеспечение поддерживает второй стек (shadow stack), зеркалирующий операции CALL/RET.
**Специальные инструкции на входе в функцию не требуются** — полностью прозрачно:

```
┌─ CALL target ─────────────────────────────────┐
│                                                │
│  Обычный стек:  PUSH return_addr  (RSP)        │
│  Shadow stack:  PUSH return_addr  (SSP, HW)    │
│                                                │
└────────────────────────────────────────────────┘

┌─ RET ─────────────────────────────────────────┐
│                                                │
│  Обычный стек:  POP return_addr_A  (RSP)       │
│  Shadow stack:  POP return_addr_B  (SSP, HW)   │
│                                                │
│  Сравнение: return_addr_A == return_addr_B ?    │
│    ✓ совпадение → нормальный возврат            │
│    ✗ несовпадение → исключение #CP              │
│                                                │
└────────────────────────────────────────────────┘
```

### 2. Indirect Branch Tracking (IBT) — защита переднего края (непрямой CALL/JMP)

Требует инструкцию `ENDBR64` (`F3 0F 1E FA`, 4 байта) на каждой допустимой цели непрямого вызова/перехода. На CPU без CET `ENDBR64` является NOP.

### Выбор ядра Windows

| Защита | Механизм | Используется ядром Windows? |
|--------|---------|---------------------------|
| Обратный край (RET) | CET Shadow Stack | **Да** (KCET) |
| Передний край (непрямой CALL/JMP) | CET IBT (ENDBR) | **Нет** — используется CFG |

### Сравнение ассемблера: режимы `-fcf-protection`

Исходный код:

```c
unsigned long rotate13(unsigned long val) {
    return (val << 13) | (val >> 19);
}
```

#### `-fcf-protection=none` (без CET)

```asm
rotate13:
    mov  eax, ecx
    rol  eax, 13
    ret
```

#### `-fcf-protection=return` (только Shadow Stack — этот пример использует данный режим)

```asm
rotate13:
    mov  eax, ecx      ; идентично "none"!
    rol  eax, 13        ; Shadow Stack полностью прозрачен
    ret
```

#### `-fcf-protection=full` (Shadow Stack + IBT)

```asm
rotate13:
    endbr64             ; ← маркер IBT (F3 0F 1E FA)
    mov  eax, ecx
    rol  eax, 13
    ret
```

---

## Активация KCET на целевой машине

```cmd
reg add HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity /v Enabled /t REG_DWORD /d 1 /f
reg add HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\KernelShadowStacks /v Enabled /t REG_DWORD /d 1 /f
```

Требуется перезагрузка. Проверьте через `msinfo32.exe`.

**Требования:** HVCI включён, Windows сборка 21389+, CPU с поддержкой CET (Intel Tiger Lake+ / AMD Zen 3+).

## Загрузка

```cmd
sc create CetDriver type= kernel binPath= C:\path\to\CetDriver.sys
sc start CetDriver
sc stop CetDriver
sc delete CetDriver
```

Включите тестовую подпись или используйте сертификат подписи кода для продакшена.

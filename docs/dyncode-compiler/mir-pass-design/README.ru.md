**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Компилятор dyncode](../README.ru.md)

# Проектирование проходов MIR — Принципы и точки хуков

> Документ-компаньон к [ir-pass-design.md](../ir-pass-design/README.ru.md). Уровень IR устраняет конструкции, которые на уровне IR явно порождают релокации. Уровень MIR служит **страховочной сетью** после выбора инструкций и распределения регистров: удаляет введённые codegen псевдоинструкции/метаданные и открывает точки хуков для сторонних проходов обфускации, выполняющих финальные преобразования на уровне инструкций.
>
> Реализация: `neverc/lib/DynCode/MIR/MIRPrepPass.cpp` + `Pipeline.cpp`.
> Интерфейс хуков: [`neverc/include/neverc/DynCode/Pipeline/Pipeline.h`].

---

## 0. Зачем нужен уровень MIR

Уровень IR уже устранил:
- Константные GV → стек-ификация / непосредственные значения (Data2TextPass)
- `memcpy` / `memset` / `str*` / `abs*` → встроенные байтовые циклы (MemIntrinPass)
- Помощники compiler-rt `__int128` → встроенные always_inline (CompilerRtPass)
- Внешние syscall libc → встроенные svc / syscall (SyscallStubPass)
- Внешние Win32 → PEB walk + хеш экспорта (WinPEBImportPass)
- Изменяемые глобальные → входной кадр стека (ZeroRelocPass)
- Computed goto → switch (IndirectBrPass)
- Опционально: прямой вызов → косвенный вызов (AllBlrPass)

Но бэкенд LLVM вводит дополнительные конструкции во время **понижения IR → MIR**, которые dyncode не может принять:

1. **Псевдоинструкции CFI / EH_LABEL**: генерируются при `-g` или включённой по умолчанию информации unwind, порождая `__compact_unwind` (Mach-O) / `.eh_frame` (ELF) / `.pdata + .xdata` (COFF).
2. **Стабы XRay / patchable function**: `-fxray-instrument` или `-fpatchable-function-entry` вставляют `PATCHABLE_FUNCTION_ENTER` и подобные.
3. **Метаданные санитайзеров**: StackMap / PatchPoint / StateMap / PseudoProbe.
4. **Фиксапы MC-уровня бэкенда**: например, ссылки GOT arm64 Windows, невидимые на уровне IR.

Кроме того, хуки MIR служат критической цели: **включение сторонней обфускации на уровне инструкций** (подстановка инструкций, переименование регистров), которую IR выразить не может (в IR есть только виртуальные регистры и абстрактные инструкции).

---

## 1. Интеграция с LLVM (нативные хуки)

`TargetPassConfig` в LLVM имеет глобальный список колбэков. `addMachinePasses()` вызывает каждый колбэк после `addPass(&PatchableFunctionID)` и перед `addPreEmitPass()`. Мы добавили публичную обёртку `addExternalPass(Pass *P)` для решения проблем контроля доступа к защищённому `addPass()`.

Регистрация в `Pipeline.cpp`:

```cpp
ListRegisterTargetPassConfigCallbacks.push_back(
    [](TargetPassConfig &TPC) {
      const DynCodeOptions &Opts = currentDynCodeOptionsStorage();
      const ObfuscationInterposes &H = getDynCodeObfuscationInterposes();
      runMIRInterpose(H.RunBeforePreEmit, TPC, Opts);
      TPC.addExternalPass(createDynCodeMIRPrepPass(Opts));
      runMIRInterpose(H.RunAfterPreEmit, TPC, Opts);
    });
```

Колбэк не захватывает `Opts`. Он читает текущий снимок `DynCodeOptions` во время выполнения, предотвращая устаревшую конфигурацию, когда один процесс компилирует и dyncode, и обычный C.

---

## 2. Встроенный MIRPrepPass

Кроссплатформенный, единая ответственность: сканирует каждый `MachineBasicBlock` и удаляет три категории псевдоинструкций. Настоящие машинные инструкции (`MOV` / `BL` / `ADRP` / `SYSCALL` / ...) **никогда не затрагиваются**.

### 2.1 Метаданные боковых секций (через `TargetOpcode::*`, кроссплатформенно)

| Opcode | Источник | Если не удалено |
|--------|----------|-----------------|
| `CFI_INSTRUCTION` | frame-lowering всех платформ / `-g` | `.eh_frame` / `__compact_unwind` / `.pdata` непусты |
| `EH_LABEL` | Точки EH / try-catch setjmp | Боковая секция LSDA непуста |
| `GC_LABEL` / `ANNOTATION_LABEL` | Маркеры GC / annotation | MCSymbol с метаданными относительно секции |
| `STATEPOINT` / `STACKMAP` / `PATCHPOINT` | Stackmap GC / sandbox | Боковая секция `.llvm_stackmaps` |
| `PSEUDO_PROBE` | `-fprofile-sample-use` | Боковая секция `.pseudo_probe` |
| Семейство `PATCHABLE_*` | Стабы XRay / Kcov | `.xray_instr_map` / `.xray_fn_idx` |
| `FENTRY_CALL` | Входной зонд `-mfentry` | Внешний вызов `__fentry__` |
| `LOCAL_ESCAPE` | Frame-escape SEH Microsoft | Тянет `_local_unwind2` / `__except_handler3` |
| `JUMP_TABLE_DEBUG_INFO` | Отладочная информация jump table | Запись `.debug_rnglists` |

### 2.2 Windows SEH (сопоставляется по префиксу `TargetInstrInfo::getName()`)

Псевдо SEH Windows — это target-специфичные опкоды, определённые в TD бэкендов AArch64/X86 (~20 инструкций вроде `SEH_StackAlloc`, `SEH_PushReg` и т.д.). Чтобы держать проход MIR **кроссплатформенным без включения заголовков бэкенда**, мы используем сопоставление по префиксу строки:

```cpp
StringRef Name = TII->getName(Opcode);
if (Name.starts_with("SEH_"))
  eraseFromParent();
```

### 2.3 Таблица перезаписи инструкций (`MIRRewritePatterns.def`)

После удаления псевдо `MIRPrepPass` выполняет проход перезаписи, чтобы заменить выбранные codegen, но не дружественные dyncode паттерны инструкций эквивалентными dyncode-дружественными формами, не изменяя файлы `.td` LLVM.

Два зарегистрированных паттерна:

1. **`aarch64-cpi-fp-to-fmov-imm`**: `ADRP + LDRSui/LDRDui [base, #:lo12:CPI]` → `FMOV Sd/Dd, #imm8`, когда битовый паттерн IEEE попадает в 256 кодируемых значений FMOV.
2. **`x86-cpi-zero-fp-to-xorps`**: `movss/movsd xmm, [rip+CPI]`, загружающий `+0.0` → `FsFLD0SS/FsFLD0SD` (3-байтовый `xorps xmm, xmm`).

Имена опкодов централизованы в `Tables/MIRRewriteOpcodes.def`. Добавление нового паттерна перезаписи требует трёх шагов:
1. Написать `tryRewriteXxx(MachineFunction &)`, используя `lookupMIRRewriteOpcode()` + `BuildMI(TII->get(...))`
2. Добавить роли опкодов в `MIRRewriteOpcodes.def`
3. Добавить запись паттерна в `MIRRewritePatterns.def`

---

## 3. Пользовательские хуки обфускации

`ObfuscationInterposes` открывает **11 точек хуков**: 6 на уровне IR, 3 на уровне MIR, 2 на уровне байтов:

Три типа сигнатур:
```cpp
using ObfuscationInterpose = std::function<void(
    llvm::ModulePassManager &, const DynCodeOptions &)>;
using MachineObfuscationInterpose = std::function<void(
    llvm::TargetPassConfig &, const DynCodeOptions &)>;
using BinaryObfuscationInterpose = std::function<void(
    llvm::SmallVectorImpl<uint8_t> &, const DynCodeOptions &)>;
```

Ключевые различия:
- `RunBeforePreEmit`: в MIR **всё ещё есть псевдо CFI/EH/XRay** — для манипуляции метаданными пролога/эпилога.
- `RunAfterPreEmit`: **очищенная MIR** — ближайшая к форме AsmPrinter, идеальна для подстановки инструкций / переименования регистров.
- `RunPostExtract`: **чистый поток байтов** после того, как extractor применил патчи к внутритекстовым релокам — для XOR/RC4 всего payload, мусорных байтов, пользовательских заголовков.

```cpp
__attribute__((constructor))
static void myMirObfInit() {
  auto H = neverc::dyncode::getDynCodeObfuscationInterposes();
  H.RunAfterPreEmit = [](llvm::TargetPassConfig &TPC,
                         const neverc::dyncode::DynCodeOptions &Opts) {
    TPC.addExternalPass(new MyInstructionSubstitutionPass(Opts.MirObfuscateSpec));
  };
  // Register via Plugin API: NEVERC_INTERPOSE_SC_BEFORE_PREEMIT / AFTER_PREEMIT / AFTER_FINAL_MIR
}
```

---

## 4. Полный порядок выполнения

```
[IR PassBuilder]
  ├─ RunBeforePrep       (пользовательский хук)
  ├─ ZeroRelocPass(Prep)
  ├─ RunAfterPrep        (пользовательский хук)
  ├─ IndirectBrPass / MemIntrinPass / CompilerRtPass
  ├─ SyscallStubPass / WinPEBImportPass / KernelImportPass
  ├─ Data2TextPass #1
  ├─ RunBeforeInlining   (пользовательский хук)
  │  (уровень O LLVM: AlwaysInliner / SROA / SLP)
  ├─ RunAfterInlining    (пользовательский хук)
  ├─ Data2TextPass #2 / ZeroReloc(Stackify)
  ├─ RunAfterStackify    (пользовательский хук)
  └─ AllBlrPass          (опц.)
        │
[Codegen (IR → MIR)]
  ├─ RunBeforePreEmit    (пользовательский хук, CFI присутствует)
  ├─ DynCodeMIRPrepPass  ← фокус этого документа
  └─ RunAfterPreEmit     (пользовательский хук, CFI удалён)
        │
[AsmPrinter → объектный файл]
        │
[DynCodeExtractor]  ← аудит-запасной вариант на уровне байтов
  ├─ RunPostExtract   (пользовательский хук, чистые байты)
  └─ плоский .bin
```

Уровень MIR выполняет **страховочную очистку + точки хуков обфускации**, а не бизнес-логику. Обещание «пишите обычный C, никаких трюков dyncode не нужно» выполняется 5+ проходами IR.

---

## 5. Обоснование проектирования

| Проблема | Уровень IR? | Уровень MIR? |
|----------|-------------|--------------|
| Устранение константных GV | Да (Data2Text) | Не нужно |
| Устранение libc extern | Да (SyscallStub / WinPEB) | Не нужно |
| Стек-ификация изменяемых глобальных | Да (ZeroReloc) | Не нужно |
| Computed goto | Да (IndirectBr) | Не нужно |
| Псевдоинструкции CFI | Нет (генерируются бэкендом) | Да (сканировать и стирать) |
| Стабы XRay | Нет (генерируются бэкендом) | Да (сканировать и стирать) |
| Обфускация на уровне инструкций | Нет (в IR нет физических регистров) | Да (есть реальные регистры/MI) |
| Переименование регистров | Нет | Да |
| Peephole-расширение констант | Частично | Да (чище) |

## 6. Руководство по расширению

**Добавление встроенного удаления псевдо**: добавьте один case в switch `isDynCodeStripPseudo`.

**Добавление встроенной перезаписи MIR**: напишите `tryRewriteXxx(MachineFunction &)`, используя `TII->getName()` / `BuildMI(TII->get(...))`. Добавьте паттерн в `MIRRewritePatterns.def`, опкоды в `MIRRewriteOpcodes.def`.

**Сторонняя библиотека обфускации**: регистрируйте через [API плагинов](../../plugin-api/README.ru.md) (хуки `NEVERC_INTERPOSE_SC_*`).

## 7. Связь с DynCodeExtractor

| Уровень | Момент | Возможность |
|---------|--------|-------------|
| MIR | **До** AsmPrinter | Может вставлять/удалять MachineInstr |
| Extractor | **После** AsmPrinter | Может только менять байты или отклонять |

**Принцип**: сначала исправляйте в MIR (ещё можно манипулировать инструкциями); прибегайте к extractor только для патчей на уровне байтов (например, внутрисекционный relloc imm26). Это разделение гарантирует, что пользователь никогда не получит «наполовину сломанный `.bin`»: либо он работает, либо есть чёткая устранимая ошибка на этапе компиляции.

## 8. Активное исправление против диагностического пропуска

1. **Активное исправление**: напрямую модифицирует MachineInstr (удаляет псевдо, переписывает CPI→FMOV). Низкая стоимость, независимость от таргета.
2. **Диагностический пропуск**: обнаруживает проблемы, сообщает об ошибках на уровне MIR, позволяет extractor отклонить на уровне байтов. Используется для конструкций, где перезапись MIR потребовала бы обширного target-специфичного кода (например, замена `adrp+ldr CPI` последовательностями `mov/movk`).
3. **Запасной вариант extractor**: жёсткий отказ при любых оставшихся внешних релоках или непустых секциях данных.

Этот принцип делает уровень MIR почти невосприимчивым к обновлениям ISA бэкенда. Единственное обслуживание: «есть ли новая псевдо в TargetOpcode? Если dyncode она не нужна, добавьте один case.»

[`neverc/include/neverc/DynCode/Pipeline/Pipeline.h`]: ../../../neverc/include/neverc/DynCode/Pipeline/Pipeline.h

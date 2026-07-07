<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← فهرس التوثيق](../README.ar.md) · [← مشروع NeverC](../../i18n/README.ar.md)

# NeverC — مُجمِّع dyncode

يُحوِّل مصدر C مباشرةً إلى dyncode ثنائي مسطح **مستقل عن الموضع، بلا إعادة تموضع، بلا قسم بيانات**.

---

## الأهداف الأساسية

1. **اكتب C عاديًا** — دون حيل خاصة بـ dyncode.
2. **مسار تلقائي بالكامل** — `static int counter = 0` و`const char s[] = "..."` والدوال العودية و`write/exit/read/...` والمصفوفات الثابتة الكبيرة تُعالَج داخليًا دون تعديل كود المستخدم.
3. **صفر تبعيات خارجية** — المخرج `.bin` تيار تعليمات خالص بلا dyld أو libSystem أو قسم بيانات.
4. **خيارات CLI عبر TableGen** — كل `-fdyncode-*` في `neverc/include/neverc/Invoke/Options.td.h` (لا مطابقة نصوص صلبة). الأخطاء الإملائية → did-you-mean؛ `--help` يعرض الكل.
5. **قيود المخرجات قابلة للتحقق** — `-fdyncode-bad-bytes=` / `-fdyncode-bad-byte-profile=` يفحصان `.bin` النهائي بعد post-extract ويرفضان المخرجات عند البايتات المحظورة مع الإزاحة والبايت والسياق.
6. **مسار واحد متعدد المنصات** — يقوده جدول `TargetDesc`. نفس مصدر C لـ macOS / Linux / Android / Windows. منصة جديدة = صف في الجدول + مُستخرج واحد، لا تكرار خمس مجموعات passes.

---

## الأهداف المدعومة

| Triple | الصيغة | syscall وضع المستخدم | محلل Ring-0 | الحالة |
|--------|--------|-------------------|-----------------|--------|
| `arm64-apple-macos*` | Mach-O | `svc #0x80` (Darwin BSD) | `DarwinXNUKextShim` | Native loader round-trip + kernel resolver covered |
| `x86_64-apple-macos*` | Mach-O | `syscall` (BSD class mask `0x2000000`) | `DarwinXNUKextShim` | Compile + extract passing; x86_64 `__text` has no reloc expectation |
| `aarch64-linux-gnu` | ELF | `svc #0` (x8 = nr) | `LinuxKallsymsShim` | Compile + extract + kernel resolver passing |
| `x86_64-linux-gnu` | ELF | `syscall` (rax = nr) | `LinuxKallsymsShim` | Compile + extract + kernel resolver passing |
| `aarch64-linux-android*` | ELF | Same as Linux arm64 | `LinuxKallsymsShim` (GKI) | Compile + extract passing |
| `x86_64-linux-android*` | ELF | Same as Linux x86_64 | `LinuxKallsymsShim` (GKI) | Compile + extract passing |
| `aarch64-pc-windows-msvc` | PE/COFF | **PEB walk** (`ldr xN, [x18, #0x60]`) | `WindowsKernelResolverShim` | User-mode PEB read byte sentinel `32 40 f9` validated; ring-0 uses loader resolver |
| `x86_64-pc-windows-msvc` | PE/COFF | **PEB module walk + PE export-table lookup** | `WindowsKernelResolverShim` | User-mode resolver is full IR-level PEB walk; ring-0 does not reuse PEB |

الثمانية triples (OS, arch) تُشغَّل جميعها بـ **نفس مجموعة الـ passes**. الاختلافات معزولة في صفوف `TargetDesc.cpp` وثلاث فروع للمُستخرجات. منصة جديدة = صف إضافي في الجدول + case في كل مُستخرج. بُعد `ExecutionLevel` متعامد: `User` يستخدم مسار syscall / PEB في وضع المستخدم؛ `Kernel` يعطّل الاثنين ويحقن `KernelImportPass` لإعادة كتابة استدعاءات extern عبر shims. راجع [kernel-mode-dyncode.md](kernel-mode-dyncode/README.ar.md).

---

## بدء سريع

```bash
# Always pass -target — output triple is independent of the compiler host.

# 1) Pure computation dyncode — no system calls
neverc -fdyncode -target arm64-apple-macos add.c -o add.bin

# 2) Darwin hello world — write/exit → svc #0x80
neverc -fdyncode -target arm64-apple-macos -mdyncode-syscall hello.c -o hello.bin

# 3) Linux arm64: svc #0 + x8=nr
neverc -fdyncode -target aarch64-linux-gnu -mdyncode-syscall \
       hello.c -o hello_linux_arm64.bin

# 4) Linux x86_64: syscall + rax=nr
neverc -fdyncode -target x86_64-linux-gnu -mdyncode-syscall \
       hello.c -o hello_linux_x64.bin

# 5) Windows x86_64 (PEB walk for API calls)
neverc -fdyncode -target x86_64-pc-windows-msvc \
       -mdyncode-win-peb-import win.c -o win.bin

# 6) Custom entry symbol
neverc -fdyncode -target arm64-apple-macos -fdyncode-entry=dyncode_main kernel.c -o k.bin

# 7) Keep intermediate object for audit (otool / llvm-objdump / dumpbin)
neverc -fdyncode -target arm64-apple-macos -fdyncode-keep-obj=/tmp/dump.obj x.c -o x.bin

# 8) Reject forbidden bytes in final .bin
neverc -fdyncode -target arm64-apple-macos -fdyncode-bad-bytes=00,0a,0d x.c -o x.bin

# 9) Built-in bad-byte profile (same as forbidding 00/0a/0d)
neverc -fdyncode -target arm64-apple-macos -fdyncode-bad-byte-profile=http-newline x.c -o x.bin

# 10) Run on macOS (platform-specific loader)
./loader_arm64_macos add.bin 3 4   # exit code = 7

# 11) Verbose extractor summary
neverc -v -fdyncode -target arm64-apple-macos fib.c -o fib.bin
#   dyncode-extractor: wrote 64 bytes to 'fib.bin'
#   dyncode-extractor: target   = arm64-apple-macos (Mach-O)
#   dyncode-extractor: entry symbol = _main
#   dyncode-extractor: patched 1 BRANCH26, 0 PAGE21, 0 PAGEOFF12 intra-section reloc(s)
```

---

## خيارات CLI (كلها في `Options.td.h`)

| الخيار | الوصف |
|--------|-------------|
| `-fdyncode` | تفعيل وضع تجميع dyncode. |
| `-fno-dyncode` | إلغاء `-fdyncode` سابق. |
| `-fdyncode-all-blr` | وضع عدواني: تحويل الاستدعاءات المباشرة داخل الوحدة إلى `blr xN` / `call *rax` وإزالة relocs الفروع النسبية. غير مطلوب في الاستخدام العادي. |
| `-mdyncode-syscall` | تفعيل صريح لـ syscall stubs (افتراضي مع `-fdyncode` على Darwin/Linux/Android؛ للنية أو توافق السكربتات). |
| `-mdyncode-libsystem` | اسم Darwin القديم لـ `-mdyncode-syscall`. |
| `-mdyncode-win-peb-import` | تفعيل صريح لاستيراد PEB في Windows (افتراضي مع `-fdyncode` + triple Windows). |
| `-fdyncode-keep-obj=<path>` | نسخ ملف الكائن الوسيط إلى `<path>` للتدقيق بمُفكك تجميع أصلي. |
| `-fdyncode-entry=<name>` | استبدال اسم نقطة الدخول (`main`, `_main`, `dyncode_entry`, `_dyncode_entry`). |
| `-fdyncode-bad-bytes=<hex-list>` | قائمة بايتات محظورة مفصولة بفواصل. فحص `.bin` النهائي بعد post-extract؛ عند التطابق يفشل التجميع دون كتابة ملف. |
| `-fdyncode-bad-byte-profile=<name>` | ملفات محظورة مدمجة: `null`, `c-string`, `http-newline`, `line`, `whitespace`, `ascii-control`. قابلة للجمع مع `-fdyncode-bad-bytes=`. |
| `-fdyncode-obfuscate=<spec>` | يُمرَّر إلى خطافات التشويش **مستوى IR** عبر [واجهة الإضافات](../plugin-api/README.ar.md). no-op بدون إضافة. راجع [ir-pass-design.md §9](ir-pass-design/README.ar.md#9-obfuscation-interposes). |
| `-fdyncode-mir-obfuscate=<spec>` | يُمرَّر إلى خطافات **مستوى MIR**. الافتراضي `-fdyncode-obfuscate=` إن لم يُحدَّد. راجع [mir-pass-design.md §3](mir-pass-design/README.ar.md#3-user-obfuscation-interposes). |

---

## نظرة على البنية

ينقسم المسار إلى **مرورات IR مستقلة عن الهدف + مُستخرجات خاصة بالمنصة**:

```mermaid
flowchart TD
    Driver["neverc -fdyncode · OptTable + Options.td.h"]
    Frontend["C23 Frontend · PIC default"]
    Driver -->|describeTriple| Frontend
    Frontend -->|LLVM IR| ZRP

    subgraph IR["Target-Independent IR Passes"]
        direction TB
        ZRP["① ZeroRelocPass — Prep\ninternal + always_inline\nreject ctors / thread_local / extern_weak"]
        IBP["② IndirectBrPass\ncomputed-goto → switch"]
        SSP["③ SyscallStubPass\nlibc → svc #0x80 / svc #0 / syscall"]
        WPP["④ WinPEBImportPass\nextern Win32 API → PEB-walk thunk"]
        MIP["⑤ MemIntrinPass\nmemcpy/memset/str* → byte-loop"]
        CRP["⑥ CompilerRtPass\ni128 div/mod → inline long-division"]
        D2T1["⑦ Data2TextPass — Phase 1\nconst GV → stack stores"]
        ZRP --> IBP --> SSP --> WPP --> MIP --> CRP --> D2T1
    end

    Backend["AArch64 / X86 Backend\nSROA · InstCombine · AlwaysInliner · SLP"]
    D2T1 --> Backend

    Backend --> D2T2
    subgraph Post["Post-Backend IR"]
        direction TB
        D2T2["⑧ Data2TextPass — Phase 2\nvector const split"]
        ZRS["⑨ ZeroRelocPass — Stackify\nglobals → entry alloca"]
        ABP["⑩ AllBlrPass (optional)\ndirect call → indirect call"]
        D2T2 --> ZRS --> ABP
    end

    Codegen["Codegen · IR → MIR → Register Allocation"]
    ABP --> Codegen

    Codegen --> MH1
    subgraph MIR["MIR Layer"]
        direction TB
        MH1["⑪ RunBeforePreEmit interpose"]
        MIRP["⑫ DynCodeMIRPrepPass\nstrip CFI / EH_LABEL / XRay / StackMap"]
        MH2["⑬ RunAfterPreEmit interpose\ninstruction-level obfuscation entry"]
        MH1 --> MIRP --> MH2
    end

    MH2 -->|"Mach-O / ELF / COFF .o"| Extractor

    subgraph Extract["Extractor Layer"]
        Extractor["DynCodeExtractor\nMachO · ELF · COFF\npatch intra-.text relocs\nreject external reloc / data section\nbad-byte audit"]
    end

    Extractor --> Output(["flat .bin dyncode"])
```

## اختلافات المنصة المعتمدة على الجداول

يُعرّف `neverc/include/neverc/DynCode/Pipeline/TargetDesc.h` بنية `TargetDesc` لكل تركيبة (OS, arch):

- `TextSectionName`: Mach-O `__text` / ELF `.text` / COFF `.text`
- `SyscallABI`: enum value (`DarwinSvc80` / `LinuxSvc0` / `LinuxSyscall` / `WindowsPEB` / `None`)
- `AsmTemplate`: `svc #0x80` / `svc #0` / `syscall`
- `SyscallNumberReg`: x16 / x8 / rax
- `SyscallRetReg`: x0 / rax
- `ArgRegs`: ordered list of platform ABI argument registers + count
- `TCBReadAsm` / `TCBReadConstraint`: inline-asm single-instruction template for reading TEB/PEB pointer (Windows x86_64 = `movq %gs:0x60, $0`, Windows arm64 = `ldr $0, [x18, #0x60]`). `WinPEBImportPass` reads directly from the table.
- `DriverInjectFlags`: platform-specific driver flags as a null-terminated static array (x86_64 Unix gets `-fpic -mcmodel=small`; Windows gets `-mno-stack-arg-probe` / `/GS-`). `perTargetInjectFlags` reads from the table.

يولّد SyscallStubPass وWinPEBImportPass InlineAsm من حقول TargetDesc. الخلفية تستخدم أنماط TableGen. هدف جديد = **صف إضافي** في `describeTriple` و**case** في كل مُستخرج.

## طبقة الاستخراج

| الصيغة | التنفيذ | relocs داخل القسم القابلة للترقيع |
|--------|---------------|-------------------------------------|
| Mach-O | `MachOExtractor.cpp` | arm64: `ARM64_RELOC_BRANCH26` / `PAGE21` / `PAGEOFF12`; x86_64: `X86_64_RELOC_SIGNED` / `SIGNED_1/2/4` / `BRANCH` (intra-`__text` pcrel32); `UNSIGNED` / `GOT_LOAD` / `GOT` / `SUBTRACTOR` / `TLV` rejected |
| ELF | `ELFExtractor.cpp` | arm64: `R_AARCH64_CALL26` / `JUMP26` / `ADR_PREL_PG_HI21(_NC)` / `ADD_ABS_LO12_NC` / `LDST{8,16,32,64,128}_ABS_LO12_NC` / `PREL32`; x86_64: `R_X86_64_PC32` / `PLT32` (`GOTPCREL` rejected) |
| COFF | `COFFExtractor.cpp` | arm64: `IMAGE_REL_ARM64_BRANCH26` / `PAGEBASE_REL21` / `PAGEOFFSET_12A` / `PAGEOFFSET_12L` / `REL32`; x86_64: `IMAGE_REL_AMD64_REL32` / `REL32_[1-5]` |

أي نوع آخر أو reloc بين الأقسام يفشل مع تلميحات (libc → stub syscall / `_Complex` → struct يدوي / fallback تجمع backend، إلخ).

---

## مصفوفة قدرات كود المستخدم

| السيناريو | كود المستخدم | مدعوم | الآلية |
|----------|-----------|-----------|-----------|
| Integer arithmetic / bitwise | `int f(int a) { return a*3+1; }` | نعم | Pure instruction stream |
| Recursion / loops | `int fib(int n) { ... }` | نعم | `static` + always_inline |
| `switch / case` | `switch (op) { case 0: ... }` | نعم | Driver injects `-fno-jump-tables` |
| Struct by-value passing | `struct Vec3 v = {...}; dot(v);` | نعم | Stack-ified + always_inline |
| Floating-point | `double y = x * 3.14;` | نعم | Data2Text rewrites ConstantFP to volatile-loaded bit pattern |
| Small constant arrays | `const int t[4] = {1,2,3,4};` | نعم | Data2Text stack-ifies |
| Large constant arrays (256B+) | `const unsigned char tbl[256] = {...}` | نعم | Data2Text, no size limit |
| String literals | `const char s[] = "hi\n";` | نعم | Data2Text stack-ifies |
| `memcpy` / `memset` / `memmove` / `memcmp` | `memcpy(dst, src, n);` | نعم | MemIntrinPass byte-loop wrappers |
| `strlen` / `strcpy` / `strcmp` / etc. | `strlen(buf);` | نعم | MemIntrinPass byte-loop wrappers |
| `__int128` division / modulo | `u128 q = a / b;` | نعم | CompilerRtPass inline long-division |
| `_Atomic` / `__atomic_*` / `__sync_*` | `__atomic_fetch_add(&c, 1, ...)` | نعم | Inline LDXR/STXR (arm64) / LOCK (x86_64) |
| `__builtin_*` family | `__builtin_popcount(x)` | نعم | Backend single-instruction selection |
| VLA / flexible array / compound literal | Normal C99/C11 | نعم | `-fno-jump-tables` + Data2Text |
| Mutable globals | `static int counter = 0;` | نعم | ZeroReloc stack-ifies |
| libc write/exit | `write(1, s, 3);` | نعم (مع `-mdyncode-syscall`) | Syscall wrapper |
| POSIX includes | `#include <unistd.h>` | نعم (وضع dyncode يبدّل تلقائياً إلى shim) | Driver injects `__NEVERC_DYNCODE__` |
| Win32 API | `WriteFile(h, buf, n, &w, 0);` | نعم (مع `-mdyncode-win-peb-import`) | PEB-walk thunk |
| Windows SDK includes | `#include <windows.h>` | نعم (وضع dyncode يبدّل تلقائياً إلى shim) | Lightweight shim headers |
| Custom entry name | `int dyncode_main(...)` | نعم (مع `-fdyncode-entry=...`) | Driver pass-through |
| Global constructors | `__attribute__((constructor))` | لا | لا يوجد runtime لتشغيلها |
| TLS / thread_local | `thread_local int x;` | Auto-demoted to static | ZeroRelocPass.Prep silently demotes |
| C++ / ObjC | — | لا | المشروع C فقط |

---

## هيكل المجلدات

```
neverc/
├── include/neverc/Invoke/Options.td.h           # -fdyncode-* TableGen definitions
├── include/neverc/DynCode/                  # Headers (organized by subsystem)
│   ├── Pipeline/                              # Pipeline / driver integration
│   │   ├── Pipeline.h                         # IR + MIR interpose registration
│   │   ├── DriverIntegration.h
│   │   ├── TargetDesc.h                       # Platform table / descriptors
│   │   ├── DynCodeOptions.h                 # Cross-subsystem config
│   │   ├── Diagnostics.h                      # Cross-subsystem diagnostics
│   │   └── SymbolNames.h                      # Cross-subsystem symbol utilities
│   ├── Extractor/
│   │   └── DynCodeExtractor.h
│   ├── IR/                                    # IR-level passes and ABIs
│   │   ├── ZeroRelocPass.h / ZeroRelocABI.h
│   │   ├── Data2TextPass.h / Data2TextABI.h
│   │   ├── AllBlrPass.h / IndirectBrPass.h
│   │   ├── MemIntrinPass.h                    # memcpy/memset/str* inlining
│   │   ├── StringRuntimePass.h / StringRuntimeABI.h
│   │   ├── HeapArenaPass.h                    # malloc/free → arena + OS fallback
│   │   ├── MmapABI.h                          # Shared mmap constants (prot/flags)
│   │   ├── DynCodeIRHelpers.h               # Common IR utilities (getSizeType, etc.)
│   │   ├── ExternRewriter.h                   # Extern function rewrite utilities
│   │   └── CompilerRtPass.h                   # __int128 division inline
│   ├── MIR/
│   │   └── MIRPrepPass.h                      # Catch-all MachineFunctionPass
│   ├── Import/                                # User-mode + kernel-mode import resolution
│   │   ├── SyscallStub.h / SyscallTables.h
│   │   ├── WinPEBImport.h / WinImportTables.h
│   │   ├── KernelImportPass.h / KernelImportABI.h
│   │   └── PtrCacheHelpers.h                  # Shared address cache encryption helpers
│   └── Tables/                                # User-extensible .def tables
├── lib/DynCode/                             # Implementation (mirrors header structure)
│   ├── Pipeline/ Extractor/ IR/ MIR/ Import/
└── lib/Invoke/Core/Driver.cpp

tests/neverc/                                   # Tests (GTest)
├── DynCodeTests.cpp                         # Core dyncode round-trip tests
├── DynCodeStressTests.cpp                   # Stress tests (VLA, __sync_*, __int128, etc.)
├── DynCodeCrossTargetTests.cpp              # Cross-target compile-only smoke tests
├── dyncode/
│   ├── loader_arm64_macos.c / loader_linux.c / loader_windows.c
│   └── test_dyncode_*.c

docs/dyncode-compiler/
├── README.md                                  ← English
├── README.ar.md                               ← العربية
├── arm64-assembly-tutorial/README.md
├── cross-platform-architecture/README.md
├── ir-pass-design/README.md
├── kernel-mode-dyncode/README.md
├── mir-pass-design/README.md
├── pipeline-and-pic/README.md
├── platform-extension-guide/README.md
├── progress/README.md
└── roadmap/README.md
```

---

## المتطلبات (عبر المنصات)

1. يجب أن يكون عنوان تحميل dyncode بمحاذاة 4 كيلوبايت — سلوك `mmap` / `VirtualAlloc` الطبيعي؛ محمّلات الاختبار تلتزم بذلك.
2. اتفاقيات الاستدعاء تتبع ABI الأصلية لنظام التشغيل:
   - Darwin / Linux / Android: System V AMD64 or AAPCS64
   - Windows: Win64 (rcx/rdx/r8/r9)
3. المُحمّل مسؤول عن تفريغ ذاكرة التعليمات (arm64) / FlushInstructionCache (Windows).

## توسيع مرورات التشويش (واجهة محجوزة)

مسار dyncode يضمن فقط «أن الكود يعمل بشكل صحيح». التشويش (CFF، تدفق وهمي، مسندات معتمة، تشفير سلاسل، استبدال تعليمات، إعادة تسمية سجلات، إلخ) عمل منفصل. `Pipeline.h` يعرّض `ObfuscationInterposes` بـ **11 نقطة ربط** على ثلاث طبقات:

**مستوى IR (6 خطافات، `ModulePassManager &`)**:
- `RunBeforePrep` — Before any dyncode pass
- `RunAfterPrep` — Linkage unified (internal + always_inline)
- `RunBeforeInlining` — Last chance before AlwaysInliner
- `RunAfterInlining` — IR fully compressed into one large function
- `RunAfterStackify` — Final IR shape, next step is codegen
- `RunAfterFinalIR` — After AllBlrPass, the true last IR interpose

**مستوى MIR (3 خطافات، `TargetPassConfig &`)**:
- `RunBeforePreEmit` — Registers allocated, **CFI/EH pseudos still present**
- `RunAfterPreEmit` — **Built-in MIRPrepPass has stripped pseudos**, closest to the byte form AsmPrinter will see; ideal for instruction-level obfuscation/register renaming
- `RunAfterFinalMIR` — True last MIR interpose, after LLVM `addPreEmitPass2()`, just before AsmPrinter

**مستوى تيار البايتات (2 خطافات، `SmallVectorImpl<uint8_t> &`)**:
- `RunPostExtract` — After extractor completes intra-text relocation patching and data-section audit; before `.bin` is written. Use for whole-payload encryption, junk byte insertion, or custom headers.
- `RunPostFinalize` — After all finalize steps; NeverC performs no further auditing.

`-fdyncode-obfuscate=<spec>` و`-fdyncode-mir-obfuscate=<spec>` يمرّران النصوص إلى `DynCodeOptions::ObfuscateSpec` / `MirObfuscateSpec`. مواصفات MIR تطابق IR افتراضياً. المسار لا يحلّل المحتوى — مكتبة التشويش تحدد DSL خاصاً. التفاصيل:

- IR-level: [ir-pass-design.md §9 — Obfuscation Interposes](ir-pass-design/README.ar.md#9-obfuscation-interposes).
- MIR-level: [mir-pass-design.md §3 — User Obfuscation Interposes](mir-pass-design/README.ar.md#3-user-obfuscation-interposes).
---

## القيود الحالية

- **يدعم 8 تركيبات (OS, arch)** (انظر المصفوفة). triples أخرى (RISC-V، PowerPC، x86 32-bit، ARM big-endian، إلخ) تُرفض في `describeTriple()` مع قائمة المدعوم. كل صف له سياقات `User` / `Kernel` مستقلة → 16 متغيراً (OS, arch, مستوى).
- **اجتياز PEB في Windows مُنفَّذ بالكامل مع توزيع multi-DLL**. `__neverc_win_resolve` يقبل أزواج `(dll_hash, api_hash)`. القائمة البيضاء الحالية تغطي kernel32.dll (~125 API)، ntdll.dll (~26)، user32.dll (~13)، ws2_32.dll (~23)، advapi32.dll (~16)، shell32.dll (~6). إضافة API = صف في `Tables/Win32Apis.def` + إعلان في `lib/Headers/windows.h`.
- **القائمة البيضاء للدوال الخارجية** تغطي syscalls شائعة لـ Darwin BSD / Linux / Android (~80+) + Win32 (~190). stdio وواجهات runtime الثقيلة غير مشمولة — dyncode لا يمكنه تضمين آلة حالة stdio كاملة.
- لا يدعم C++ / ObjC / CUDA — NeverC مخصّص لـ C فقط.
</div>

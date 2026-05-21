**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode 컴파일러](../README.ko.md)

# Shellcode 파이프라인, MIR 및 PIC 전략 (설계 노트)

이 문서는 NeverC shellcode 모드의 **IR → LLVM 최적화 → 백엔드 MIR → 오브젝트 파일 → 추출/패치** 체인에서의 설계 트레이드오프와 **컴파일러 전반의 기본 PIC** 정책과의 관계를 설명합니다. 구현 세부사항은 소스 코드와 영문 주석이 정본입니다.

## 1. 왜 기본적으로 PIC를 강제하는가 (비 shellcode 포함)

shellcode 추출기는 실행 가능 프래그먼트의 외부 심볼 참조가 **PC 상대** 또는 섹션 내 해결 가능한 재배치에 위치한다고 가정합니다 (로더가 `.data`를 채워야 하는 하드코딩된 절대 주소나 상수 풀이 아님).

NeverC는 `Generic_GCC::isPICDefaultForced()`, `MachO::isPICDefaultForced()`, `MSVCToolChain::isPICDefaultForced()`에서 **true**를 반환하여, 업스트림 Clang의 "선택적 기본 PIC"와 구분됩니다: **모든 플랫폼 컴파일이 항상 PIC만을 모델로 사용**. 이는:

- 일반 C 컴파일과 `-fshellcode` 컴파일이 동일한 재배치 습관을 공유하여 "일반 작동, shellcode에서 고장" 인지 부담을 줄입니다.
- Linux / Android / macOS / Windows 백엔드가 테이블 기반 디스크립터(`TargetDesc` + `Options.td.h`)에서 동일한 가정을 공유하여 드라이버의 `if (linux)` 스타일 하드코딩을 방지합니다.

이 정책은 `-fshellcode` 활성화 여부나 user/kernel 컨텍스트를 구분하지 않습니다. 사용자가 `-fno-pic` / `-static` / `-mkernel` / `-mdynamic-no-pic`를 전달해도 `ParsePICArgs()`는 `Reloc::PIC_`를 유지하며, 일반 컴파일, 사용자 모드 shellcode, 커널 모드 shellcode에 동일한 PC 상대 가정을 사용합니다.

## 2. IR과 MIR 2단계 분업

### 2.1 IR 계층 (`registerShellcodePasses`)

"일반 C" 시맨틱을 **단일 진입, 독립 데이터 섹션 없음, 문제 전역 변수 없음** 형태로 압축하는 역할: `ZeroRelocPass`, `IndirectBrPass`, `MemIntrinPass`, `StringRuntimePass`, `HeapArenaPass`, `CompilerRtPass`, `SyscallStubPass`, `WinPEBImportPass`, `KernelImportPass`(커널 전용), `Data2TextPass` 등.

**원칙**: IR에서 구조적으로 해결 가능한 문제는 IR에서 먼저 수정(상수 풀, BlockAddress, `memcpy`의 libc 폴스루, `__int128 /`의 `__udivti3` 폴스루 등)하여 백엔드와 추출기가 보는 바이트 스트림을 단순화합니다. 사용자 인지 부담이 높지만 안전하게 내부화할 수 있는 시나리오에서는 드라이버가 적극적으로 규칙을 주입합니다(예: AArch64 Linux / Android / Windows의 `long double`을 shellcode 모드에서 binary64로 강등). 런타임 없이는 지원할 수 없는 구조만이 MIR / 추출기 진단을 트리거합니다.

### 2.2 MIR 계층 (`registerShellcodeMachinePasses`)

LLVM 레거시 `TargetPassConfig`에 콜백을 등록, **레지스터 할당 후, `addPreEmitPass` 전**에 다음 순서로:

1. 사용자/난독화 라이브러리: `RunBeforePreEmit` (CFI / EH 의사 명령이 아직 존재; 메타데이터 의존 변환에 적합).
2. **`ShellcodeMIRPrepPass`**: `.eh_frame` / `.pdata` / `.xray_*` 부속 섹션을 생성하는 의사 명령을 제거하여 AsmPrinter 전 명령 스트림을 "순수 코드"에 가깝게 만듦.
3. 사용자/난독화 라이브러리: `RunAfterPreEmit` (명령 치환, 레지스터 리네이밍 등 "최종 머신 코드 형태" 난독화에 적합).

**원칙**: 네이티브 명령 시퀀스에 문제가 있으면 MIR에서 수정(`ShellcodeMIRPrepPass` 주변 특히); **추출과 패치는 최후의 안전망**으로, COFF/ELF/Mach-O 계층에서 동일 로직 중복을 방지합니다.

MIR 연산 코드 이름은 pass 제어 흐름에 분산시키지 않음; `ShellcodeMIRPrepPass`는 `Tables/MIRRewriteOpcodes.def`의 `(pattern, role, opcode)` 테이블을 `TargetInstrInfo::getName()` 경유로 참조합니다. shellcode 친화적 명령 치환 추가 시 테이블 항목과 소규모 MIR 리라이트 추가를 우선; 필요 시에만 백엔드 `.td` 명령 선택 변경으로 폴백하고, 추출기 수준 오브젝트 형식 폴백이 최후 수단입니다.

> 참고: `ShellcodeMIRPrepPass`는 `-fshellcode` 활성화 시에만 등록됩니다. 일반 프로그램에서 CFI/EH를 전역적으로 제거하면 정상 예외 처리와 디버그 정보가 파괴됩니다.

IR과 MIR 전역 콜백 모두 **한 번 등록, 런타임에 현재 `ShellcodeOptions` 스냅샷 읽기** 패턴을 사용합니다. 장수명 임베디드 컴파일러 프로세스를 지원: 동일 프로세스가 shellcode를 먼저 컴파일하고 일반 C를 컴파일할 때 일반 C는 이전 IR/MIR pass를 상속하지 않음; 여러 shellcode TU를 연속 컴파일 시 전역 콜백 등록 중복이 동일 pass 세트를 다중 스택하지 않습니다.

## 3. 테이블 기반 플랫폼 차이

- **Triple → 동작**: `TargetDesc.cpp`의 `describeTriple()`와 `TargetDesc` 필드에 집중(섹션 이름, syscall ABI, 인라인 어셈블리 템플릿, 드라이버 주입 플래그 등). 새 OS/Arch 추가 시 추출기나 pass에 긴 분기를 작성하기보다 **테이블 항목 추가**를 우선합니다.
- **CLI 옵션**: `neverc/include/neverc/Invoke/Options.td.h`에 정의; `DriverIntegration.cpp`가 `OPT_*` 열거형으로 소비하여 문자열 매직을 방지합니다.

## 4. Windows MSVC 툴체인 및 SDK 레이아웃

Windows 타겟으로 크로스 컴파일 시 NeverC는 **하드코딩된 절대 경로 없이** 두 가지 SDK 소스를 지원합니다:

1. **빌드 트리에 번들된 SDK** (권장): 사용자와 테스트 스크립트가 `build-neverc/sdk`를 SDK 루트로 사용. NeverC가 설치 디렉터리 내 `sdk/msvc/`를 자동 감지하고 `MSVCToolChain::AddNeverCSystemIncludeArgs` / `Linker::ConstructJob`에서 include/lib 경로를 주입. 일반적 레이아웃:

   ```
   build-neverc/bin/neverc
   build-neverc/sdk/msvc/
     crt/include, crt/lib/<arch>
     sdk/include/{ucrt,um,shared}, sdk/lib/{ucrt,um}/<arch>
   ```

2. **실제 VS 스타일 sysroot** (선택): `VC/Tools/MSVC/<version>/...` + `Windows Kits/10/...` 디렉터리 트리가 있다면 `-winsysroot=<path>` 또는 `NEVERC_WIN_SYSROOT` 환경 변수로 지정.

두 소스 모두 레지스트리나 OS 제공 VS 환경 변수 없이 작동하여 macOS / Linux에서 Windows shellcode 크로스 컴파일을 가능하게 합니다.

## 5. 난독화 및 확장 포인트

- **IR 난독화**: `setShellcodeObfuscationHooks`로 복수 IR 단계 훅 제공; `-fshellcode-obfuscate=`가 spec 문자열을 외부 라이브러리에 전달. 각 계층에 **전**(최적화 전)과 **후**(최적화 후) 훅 있음. `RunAfterFinalIR`은 진정한 마지막 IR 주입 가능 지점——여기 등록된 난독화 pass 이후 출력을 수정하는 pass 없음. 총 11개 훅(6 IR + 3 MIR + 2 바이트스트림).
- **MIR 난독화**: `RunBeforePreEmit` / `RunAfterPreEmit`는 중간 수준 MIR 훅; `RunAfterFinalMIR`은 **진정한 마지막** MIR 훅(fork 확장이 `RegisterTargetPassConfigPostPreEmitCallbackFn` 추가, `addPreEmitPass2()` 후 호출). `-fshellcode-mir-obfuscate=`로 MIR spec을 별도 지정; 미설정 시 IR spec이 기본.
- **바이트스트림 훅**: `RunPostExtract`는 finalize **전** 훅; `RunPostFinalize`는 finalize **후** 훅(디스크 기록 전 마지막 순간, NeverC는 이후 감사하지 않음).
- **Finalize 플러그인 SDK**: `Plugin.h`가 `registerBadByteRewriteStrategy`(명령 수준 금지 바이트 재작성 전략 체인)와 `registerCharsetEncoder`(명명된 캐릭터셋 등록)를 노출. [plugin-interface.md 제 2–3절](../plugin-interface/README.ko.md#2-bad-byte-rewriter-badbyterewritestrategy) 참조.
- **크기/정렬/패딩**: `-fshellcode-max-length=`, `-fshellcode-align=`, `-fshellcode-pad=`가 finalize 끝에 실행; 드라이버가 모순된 설정을 거부.
- **설계 선택**: 난독화, 다형성, 단계 인코더, 간접 시스템 콜 등 전략 계층 기능은 **의도적으로 내장하지 않으며**, 선택적 플러그인으로만 제공.

## 6. 커널 모드 (Ring-0) 차원

shellcode 모드는 `-mshellcode-context=user|kernel`을 파이프라인의 두 번째 차원으로 도입, triple 위에 레이어링:

- **사용자 모드**: PEB 워크 / syscall stub 파이프라인.
- **커널 모드**:
  - `SyscallStubPass` / `WinPEBImportPass`가 pass 수준에서 조기 리턴.
  - `TargetDesc::KernelInjectFlags`가 OS/arch 적절한 백엔드 플래그 추가(Unix x86_64: `-mno-red-zone -mcmodel=kernel`, Windows: `/kernel`, AArch64: `-mgeneral-regs-only`).
  - `KernelImportPass`가 미해결 extern 직접 호출을 리졸버 지원 간접 호출로 재작성, 필요 시 `(resolver, cookie)` 암시적 접두 매개변수 주입.
  - `<neverc/kernel.h>`가 `neverc_kern_resolve_t`, `neverc_kern_hash()` 및 관련 커널 측 시그니처를 노출; 사용자 모드 shim(`<windows.h>`, `<unistd.h>` 등)은 커널 모드에서 `#error`로 거부.

자세한 내용은 [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.ko.md) 참조.

## 7. Windows POSIX 호환 계층

### 7.1 문제

크로스 플랫폼 C 코드는 `write(fd, buf, n)`, `read(fd, buf, n)`, `exit(code)` 등을 일반적으로 사용합니다. Unix에서는 `SyscallStubPass`가 이를 인라인 syscall로 대체합니다. Windows에서는 이 POSIX 이름에 대응하는 Win32 API가 없어 "해결 불가 재배치" 오류가 발생합니다.

### 7.2 설계 목표

**제로 사용자 인식**: 동일 C 소스가 모든 8개 타겟 triple에서 `#ifdef _WIN32`나 수동 Win32 API 호출 없이 컴파일됩니다.

### 7.3 구현

`WinPEBImportPass`가 3단계 처리를 구현합니다:

1. **1단계 — POSIX 스캔**: 미매칭 extern 선언을 POSIX 호환 테이블과 대조.
2. **2단계 — 브리지 래퍼 생성**: `Win32PosixCompat.def`가 POSIX 이름을 `always_inline` 래퍼 빌더에 배분(예: `write` → `GetStdHandle` + `WriteFile`, `mmap` → `VirtualAlloc` + prot 매핑, `exit` → `ExitProcess` 등). 13개 POSIX 함수 그룹 커버.
3. **3단계 — PEB 해결**: 래퍼가 참조하는 Win32 API를 일반 PEB 워크 리졸버로 해결.

### 7.4 확장성

새 POSIX 호환 함수 추가: 별칭만이면 `Win32PosixCompat.def` 변경; 새 시맨틱은 소규모 IR 빌더 + 테이블 1항목 필요. `open→CreateFileA` 같은 상태 유지 연산(fd/handle 수명 테이블 필요)은 의도적으로 에뮬레이트하지 않습니다.

## 8. K&R 암시적 선언 자동 수정

사용자가 `#include` 없이 POSIX 함수를 호출하면 C89가 0개 형식 매개변수의 K&R 암시적 선언을 생성합니다. `SyscallStubPass`는 50+ 일반 POSIX 함수의 정규 LLVM IR 함수 타입을 가진 `getCanonicalSyscallType()` 테이블을 유지합니다. 0개 형식 매개변수의 K&R 선언 감지 시 정규 시그니처를 자동 대체합니다.

## 9. 요약

| 주제 | 접근법 |
|------|--------|
| 기본 PIC | 모든 툴체인 `isPICDefaultForced()==true`, shellcode 가정에 정렬 |
| IR에서 먼저 수정 | 상수, 간접 점프, 메모리 내장 함수를 가능하면 IR에서 제거 |
| MIR 안전망 | `ShellcodeMIRPrepPass` + 전후 훅, 오브젝트 추출/패치는 최후 수단 |
| 하드코딩 최소화 | `TargetDesc` + `Options.td.h` 테이블 기반 |
| 사용자/커널 2차원 | `-fshellcode` × `-mshellcode-context={user,kernel}`; 각 (OS, arch, level)이 `describeTriple()`의 1행 |
| Windows POSIX 호환 | `WinPEBImportPass`가 13개 POSIX 함수 그룹 브리지(write→WriteFile, mmap→VirtualAlloc 등) |
| K&R 자동 수정 | `SyscallStubPass`가 0개 형식 매개변수 선언에서 정규 POSIX 시그니처로 폴백 |

## 10. Shim 헤더 크로스 플랫폼 상수

Shim 헤더(`sys/mman.h`, `fcntl.h` 등)는 타겟 커널 ABI에 맞는 상수를 노출해야 합니다. shellcode syscall stub이 이 값들을 libc 변환 없이 직접 커널에 전달하기 때문입니다.

주요 차이:

| 상수 | Darwin | Linux/Android |
|------|--------|---------------|
| `AT_FDCWD` | `-2` | `-100` |
| `MAP_ANONYMOUS` | `0x1000` | `0x20` |
| `O_CREAT` | `0x0200` | `0x0040` |
| `O_TRUNC` | `0x0400` | `0x0200` |
| `O_CLOEXEC` | `0x1000000` | `0x80000` |

구현: shim 헤더의 `#if defined(__APPLE__)` 가드. `SyscallTables.cpp` POSIX 호환 테이블은 Linux 값(`AT_FDCWD = -100`)을 사용하며, `SyscallABI::LinuxSvc0` / `LinuxSyscall` 경로에서만 활성화됩니다. Windows 타겟은 이 POSIX 헤더를 사용하지 않으며; POSIX→Win32 브리지는 `WinPEBImportPass` 호환 래퍼가 처리합니다.

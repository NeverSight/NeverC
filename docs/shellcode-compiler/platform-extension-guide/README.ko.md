**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode 컴파일러](../README.ko.md)

# 플랫폼 확장 가이드

이 문서는 shellcode 컴파일러를 새로운 타겟 플랫폼으로 확장하는 방법을 설명합니다. 현재 지원: **macOS / Linux / Android / Windows의 arm64 / x86_64** (8개 triple), 각각 독립된 **User** / **Kernel** 컨텍스트 (총 16개 변형). 새 플랫폼 추가는 보통 수백 줄의 코드로 충분합니다.

## 설계 철학: 테이블 기반, 분기 기반이 아님

모든 pass는 타겟 독립적입니다. 플랫폼 차이는 **두 곳**에 집중됩니다:

1. `TargetDesc.cpp`의 `describeTriple()` 테이블 항목
2. 세 추출기(Mach-O / ELF / COFF)의 아키텍처 switch

새 플랫폼 추가 = (1)에 1행 추가 + (2)에 1 case 추가.

## 단계

### 1. `TargetDesc`에 행 추가

`describeTriple()`에 해당 OS 분기를 추가합니다:

```cpp
if (TT.isOSFreeBSD()) {
  D.OS = ShellcodeOS::FreeBSD;
  D.Format = ObjectFormat::ELF;
  D.TextSectionName = ".text";
  if (D.Arch == ShellcodeArch::X86_64) {
    D.Syscall = SyscallABI::FreeBSDSyscall;
    D.AsmTemplate = "syscall";
    D.SyscallNumberReg = "rax";
    D.SyscallRetReg = "rax";
    D.ArgRegs = kX86_64FreeBSDArgRegs;
    D.NumArgRegs = 6;
    D.DriverInjectFlags = kX86_64UnixInjectFlags;
  }
  return D;
}
```

**필수 필드** (`TargetDesc.h`에 정의):

| 필드 | 용도 | 누락 시 |
|------|------|---------|
| `OS` / `Arch` / `Format` | 디스패치 키 | `describeTriple`이 Unknown 반환 → 드라이버 조기 거부 |
| `TextSectionName` | 추출기가 진입 섹션 검색 | `.text` 미발견 → 거부 |
| `Syscall` | SyscallStubPass 대체 결정 | `None` → SyscallStubPass no-op |
| `AsmTemplate` / `SyscallNumberReg` / `SyscallRetReg` / `ArgRegs` | SyscallStubPass 인라인 어셈블리 생성 | 하나라도 비어있음 → SyscallStubPass no-op |
| `TCBReadAsm` / `TCBReadConstraint` | WinPEBImportPass TEB 읽기 인라인 어셈블리 | 비어있음 → PEB walk이 빈 InlineAsm 생성 (Windows: 필수) |
| `DriverInjectFlags` | shellcode 모드에서 주입되는 플랫폼별 플래그 | null → 주입 없음 |

### 2. `SyscallStub` / `SyscallTables` 확장 (OS에 커널 트랩이 있는 경우)

- `TargetDesc.h`의 `SyscallABI`에 열거값 추가
- `SyscallTables.cpp`에 `kXxxTable` 추가
- `lookupSyscall`의 switch에 case 추가
- `SyscallStubPass` 변경 불필요 — InlineAsm 템플릿/제약은 `TargetDesc`에서 읽음

### 2.5 Windows Win32 API 화이트리스트 확장

Windows에는 안정적인 syscall ABI가 없습니다; `WriteFile` / `CreateThread` / `VirtualAlloc`에 대한 사용자 호출은 `WinPEBImportPass`를 통합니다. 화이트리스트는 다중 DLL 테이블입니다:

- `Tables/Win32Apis.def`에 정의
- 각 행: `NEVERC_WIN32_API(ApiName, "dll.dll")`
- 리졸버는 2매개변수 `__neverc_win_resolve(dll_hash, api_hash)`로 임의 DLL 지원

**API 추가** (예: `DeviceIoControl`):
1. `Win32Apis.def`에 1행 추가
2. `lib/Headers/windows.h`의 shellcode 분기에 선언 추가
3. pass 변경 불필요

**새 DLL 버킷 추가** (예: `winhttp.dll`):
- `Win32Apis.def`에 새 DLL 이름의 행 추가만 하면 됨

### 3. 해당 추출기 확장

3가지를 처리:
1. 재배치 유형 식별 → 바이트 패치 또는 거부
2. 금지 데이터 섹션 이름 목록 업데이트 (새 OS에 고유 섹션이 있을 수 있음)
3. 진입 오프셋 0 재배치 대상 범위 검증 업데이트

완전히 새로운 오브젝트 형식 (예: WASM 모듈):
1. `ObjectFormat` 열거값 추가
2. `ShellcodeExtractor.cpp`의 디스패치 switch에 case 추가
3. `<Format>Extractor.cpp` 작성 (`ELFExtractor.cpp` 구조 참조)

### 4. Loader 추가 (테스트 도구 전용)

- `tests/neverc/shellcode/loader_linux.c`와 `loader_windows.c` 참조
- 일반적: `mmap(RWX) → memcpy → icache flush → call`

### 5. 테스트 업데이트

- `tests/neverc/ShellcodeCrossTargetTests.cpp`에 크로스 컴파일 체크 추가
- CI에서 해당 플랫폼 실행 가능하면 loader 왕복 테스트 추가

---

## 알려진 크로스 플랫폼 주의사항

- **엔디안**: NeverC는 리틀 엔디안(LE)만 지원, 모든 주류 타겟 커버.
- **ABI 차이**: Win64(rcx/rdx/r8/r9) vs System V AMD64(rdi/rsi/rdx/rcx/r8/r9)는 완전히 다른 인수 레지스터. Clang 프론트엔드 레이어에서 처리; shellcode 파이프라인은 관여 불필요.
- **syscall 번호**: Linux에서 아키텍처별 상이, Android는 Linux와 동일, Darwin은 자체 BSD 번호, Windows는 안정적 번호 없음(PEB walk). 테이블에서 (OS, arch)로 인덱스.
- **캐시 일관성**: ARM은 명시적 i-cache flush 필요; x86은 불필요. macOS arm64 JIT는 `pthread_jit_write_protect_np`도 필요; Linux arm64는 `__builtin___clear_cache`; Windows는 `FlushInstructionCache` (x86에서 no-op).
- **SELinux / W^X**: Android는 SELinux `execmem`으로 제약; 비탈옥 iOS는 `mmap(RWX)` 완전 거부, `MAP_JIT` + 코드 서명 필요.

## 향후 확장 로드맵

| 타겟 | 예상 공수 | 의존성 |
|------|----------|--------|
| **iOS arm64** (탈옥 / `MAP_JIT`) | 1일 | Mach-O 추출기 재사용, loader 수정 |
| **FreeBSD / OpenBSD x86_64** | 반나절 | ELF 추출기 재사용 + 새 syscall 테이블 |
| **RISC-V64 Linux** | 2일 | RISC-V TargetDesc + 새 AllBlr 변형 + RISC-V 재배치 패치 필요 |

## 난독화 Pass 확장 인터페이스

shellcode 파이프라인은 `Pipeline.h::ObfuscationHooks`를 통해 서드파티 난독화 라이브러리에 11개 훅을 노출합니다:

```
PipelineStartEP:
  RunBeforePrep → [ZeroReloc Prep] → RunAfterPrep →
  [IndirectBr → MemIntrin → CompilerRt → SyscallStub →
   WinPEBImport → KernelImport → Data2Text phase 1] →
  RunBeforeInlining

OptimizerLastEP:
  RunAfterInlining → [Data2Text phase 2 → ZeroReloc Stackify] →
  RunAfterStackify → [AllBlrPass] → RunAfterFinalIR

MIR: RunBeforePreEmit → [MIRPrepPass] → RunAfterPreEmit →
     [LLVM addPreEmitPass/addPreEmitPass2] → RunAfterFinalMIR

바이트스트림: RunPostExtract → [finalize 체인] → RunPostFinalize
```

IR 수준 사용법:
```cpp
neverc::shellcode::ObfuscationHooks H;
H.RunAfterInlining = [](llvm::ModulePassManager &MPM,
                        const neverc::shellcode::ShellcodeOptions &Opts) {
  MPM.addPass(MyCFFPass(Opts.ObfuscateSpec));
};
neverc::shellcode::setShellcodeObfuscationHooks(std::move(H));
```

MIR 수준 사용법:
```cpp
H.RunAfterPreEmit = [](llvm::TargetPassConfig &TPC,
                       const neverc::shellcode::ShellcodeOptions &Opts) {
  TPC.addExternalPass(new MyInstructionSubstitutionPass(Opts.MirObfuscateSpec));
};
```

내장 MIR 패치도 테이블 기반: `Tables/MIRRewritePatterns.def`에 패턴 진단 이름, 아키텍처 필터, 헬퍼 이름 기록; `Tables/MIRRewriteOpcodes.def`에 백엔드 연산 코드 이름 기록. 새 shellcode 친화적 백엔드 형태 추가 시 pass 본문에 타겟 특정 분기를 분산시키지 말고 테이블 항목과 좁은 범위 헬퍼 추가를 우선하십시오.

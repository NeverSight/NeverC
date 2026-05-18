**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode 컴파일러](../README.ko.md)

# Shellcode 컴파일러 — 진행 상황 추적

## 단계 0 — macOS arm64 MVP (완료)

- [x] 디렉터리 및 CMake 스켈레톤 (`nevercShellcode` 라이브러리)
- [x] `ZeroRelocPass`: 2단계 (Prep + Stackify), 가변 전역 변수 자동 스택화
- [x] `Data2TextPass`: 2단계 (상수 배열 → 스택 청크 스토어; SROA 후 벡터 상수 분할; ConstantFP → volatile 로드 비트 패턴)
- [x] `SyscallStubPass`: 테이블 기반 화이트리스트, Darwin BSD / Linux arm64 / Linux x86_64 / Android syscall 지원
- [x] `AllBlrPass`: 선택적 적극적 간접 호출 재작성
- [x] `ShellcodeExtractor`: Mach-O `.o` → 플랫 `.bin`, 섹션 내 재배치 패치 포함
- [x] CLI 옵션 (생성된 `neverc/include/neverc/Invoke/Options.td.h` 경유): `-fshellcode`, `-fshellcode-all-blr`, `-mshellcode-syscall`, `-fshellcode-keep-obj=`, `-fshellcode-entry=`
- [x] 전 플랫폼 기본 PIC (`isPICDefault()`가 일괄 `true` 반환)
- [x] 범용 재귀 스택화 (함수 포인터 테이블, 문자열 포인터 테이블, 중첩 구조체 테이블, ConstantExpr GEP/BitCast 초기화자)
- [x] `IndirectBrPass`: GCC computed-goto (`&&label`) → switch, 다중 디스패치 사이트 테이블 공유 포함
- [x] SIMD 벡터 상수 인라인 (`inlineVectorConstants`)
- [x] `_Thread_local` 자동 static 강등
- [x] 네이티브 macOS arm64 로더 (MAP_JIT + i-cache flush)

**테스트**: 108/108 shellcode 어설션 통과. 바이너리 크기: `add` 8B, `fib` 64B, `hello` 64B, `big_const` 632B.

## 단계 1 — Linux / Android / Windows 크로스 플랫폼 (완료)

- [x] `TargetDesc` 추상화: 테이블 기반 플랫폼 차이
- [x] 크로스 플랫폼 `-mshellcode-syscall` 시맨틱 (Darwin 전용 `-mshellcode-libsystem` 대체)
- [x] Linux / Android syscall 번호 테이블 (Darwin BSD 80+, Linux arm64 60+, Linux x86_64 90+)
- [x] `ShellcodeExtractor`를 `MachOExtractor` / `ELFExtractor` / `COFFExtractor`로 리팩터링
- [x] ELF 추출기 (arm64: `R_AARCH64_CALL26`/`JUMP26`/`ADR_PREL_PG_HI21`/등; x86_64: `R_X86_64_PC32`/`PLT32`)
- [x] COFF 추출기 (arm64: `IMAGE_REL_ARM64_BRANCH26`/등; x86_64: `IMAGE_REL_AMD64_REL32`/등)
- [x] Windows PEB 임포트 패스 (`WinPEBImportPass`), 실제 PEB walk 리졸버 포함
- [x] 멀티 DLL Win32 API 화이트리스트 (kernel32/ntdll/user32/ws2_32/advapi32/shell32에 걸쳐 ~190 API)
- [x] `MemIntrinPass`: memcpy/memset/memmove/memcmp/bcmp/bzero/memchr + strlen/strcpy/strcmp/등 → 인라인 바이트 루프 헬퍼
- [x] `CompilerRtPass`: `__int128` 나눗셈/나머지 → 인라인 장나눗셈 헬퍼
- [x] Windows `aarch64-pc-windows-msvc` 프런트엔드 지원
- [x] `MIRPrepPass`: 크로스 플랫폼 의사 명령어 제거 (CFI/EH/XRay/StackMap/SEH/FENTRY/등)
- [x] MIR + 바이트 수준 난독화 훅 (IR/MIR/바이트스트림 3계층 총 11개 훅)
- [x] AArch64 비 Darwin `long double` binary64 자동 다운그레이드
- [x] Shellcode 심 헤더: `<windows.h>`, `<unistd.h>`, `<fcntl.h>`, `<sys/stat.h>`, `<sys/mman.h>`, `<string.h>`, `<stdlib.h>`
- [x] Windows POSIX 호환 레이어 (13개 POSIX→Win32 브리지: write→WriteFile, mmap→VirtualAlloc 등)
- [x] K&R 암시적 선언 자동 수정 (50+ 표준 POSIX 시그니처)
- [x] 테이블 기반 정제 (아키텍처 분기 하드코딩 → 제로)
- [x] `KernelImportPass`: ring-0 자동 리졸버 기반 호출 사이트 재작성
- [x] 커널 헬퍼 이름 테이블 기반 진단 (`KernelHelperNames.def`)
- [x] `<neverc/kernel.h>`: ring-0 진입점 규약용
- [x] 진입점 오프셋 0 강제 (`placeEntryFirst`)
- [x] 파이널라이즈 파이프라인: 배드바이트 리라이터 SDK + 문자 집합 인코더 SDK + 크기 제약
- [x] 플러그인 SDK (`Plugin.h`): `registerBadByteRewriteStrategy` + `registerCharsetEncoder`
- [x] x86_64 `-mno-implicit-float` 주입 (백엔드 SSE 상수 풀 스필 방지)
- [x] 크로스 플랫폼 로더 (macOS/Linux/Windows)

**테스트**: 743+ shellcode 어설션, 8개 트리플 모두 통과. NeverC 전체 테스트 스위트: 1000+ 테스트 통과.

## 단계 2 — 인쇄 가능 / 영숫자 인코더 (예정)

- [ ] ARM64 인쇄 가능 shellcode 인코더 (0x20–0x7e 명령어 서브셋)
- [ ] x86_64 영숫자 인코더
- [ ] 자체 디코딩 스텁 (decoder stub) 생성
- [ ] 인코딩 후 크기 / 엔트로피 통계

## 단계 3 — 다형성 / 자기 수정 (예정)

- [ ] 다형성 엔진: 동일 소스 → 컴파일마다 다른 동등 바이트 시퀀스
- [ ] 자기 수정 코드: 런타임 페이로드 본문 복호화 / 압축 해제
- [ ] 탐지 회피: 알려진 shellcode 시그니처 패턴 회피

## 향후 확장

- [ ] iOS arm64 (코드 서명 + JIT 탈옥 시나리오)
- [ ] Cortex-M / Thumb
- [ ] RISC-V 64

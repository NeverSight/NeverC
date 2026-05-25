**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode 컴파일러](../README.ko.md)

# IR Pass 설계 — 원칙, 파이프라인, 전후 비교

> 이 문서는 shellcode 컴파일 파이프라인의 각 pass의 **설계 근거**를 설명합니다. 구현 세부사항은 `.cpp` 소스의 영문 주석에 있습니다.

---

## 0. 핵심 아이디어

shellcode의 목표 한 문장: **`.o`에서 재배치가 될 모든 것을 제거하고, `mmap(RWX)` + `memcpy` + `blr`할 수 있는 순수한 명령 스트림만 남긴다.**

이 제약을 사용자에게 노출하지 않으려 함 — 사용자는 일반 C를 쓰고, 파이프라인이 재배치를 생성하는 IR 구조를 내부적으로 처리합니다.

| Pass | 기능 | 실행 시점 |
|------|------|---------|
| ZeroRelocPass (Prep) | 링키지 통일 / always_inline 강제 / 하드 블로커 거부 | PipelineStart |
| IndirectBrPass | computed-goto `indirectbr` → `switch` | PipelineStart |
| MemIntrinPass | `@llvm.mem*` + 명시적 mem*/str*/abs → 내부 바이트 루프 헬퍼 | PipelineStart |
| StringRuntimePass | 내장 `string` 타입 런타임 → 스택 arena 변형 | PipelineStart |
| HeapArenaPass | `malloc`/`free`/`calloc`/`realloc` → arena 할당 + 대형 할당 OS 폴백 | PipelineStart |
| CompilerRtPass | `__udivti3` 계열 + i128 div/rem → 내부 128비트 긴 나눗셈 | PipelineStart |
| SyscallStubPass | libc extern → TargetDesc 테이블 기반 커널 트랩 래퍼 | PipelineStart |
| WinPEBImportPass | Win32 extern → PEB 모듈 워크 + PE 익스포트 리졸버 + 암호화 주소 캐시 | PipelineStart |
| KernelImportPass | Ring-0 extern → 리졸버 지원 간접 호출 + 암호화 주소 캐시 (커널만) | PipelineStart |
| Data2TextPass (Phase 1) | 상수 GV → 즉시값 / 스택 청크 스토어 | PipelineStart |
| *(LLVM 표준 최적화)* | AlwaysInliner / SROA / InstCombine / SLP | O-level |
| Data2TextPass (Phase 2) | SROA 잔여 벡터 store 분할, 지연 상수 소비 | OptimizerLast |
| ZeroRelocPass (Stackify) | 변경 가능 전역 → 진입 alloca + 최종 검증 | OptimizerLast |
| AllBlrPass (선택) | 직접 호출 → 간접 호출 | OptimizerLast |
| MIRPrepPass | MIR 캐치올: CFI/EH/XRay/SEH 의사 명령 제거 | Before addPreEmitPass |
| ShellcodeExtractor | `.o` 스캔 최종 감사 + flat `.bin` 출력 | 후처리 |

---

## 1. ZeroRelocPass

### 1.1 Prep — 링키지 통일
모든 비진입 함수 → `internal` + `alwaysinline`. 하드 블로커 거부. `_Thread_local` 조용히 static으로 강등.

### 1.2 Stackify — 전역 변수 제거
변경 가능 GV → 진입 함수의 `alloca`. 최종 검증에서 잔여 GV 거부.

### 1.3 `placeEntryFirst`
진입 함수를 `.bin` 오프셋 0에 배치.

## 2. IndirectBrPass
GCC computed-goto → `switch` (제로 재배치).

## 3. SyscallStubPass
libc extern → TargetDesc 기반 인라인 어셈블리. POSIX 호환 계층. K&R 자동 수정.

## 4. WinPEBImportPass
Win32 extern → PEB 워크 리졸버 (~190 API, 6 DLL). Windows POSIX 호환 (13 함수 그룹).

### 4.1 주소 캐시 암호화

해석된 API 주소는 캐시 전역 변수에 저장하기 전에 암호화됨 (메모리 스캔 방지). 기본값은 XOR 명령어 없는 산술 분해 `(a + b) - 2*(a & b)` + `volatile` 중간값으로 LLVM의 `xor` 재최적화를 방지. 암호화 인프라 (`PtrCacheHelpers.h`)는 `WinPEBImportPass`와 `KernelImportPass`가 공유.

**3개의 플러그형 헬퍼 함수** (모두 `internal alwaysinline`):

| 함수 | 시그니처 | 용도 |
|------|---------|------|
| `__sc_derive_key` | `() → i64` | 런타임 암호 키 파생 |
| `__sc_ptr_encrypt` | `(ptr) → i64` | 함수 포인터를 캐시 저장용으로 암호화 |
| `__sc_ptr_decrypt` | `(i64) → ptr` | 캐시 값을 함수 포인터로 복호화 |

**기본 구현**: XOR 명령어 없는 산술 분해. `key = (PEB + seed) - 2*(PEB & seed)` (Windows 사용자 모드) 또는 순수 시드 (커널). `encrypt/decrypt = (a + b) - (a & b) - (b & a)`, `volatile` 중간값으로 LLVM의 `xor` 재최적화 방지.

**캐시 슬롯**: `@__sc_cache_<dll>_<api>` (i64, 초기값 0, `.text` 섹션, 8바이트 정렬). Fast/Slow 경로: fast path (`atomic_load → decrypt → 간접 호출`, ~10 명령), slow path (PEB 워크 → `encrypt → cmpxchg weak`). lock-free 스레드 안전.

**오버라이드**: 소스 코드에서 동일 이름 함수 정의. `encrypt`/`decrypt`는 수학적 역연산이어야 하며, `always_inline` 필수, 외부 함수 호출 불가.

## 5. MemIntrinPass
memcpy/memset/strlen/strcpy 등 → `internal alwaysinline` 바이트 루프 헬퍼.

## 6. CompilerRtPass
`__int128` 나눗셈/나머지 → 인라인 시프트 감산 긴 나눗셈.

## 7. Data2TextPass
Phase 1: 상수 GV → 즉시값/스택 스토어. ConstantFP → volatile 비트 패턴.
Phase 2: SROA 잔여 벡터 store 분할. 벡터 상수 인라인화.

## 8. AllBlrPass (선택)
직접 호출 → volatile 슬롯 + `blr xN` / `call *rax` 간접 호출.

## 9. 난독화 훅
11개 훅 포인트. [plugin-interface.md §6](../plugin-interface/README.ko.md#6-registration-position-selection--pic-coverage-matrix) 참조.

## 10. 2단계 설계 근거
Phase 1은 원본 IR 정리. LLVM 최적화 후 Phase 2는 최적화기가 도입한 새 구조를 정리.

## 11. KernelImportPass (ring-0만)
미해결 extern → 암호화 주소 캐시 포함 리졸버 간접 호출로 자동 리라이트. `(resolver, cookie)` 암시적 매개변수 주입. 각 API 해석 결과는 암호화 후 캐시되며, 이후 호출은 캐시 사용 (lock-free `cmpxchg`). 암호화 메커니즘은 WinPEBImportPass와 공유 ([§4.1](#41-주소-캐시-암호화) 참조). [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.ko.md) 참조.

## 12. StringRuntimePass
내장 `string` 메서드 → 스택 arena 변형.

## 13. HeapArenaPass

셸코드 내 `malloc`/`free`/`calloc`/`realloc` 호출을 하이브리드 할당 전략으로 재작성 (기본 활성화, `-fshellcode-heap-arena` / `-fno-shellcode-heap-arena`로 제어):

- **소규모 할당 (≤ 64 KB)**: `StringRuntimePass`의 스택 상주 arena에서 할당. `__sc_string_arena` 공유로 스택 사용량 증가 방지.
- **대규모 할당 (> 64 KB) 또는 arena OOM**: OS 할당자로 폴백 (Windows: msvcrt.dll, Linux/macOS: mmap syscall).

**안전성**: `free(NULL)`은 no-op, `calloc`은 `llvm.umul.with.overflow`로 오버플로 검사, `realloc`은 포인터 출처 (arena / fallback)에 따라 올바른 이전 크기 읽기.

---

## 14. 오류 진단 철학
각 하드 오류는 정확히 **1개의 실행 가능한 진단**을 생성. `__neverc_shellcode_hard_error` 메타데이터 센티넬이 캐스케이드를 방지. 사용자에게 1개의 명확한 오류와 수정이 표시되고, 3개의 캐스케이드 메시지는 나오지 않습니다.

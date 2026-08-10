**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 내장 런타임 시스템](../README.ko.md)

# 컴파일 타임 문자열 암호화 (`xorstr`)

## 개요

NeverC는 C 코드를 위한 2계층 컴파일 타임 문자열 암호화를 제공합니다. API 이름, 레지스트리 경로, 디버그 메시지 등 민감한 문자열이 컴파일된 바이너리에 평문으로 남지 않도록 설계되었습니다.

- **레이어 1 — 명시적 매크로**: `NC_XORSTR("string")` / `NEVERC_XORSTR("string")`으로 문자열별 정밀 제어
- **레이어 2 — 자동 IR 패스**: `-fencrypt-call-strings`로 함수 호출의 모든 문자열 인수 자동 암호화

두 레이어 모두 스택 할당 버퍼(힙 할당 없음), 인스턴스별 키 스트림, volatile 정리를 사용합니다. 네이티브 기계어 경계에서는 명시적 `NC_XORSTR` 디코더 호출을 다시 암호화해 각 호출 지점에 직접 펼치므로 최종 오브젝트에 공유 디코더 함수가 남지 않습니다.

---

## 빠른 시작

### 레이어 1: 명시적 매크로

```c
#include <neverc/xorstr/xorstr.h>

FARPROC addr = GetProcAddress(hModule, NC_XORSTR("NtQuerySystemInformation"));
```

### 레이어 2: 자동 암호화

```bash
neverc -fencrypt-call-strings main.c -o main
```

---

## 레이어 1: `NC_XORSTR` / `NEVERC_XORSTR` 매크로

모든 문자열 리터럴 유형(일반, UTF-8, 와이드, UTF-16, UTF-32) 지원. 비 리터럴 인수는 컴파일 오류 발생.

### 보호 흐름

1. **Sema**가 각 리터럴을 독립 키로 암호화합니다. seed `0`은 운영체제에서 새 엔트로피를 얻고, `-fstring-encrypt-key=`는 결정적인 64비트 출력을 선택합니다.
2. **중간 IR / LTO 입력**은 불투명하고 특수화할 수 없는 디코더 호출을 유지하여 일반 최적화나 LTO가 평문을 IR에 다시 접어 넣지 못하게 합니다.
3. **최종 기계어 경계**에서 컴파일러 측 ciphertext를 복호화하고 다시 암호화한 뒤, 호출 지점별 루프 형태를 선택해 그 자리에 펼칩니다. 이후 디코더, helper 그래프, ABI anchor, route state, 의미가 드러나는 이름을 제거합니다.
4. **정리**는 최적화/provider 전달 전과 최종 tail에 모두 삽입됩니다. 두 번째 실행은 멱등이며 CFG 변경 뒤 배치를 복구합니다.

### 디코더 다양화

상태 스케줄, 상수, ciphertext, 동등한 바이트 식은 seed와 호출 지점에 따라 달라집니다. `a + b − 2 × (a & b)`는 가능한 형태 중 하나입니다. volatile 상태/ciphertext 로드는 상수 폴딩을 억제하고, `nooutline`은 IR finalization 뒤 Machine Outliner가 공유 디코더를 다시 만드는 것을 막습니다.

따라서 IDA가 한 번만 식별하거나 에뮬레이션할 수 있는 안정적인 단일 루틴이 없습니다. 다만 실행 중 필요한 평문까지 동적 instrumentation으로 관찰할 수 없다는 뜻은 아닙니다.

---

## 레이어 2: `-fencrypt-call-strings`

| 플래그 | 설명 | 기본값 |
|--------|------|--------|
| `-fencrypt-call-strings` | 자동 암호화 활성화 | 꺼짐 |
| `-fno-encrypt-call-strings` | 비활성화 | — |
| `-fencrypt-call-strings-max-len=N` | N 바이트 초과 문자열 건너뛰기 | 1024 |

이 변환은 IPO 전, 일반 최적화 후, 그리고 일반 또는 plugin 제공 late IR 단계가 끝날 때마다 실행됩니다. LTO도 provider hook과 pre-codegen hook 뒤에 동일한 필수 sealing을 적용합니다.

컴파일러 소유 private `unnamed_addr` 리터럴에서 유래한 직접·간접 `CallBase` 인수를 처리하며 GEP, cast, `freeze`, `select`, PHI, 승격 가능한 로컬 포인터 슬롯의 의미를 보존합니다. intrinsic, inline asm, 외부에서 보이는 배열이나 사용자 정의 배열, 길이 제한을 넘는 리터럴은 제외합니다. 보호 리터럴을 `musttail`로 전달하면 안전하게 컴파일 오류를 냅니다.

## 스택 정리 (`XorStrCleanupPass`)

도달 가능한 모든 `ret`, `resume`, 호출자로 unwind하는 `cleanupret`, 포착되지 않은 `catchswitch` unwind 전에 전체 버퍼를 volatile `memset`으로 지웁니다. 완전히 추적할 수 없거나 안전하지 않은 저장소는 일부만 지우지 않고 거부합니다.

---

## `.encrypt()`와의 비교

| 측면 | `NC_XORSTR()` | `.encrypt()` |
|------|---------------|--------------|
| **사용 가능성** | 순수 C (헤더 포함) | NeverC 구문 확장만 |
| **메모리** | 스택 (`alloca`) | 힙 (`NEVERC_STRING_ALLOC`) |
| **반환 타입** | `const char*` | `string` (값 타입) |
| **사용 사례** | Win32 API, FFI | 일반 문자열 조작 |

---

## 컴파일러 플래그 참조

| 플래그 | 설명 |
|--------|------|
| `-fencrypt-call-strings` | 함수 호출 인수의 자동 문자열 암호화 활성화 |
| `-fno-encrypt-call-strings` | 자동 암호화 비활성화 |
| `-fencrypt-call-strings-max-len=N` | 자동 암호화 최대 바이트 길이 (기본: 1024) |
| `-fstring-encrypt-key=0xHEX` | 전체 64비트 seed 지정. `0`은 새 엔트로피 사용 |

## 출력 경계와 재현성

- `-fno-lto`는 frontend 네이티브 코드 생성 시 finalization합니다.
- Auto-LTO와 Full LTO는 pre-link bitcode에 불투명 디코더를 유지하고 전체 프로그램 및 plugin IR 최적화 뒤에 다시 암호화하고 펼칩니다.
- provider 교체 pipeline과 late plugin pass 뒤에는 항상 암호화, 정리, finalization tail이 이어집니다.
- 기본 seed에서는 독립 네이티브 빌드가 서로 다르며, 이전 보호 코드를 재사용할 수 있는 whole-link/partition cache를 우회합니다.
- 0이 아닌 seed는 의도적으로 결정적이며 cache 가능합니다. 동일한 입력과 동일한 전체 64비트 seed는 동일한 보호 코드를 만듭니다.
- `-emit-llvm`과 pre-link bitcode는 중간 산출물이므로 불투명 decoder ABI를 의도적으로 유지합니다. “공유 디코더 없음” 보장은 성공적으로 생성된 최종 기계어에 적용됩니다.

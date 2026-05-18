**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode 컴파일러](../README.ko.md)

# 커널 모드 (Ring-0) Shellcode 지원

`-fshellcode`는 원래 ring-3 페이로드만 커버했습니다. Ring-0 페이로드 (Windows 드라이버, Linux/Android 커널 모듈, macOS kext)는 ring-3 ABI를 재사용할 수 없습니다: TEB/PEB가 존재하지 않고, syscall 명령은 사용자→커널 트랩이며, x86_64에서는 코드 모델과 레드존 비활성화가 추가로 필요합니다.

## 1. 핵심 스위치: `-mshellcode-context={user,kernel}`

- **사용자 모드** (기본): 기존 PEB / syscall stub 파이프라인 유지.
- **커널 모드**: SyscallStubPass / WinPEBImportPass 비활성화, 커널 플래그 주입, KernelImportPass 활성화, `__NEVERC_SHELLCODE_KERNEL__` 주입.

## 2. `TargetDesc` 새 필드

`Level` (User/Kernel), `KernelImport`, `KernelInjectFlags`. 커널 지원 추가 = "테이블 1행 추가".

## 3. 플랫폼별 드라이버 플래그 차이

| 차원 | x86_64 커널 | AArch64 커널 |
|------|------------|-------------|
| 레드존 | `-mno-red-zone` | 자연적으로 없음 |
| 코드 모델 | `-mcmodel=kernel` | 기존 `-mcmodel=small` 재사용 |
| 암시적 SIMD | `-mno-sse -mno-sse2 -mno-mmx` | `-mgeneral-regs-only` |

## 4. `KernelImportPass`: 자동 리졸버 주입

미해결 extern 직접 호출을 리졸버 지원 간접 호출로 자동 리라이트. 사용자는 일반 C를 작성. 암시적 `(resolver, cookie)` 매개변수를 진입에 주입. FNV-1a 64비트 해시. 3계층 방어 (IR → MIR → 추출기).

## 5–6. Android 커널, 헤더 분할

Ring-3은 bionic + Linux syscall ABI; Ring-0은 순수 Linux 커널. `<neverc/kernel.h>`가 커널 모드 API 제공.

## 7. Ring-0 Shellcode 작성

### 7.1 순수 계산 페이로드
```c
#include <neverc/kernel.h>
NEVERC_KERNEL_ENTRY
int shellcode_entry(int a, int b) { return a * 13 + b * 7; }
```

### 7.2 리졸버 기반 페이로드
`neverc_kern_resolve_t resolver`와 `neverc_kern_hash("printk")`로 커널 함수 해결.

## 8. 로드맵

커널 컨텍스트 전환, 리졸버 리라이트, 순수 계산/리졸버 페이로드 모두 완료. 커널 SDK 헤더 서브셋 계획 중.

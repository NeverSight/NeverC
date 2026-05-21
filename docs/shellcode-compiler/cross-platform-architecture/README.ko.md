**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode 컴파일러](../README.ko.md)

# NeverC Shellcode 크로스 플랫폼 아키텍처 개요

이 문서는 "하나의 pass 세트로 macOS / Linux / Android / Windows × arm64 / x86_64 × User / Kernel을 커버"하는 설계 원칙을 설명합니다. 새 플랫폼이나 컨텍스트로 확장하기 전에 읽어주세요.

관련 하위 시스템 문서:
- [README.md](../README.ko.md) — 개요, CLI 옵션, 빠른 시작
- [ir-pass-design.md](../ir-pass-design/README.ko.md) — IR 계층 pass 책임과 예시
- [mir-pass-design.md](../mir-pass-design/README.ko.md) — MIR 계층 prep pass + 난독화 훅
- [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.ko.md) — 커널 컨텍스트 설계 세부사항
- [platform-extension-guide.md](../platform-extension-guide/README.ko.md) — 새 플랫폼 추가 단계별 가이드

---

## 1. 3차원 매트릭스: OS × Arch × ExecutionLevel

모든 크로스 플랫폼 차이가 **3차원 매트릭스**로 수렴하며, 각 셀이 `TargetDesc` 테이블 항목에 해당합니다:

```
                ┌──── arm64 ────┬──── x86_64 ────┐
     Darwin ────┤ User / Kernel │ User / Kernel  │  Mach-O
     Linux  ────┤ User / Kernel │ User / Kernel  │  ELF
     Android────┤ User / Kernel │ User / Kernel  │  ELF
     Windows────┤ User / Kernel │ User / Kernel  │  COFF
                └───────────────┴────────────────┘
```

8 (OS, arch) × 2 ExecutionLevel = **16 테이블 항목**.

**핵심 설계 철학**: pass는 항상 테이블에서 읽고, `if (OS == Darwin)` 분기를 쓰지 않습니다. 새 플랫폼 추가 = `describeTriple()`에 1행 + 각 추출기 switch에 1 case.

## 2. 파이프라인 실행 순서

`-fshellcode` 활성화 시 컴파일러는 고정 순서를 따릅니다. **각 단계에 난독화 훅**이 있습니다.

2가지 핵심 불변량:
1. **백엔드 TableGen `.td`가 명령 설명의 유일한 소스**.
2. **shellcode에 외부 재배치와 데이터 섹션이 없음**.

## 3. 글로벌 PIC 전략

`isPICDefaultForced()`가 모든 3개 ToolChain에서 **무조건 true** 반환.

## 4. User / Kernel 직교 차원

- **User** (기본): PEB 워크 / syscall stub 파이프라인.
- **Kernel**: SyscallStubPass / WinPEBImportPass 쇼트서킷; KernelImportPass 활성화.

## 5. 사용자 모드 "일반 C" 지원 매트릭스

`-fshellcode` 사용 시 대형 배열, 부동소수점 상수, computed-goto, memcpy/strlen, `__int128` 나눗셈, 원자 연산, POSIX/Win32 헤더 등이 **사용자 인식 없이 직접 지원**.

## 6. MIR 계층: 수정 / 폴백 / 추출 3단계 파이프라인

1. 크로스 플랫폼 의사 명령 정리
2. Shellcode 친화적 명령 리라이트 (테이블 기반)
3. 외부 참조 / 상수 풀 감사

## 7. 추출기 계층

`ObjectFormat`으로 디스패치. 공통 계약: "intra-`.text` PC-rel 패치 수락, 나머지 전부 거부".

## 8. 난독화 훅 포인트

11개 훅(6 IR + 3 MIR + 2 바이트 수준)이 모든 파이프라인 단계를 커버.

## 9. 새 (OS, Arch) 항목 추가

비용: TargetDesc 1행 + syscall 테이블 + 추출기 case + 테스트. IR/MIR pass 변경 제로.

## 10. 비목표

- C++ / ObjC / Rust 프론트엔드
- 32비트 / 빅엔디안 / 니치 ISA
- shellcode에 libc 런타임 임베딩 (`malloc`/`free`/`calloc`/`realloc`은 `HeapArenaPass`에서 처리)
- 절대 주소 재배치

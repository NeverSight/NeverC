**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 내장 런타임 시스템](../README.ko.md)

# 컴파일 타임 문자열 암호화 (`xorstr`)

## 개요

NeverC는 C 코드를 위한 2계층 컴파일 타임 문자열 암호화를 제공합니다. API 이름, 레지스트리 경로, 디버그 메시지 등 민감한 문자열이 컴파일된 바이너리에 평문으로 남지 않도록 설계되었습니다.

- **레이어 1 — 명시적 매크로**: `NC_XORSTR("string")` / `NEVERC_XORSTR("string")`으로 문자열별 정밀 제어
- **레이어 2 — 자동 IR 패스**: `-fencrypt-call-strings`로 함수 호출의 모든 문자열 인수 자동 암호화

두 레이어 모두 스택 할당 버퍼(힙 할당 없음), XOR 명령어 미사용 복호화 알고리즘(안티 시그니처), 함수 리턴 전 volatile `memset` 제로화를 사용합니다.

---

## 빠른 시작

### 레이어 1: 명시적 매크로

```c
#include <neverc/xorstr.h>

FARPROC addr = GetProcAddress(hModule, NC_XORSTR("NtQuerySystemInformation"));
```

### 레이어 2: 자동 암호화

```bash
neverc -fencrypt-call-strings main.c -o main
```

---

## 레이어 1: `NC_XORSTR` / `NEVERC_XORSTR` 매크로

모든 문자열 리터럴 유형(일반, UTF-8, 와이드, UTF-16, UTF-32) 지원. 비 리터럴 인수는 컴파일 오류 발생.

### 안티 시그니처 복호화

XOR 명령어를 완전히 우회하고 수학적으로 동등한 `a + b − 2 × (a & b)`를 사용합니다.

---

## 레이어 2: `-fencrypt-call-strings`

| 플래그 | 설명 | 기본값 |
|--------|------|--------|
| `-fencrypt-call-strings` | 자동 암호화 활성화 | 꺼짐 |
| `-fno-encrypt-call-strings` | 비활성화 | — |
| `-fencrypt-call-strings-max-len=N` | N 바이트 초과 문자열 건너뛰기 | 1024 |

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
| `-fstring-encrypt-key=0xHEX` | XOR 키 시드 덮어쓰기 |

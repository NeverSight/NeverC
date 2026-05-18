**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode 컴파일러](../README.ko.md)

# ARM64 (AArch64) 어셈블리 튜토리얼 — Shellcode 관점

> ARM64에 익숙하지 않은 독자를 위해, shellcode 컴파일러가 생성하는 명령에 초점을 맞춥니다. 각 명령에 주석과 전후 비교를 포함합니다.

## 1. 레지스터 개요

```
범용 레지스터:
  x0 ~ x30    64비트 범용
  w0 ~ w30    대응하는 하위 32비트 별칭
  x29 (fp)    프레임 포인터
  x30 (lr)    링크 레지스터 (반환 주소 보관)
  sp          스택 포인터 (x31이 아님!)
  xzr / wzr   제로 레지스터

특수 레지스터:
  pc          프로그램 카운터 (직접 쓰기 불가)

Apple ABI 예약:
  x16, x17    플랫폼 예약
  x18         TLS 베이스 (Apple 전용)

호출 규약 (AAPCS64):
  인수:       x0~x7 (정수), d0~d7 (부동소수점)
  반환값:     x0 (정수), d0 (부동소수점)
  피호출자 보존: x19~x28, x29, x30, sp
  호출자 보존: x0~x18, d0~d31
  레드존:     sp 아래 128바이트
```

## 2. 분기와 호출

- `b label` — 무조건 분기 (PC 상대 ±128MB)
- `bl label` — 링크 포함 분기 (imm26 → ARM64_RELOC_BRANCH26 생성)
- **shellcode가 `bl`을 피해야 하는 이유**: 26비트 즉시 오프셋을 링커가 채움. 외부 심볼에서는 재배치 발생. `blr`로 대체 필수.
- `br xN` / `blr xN` — 레지스터 간접 분기/호출
- `ret` — `br lr` 동등

## 3. PC 상대 주소 지정

`adr` (±1MB)과 `adrp + add` (±4GB 페이지 정렬). x86_64의 `lea rax, [rip + _sym]` 동등하지만 2개 명령으로 분할.

## 4. 즉시값 로드

`mov` + `movk` 시퀀스로 64비트 값 구성. **Data2TextPass의 핵심**: 상수 데이터를 mov/movk 시퀀스로 스택에 저장.

## 5. 메모리 접근

`ldr/str`, `ldp/stp` (쌍 로드/스토어), 프리/포스트 인덱스 주소 지정.

## 6. 산술 및 논리

`add`, `sub`, `and`, `orr`, `eor`, `lsr`, `lsl`.

## 7. 비교와 조건 분기

`cmp` + 조건 분기 (`b.eq`, `b.ne`, `b.lt`, `b.gt`), 조건 선택 (`csel`).

## 8. 이 프로젝트가 생성하는 전형적 명령 시퀀스

순수 계산 add, 재귀 피보나치, 문자열의 스택 인라인화 (Data2TextPass), 시스템 콜 (SyscallStubPass 직접 svc) 포함.

전체 명령 스트림이 `__TEXT,__text` 내에 100% 들어감 — 이것이 "진정한 shellcode".

## 9. 핵심 요약

| 개념 | x86_64 동등 | ARM64 | Shellcode 참고 |
|------|------------|-------|---------------|
| 함수 호출 | `call rel32` | `bl imm26` | 섹션 내: 추출기가 BRANCH26 패치 |
| 주소 로드 | `lea rax, [rip+sym]` | `adrp+add` | 섹션 내: PAGE21/PAGEOFF12 패치 |
| 64비트 즉시값 | `mov rax, imm64` | `mov+movk ×4` | 재배치 없음, Data2TextPass 핵심 |
| 프롤로그 | `push rbp; mov rbp,rsp` | `stp x29,x30,[sp,#-16]!` | 1개 명령으로 쌍 저장 |
| syscall | `syscall` | `svc #0x80` | Darwin BSD: x16=nr, x0~x7=args |

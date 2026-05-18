**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode 컴파일러](../README.ko.md)

# 로드맵

이 문서는 계획 중이거나, 진행 중이거나, 의도적으로 보류된 기능을 추적합니다.

## 현재 상태

NeverC의 shellcode 파이프라인은 다음을 포함합니다:

- 11개 이상의 전용 패스가 있는 완전한 LLVM IR 파이프라인
- COFF / ELF / Mach-O 추출기
- Win32 PEB-walk 임포트 해결 (ROR-13 해시, 6개 DLL 버킷)
- 직접 syscall 하강 (Darwin `svc #0x80`, Linux `svc #0` / `syscall`)
- 커널 모드 지원 (Windows, Linux)
- 구성 가능한 프로파일을 사용한 배드바이트 감사
- 배드바이트 리라이터 및 문자 집합 인코더용 플러그인 SDK
- 크기 / 정렬 / 패딩 제약 (`-fshellcode-max-length=`, `-fshellcode-align=`, `-fshellcode-pad=`)
- IR, MIR, 바이트스트림 3계층에 걸쳐 11개 난독화 훅

## 완료 (2026-04)

1. **크기 / 정렬 / 패딩 제약** — 내장 기능. `-fshellcode-max-length=`, `-fshellcode-align=`, `-fshellcode-pad=`는 `finalizeShellcodeBytes` 끝에서 실행. 드라이버는 모순된 구성을 거부 (예: 패드 바이트가 배드바이트 세트에 있거나, align/max-length 없이 pad 사용).

2. **배드바이트 리라이터 인터페이스** — 골격은 내장, 내장 전략 없음. `Plugin.h::registerBadByteRewriteStrategy`가 SDK를 노출. `-fshellcode-bad-byte-rewrite` / `-fno-...`가 파이널라이즈 체인의 리라이터 호출 여부를 제어. 비활성화 시 감사만 수행으로 폴백. 다운스트림 라이브러리가 Capstone 기반 또는 커스텀 리라이트 전략을 등록.

3. **문자 집합 인코더 인터페이스** — 골격은 내장, 내장 문자 집합 없음. `Plugin.h::registerCharsetEncoder`가 `(name, Encode, Stub, IsCharsetMember)` 튜플을 노출. `-fshellcode-charset=<name>` 설정 시 파이널라이즈 단계가 `.text`를 `Stub(target) || Encode(text, target)`로 교체하고 모든 출력 바이트를 문자 집합에 대해 검증. 인쇄 가능 / 영숫자 / 커스텀 인코더는 다운스트림 라이브러리가 등록.

## 계획 중 — 플러그인 레이어 (훅을 통해)

다음 기능은 **의도적으로 내장하지 않습니다**. 전략/난독화 레이어에 속하며, 훅 및 플러그인 인터페이스를 통해 서드파티 플러그인이 제공하도록 설계되었습니다.

| 기능 | 훅 위치 | 비고 |
|------|---------|------|
| 안티 디스어셈블리 | `RunBeforePreEmit` / `RunAfterPreEmit` / `RunAfterFinalMIR` | 명령어 접두사 간섭, 점프 재배열, 정크 바이트 삽입 |
| 다형성 | `RunAfterFinalMIR` / `RunPostExtract` | 시드 기반 컴파일별 출력 변화 |
| 단계별 인코더 (XOR / RC4 / 자체 복호화) | `RunPostExtract` / `RunPostFinalize` | 컴파일 타임 스텁 생성 + 페이로드 암호화 |
| 간접 syscall (Halos / Tartarus / Recycled Gate) | IR 수준 플러그인 또는 `RunPostExtract` | 런타임 ntdll 가젯 스캔 |
| 수면 마스크 / 콜 스택 스푸핑 | IR 패스 플러그인 | Ekko / FOLIAGE / Cronos 패턴 |
| ETW / AMSI 패치 | IR 패스 플러그인 | 런타임 패치 시퀀스 |
| 모듈 스톰핑 / 언후킹 | IR 패스 플러그인 | 메모리 조작 패턴 |

## 플러그인 훅 개요

3계층 총 11개 훅:

**IR 레이어 (6개 훅, `ModulePassManager &` 수신)**:
- `RunBeforePrep` — 모든 shellcode 패스 이전
- `RunAfterPrep` — 링키지 통합 이후
- `RunBeforeInlining` — AlwaysInliner 이전 마지막 기회
- `RunAfterInlining` — IR이 하나의 함수로 완전히 평탄화
- `RunAfterStackify` — 코드 생성 전 최종 IR 형태
- `RunAfterFinalIR` — `AllBlrPass` 이후, 절대적으로 마지막 IR 훅

**MIR 레이어 (3개 훅, `TargetPassConfig &` 수신)**:
- `RunBeforePreEmit` — 레지스터 할당 완료, CFI/EH 의사 명령어 존재
- `RunAfterPreEmit` — `MIRPrepPass` 정리 후, 최종 바이트에 가장 가까움
- `RunAfterFinalMIR` — LLVM `addPreEmitPass2()` 이후, AsmPrinter 직전

**바이트스트림 레이어 (2개 훅, `SmallVectorImpl<uint8_t> &` 수신)**:
- `RunPostExtract` — 프리 파이널라이즈, 리라이터/인코더/감사/사이징이 처리
- `RunPostFinalize` — 포스트 파이널라이즈, 디스크 기록 직전; NeverC는 이후 감사를 수행하지 않음

## 파이널라이즈 파이프라인

각 추출기는 `.bin` 기록 전에 `finalizeShellcodeBytes`를 호출:

```
applyPostExtractObfuscationHook       (ObfuscationHooks::RunPostExtract)
        |
runBadByteRewriters                   (Plugin.h::registerBadByteRewriteStrategy)
        |
runCharsetEncoder                     (Plugin.h::registerCharsetEncoder)
        |
auditFinalBadBytes                    (내장 하드 감사)
        |
applyShellcodeSizing                  (-fshellcode-align/-max-length/-pad)
```

사용법 및 코드 예제는 [plugin-interface.md](../plugin-interface/README.ko.md) 참조.

## 계획 없음

- **크로스 언어 프런트엔드** — NeverC는 자체 C23 프런트엔드만 수용. IR 파이프라인은 프런트엔드와 분리되어 있지만, 외부 비트코드 (예: `rustc` 또는 `zig`에서) 수용은 프로젝트 목표가 아님.

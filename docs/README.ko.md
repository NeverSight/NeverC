**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 프로젝트](i18n/README.ko.md)

> **Tip:** Use the language bar above; links on this page point to the same locale (shellcode README and breadcrumbs).

# NeverC 문서

각 서브시스템의 설계 노트, API 레퍼런스, 가이드.

---

## Shellcode 컴파일러

Shellcode 컴파일 파이프라인은 NeverC의 핵심 연구 영역입니다. 아키텍처, CLI 옵션, 플랫폼 매트릭스, 예제:

**[Shellcode 컴파일러 →](shellcode-compiler/README.ko.md)**

| 문서 | 설명 |
|------|------|
| [README](shellcode-compiler/README.ko.md) | 개요, 빠른 시작, 지원 대상 |
| [Pipeline & PIC](shellcode-compiler/pipeline-and-pic/README.ko.md) | IR → 객체 → 추출 설계 |
| [IR Pass Design](shellcode-compiler/ir-pass-design/README.ko.md) | 각 IR 패스 설계 근거 |
| [MIR Pass Design](shellcode-compiler/mir-pass-design/README.ko.md) | 백엔드 MIR 패스 |
| [Kernel-Mode Shellcode](shellcode-compiler/kernel-mode-shellcode/README.ko.md) | Ring-0 컴파일 |
| [Cross-Platform Architecture](shellcode-compiler/cross-platform-architecture/README.ko.md) | `TargetDesc` 및 추출기 |
| [Platform Extension Guide](shellcode-compiler/platform-extension-guide/README.ko.md) | 새 플랫폼 추가 |
| [ARM64 Assembly Tutorial](shellcode-compiler/arm64-assembly-tutorial/README.ko.md) | shellcode 관점의 ARM64 명령어 |
| [Roadmap](shellcode-compiler/roadmap/README.ko.md) | 예정 작업 |
| [Progress](shellcode-compiler/progress/README.ko.md) | 구현 현황 |

---

## `.nc` 파일 확장자

NeverC는 `.nc`를 네이티브 소스 파일 확장자로 인식합니다. `.nc`를 사용하면 모든 NeverC 언어 확장(`-fneverc-types`, `-fbuiltin-string`)이 자동으로 활성화됩니다 — 추가 플래그 불필요.

**[`.nc` 확장자 →](nc-extension/README.ko.md)**

---

## 내장 런타임

NeverC는 LLVM bitcode로 임베디드된 내장 런타임으로 표준 C를 확장합니다. 각 `-fbuiltin-<name>` 플래그로 제어됩니다. `.nc` 파일에서는 `string`이 자동 활성화됩니다.

**[내장 런타임 시스템 →](builtins/README.ko.md)**

| 내장 기능 | 플래그 | 설명 |
|----------|--------|------|
| [내장 문자열](builtins/string/README.ko.md) | `-fbuiltin-string` | 값 의미론 `string` 타입, 도트 호출 메서드, 자동 메모리 관리, 네이티브 UTF-8 |
| [내장 mimalloc](builtins/mimalloc/README.ko.md) | `-fbuiltin-mimalloc` | `malloc`/`free`/`calloc`/`realloc` `mimalloc` 투명 고성능 할당자 오버라이드 |
| [문자열 암호화 (xorstr)](builtins/xorstr/README.ko.md) | `-fencrypt-call-strings` | 컴파일 타임 문자열 암호화, 스택 할당 XOR 복호화, 안티 시그니처 |

---

## 플러그인 API

NeverC는 아웃오브트리 패스 플러그인을 위한 순수 C ABI를 제공합니다. 플러그인은 공유 라이브러리(`.dll` / `.so` / `.dylib`)로, 파이프라인의 지정된 훅 포인트에 커스텀 패스를 등록합니다. 단일 헤더만 필요하며 LLVM/CRT 의존성이 없습니다.

**[플러그인 API →](plugin-api/README.ko.md)**

---

## 예제

NeverC 기능을 보여주는 빌드 가능한 샘플:

**[예제 →](../examples/)**

| 예제 | 설명 |
|------|------|
| [Windows 커널 드라이버](../examples/windows-driver/README.ko.md) | 최소 WDM 커널 드라이버, macOS/Linux에서 크로스 컴파일 |
| [Windows 드라이버 + CET](../examples/windows-driver-cet/README.ko.md) | 제어 흐름 강제 기술 커널 드라이버 |
| [Windows 드라이버 + 부동 소수점](../examples/windows-driver-float/README.ko.md) | 부동 소수점 지원 커널 드라이버 |

**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 프로젝트](../README.ko.md)

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
| [Plugin Interface](shellcode-compiler/plugin-interface/README.ko.md) | 난독화·인코딩 플러그인 |
| [Cross-Platform Architecture](shellcode-compiler/cross-platform-architecture/README.ko.md) | `TargetDesc` 및 추출기 |
| [Platform Extension Guide](shellcode-compiler/platform-extension-guide/README.ko.md) | 새 플랫폼 추가 |
| [ARM64 Assembly Tutorial](shellcode-compiler/arm64-assembly-tutorial/README.ko.md) | shellcode 관점의 ARM64 명령어 |
| [Roadmap](shellcode-compiler/roadmap/README.ko.md) | 예정 작업 |
| [Progress](shellcode-compiler/progress/README.ko.md) | 구현 현황 |

---

## 내장 문자열 타입

NeverC는 C 언어를 위한 내장 `string` 값 타입을 제공합니다. `std::string`의 사용 편의성과 `QString` 수준의 Unicode 지원을 C에 제공합니다. `-fbuiltin-string`으로 활성화 (`-fshellcode` 모드에서는 자동 활성화).

**[내장 문자열 →](builtin-string/README.ko.md)**

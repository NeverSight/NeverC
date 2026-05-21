**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 내장 런타임 시스템](../README.ko.md)

# 내장 `mimalloc` 할당자

## 개요

NeverC는 [mimalloc](https://github.com/microsoft/mimalloc)(Microsoft의 고성능 범용 메모리 할당자)을 LLVM bitcode 병합을 통해 컴파일된 바이너리에 직접 임베드할 수 있습니다. 활성화하면 `malloc`, `free`, `calloc`, `realloc`이 컴파일 시 mimalloc 구현으로 투명하게 대체됩니다.

**활성화:**

```bash
neverc -fbuiltin-mimalloc main.c -o main
```

---

## 사용법

```bash
neverc -fbuiltin-mimalloc hello.c -o hello                     # 기본
neverc -fbuiltin-string -fbuiltin-mimalloc main.c -o main      # `string`과 조합
neverc -fno-builtin-mimalloc main.c -o main                    # 비활성화
```

```c
#ifdef __NEVERC_MIMALLOC__
    printf("mimalloc 할당자 사용 중\n");
#endif
```

---

## 플랫폼 지원

| 플랫폼 | Triple | 상태 |
|--------|--------|------|
| Linux x86_64 | `x86_64-unknown-linux-gnu` | 지원 |
| Linux AArch64 | `aarch64-unknown-linux-gnu` | 지원 |
| Android | `aarch64-linux-android` | 지원 |
| macOS x86_64 | `x86_64-apple-macosx` | 지원 |
| macOS AArch64 | `arm64-apple-macosx` | 지원 |
| iOS | `arm64-apple-ios` | 지원 |
| Windows x86_64 (MSVC) | `x86_64-pc-windows-msvc` | 지원 |
| Windows AArch64 (MSVC) | `aarch64-pc-windows-msvc` | 지원 |

---

## 자동 억제

| 플래그 / 모드 | 이유 |
|--------------|------|
| `-fno-builtin` | CRT 함수 오버라이드 시나리오 없음 |
| `-mkernel` | 커널 모드에 유저스페이스 힙 없음 |
| `-fshellcode-mode` | HeapArenaPass로 대체 (arena + OS 폴백) |
| `-ffreestanding` | 오버라이드할 libc 없음 |

---

## 부트스트랩 빌드

```bash
ninja neverc                         # 스테이지 1: 빈 bitcode 플레이스홀더
ninja neverc-bootstrap-mimalloc-bc   # 스테이지 2: OS별 bitcode 컴파일
ninja neverc                         # 스테이지 3: 실제 bitcode 임베드
```

---

## 아키텍처

mimalloc은 LLVM bitcode로 컴파일러 바이너리에 임베드됩니다. 사용자 컴파일 시 Module Pass가 최적화 파이프라인 전에 bitcode를 사용자 IR에 병합합니다. OS별로 별도 컴파일(Linux `mmap`, macOS `vm_allocate`, Windows `VirtualAlloc`)되며, 병합 시 target triple에 따라 선택됩니다. **전체 아카이브** 시맨틱스 — 모든 함수가 링크됩니다.

---

## 파일 구조

```
neverc/
├── include/neverc/Foundation/Builtin/BuiltinMimalloc.h
├── lib/Foundation/Builtin/
│   ├── BuiltinMimalloc.cpp / gen_mimalloc_source.py / bin2c.py
├── lib/Emit/Backend/
│   ├── MimallocRuntimeLinker.{h,cpp} / BackendUtil.cpp
├── lib/Invoke/ToolChains/NeverC.cpp
└── lib/Compiler/Preprocessor/InitPreprocessor.cpp
```

---

## 컴파일러 플래그 참조

| 플래그 | 설명 |
|--------|------|
| `-fbuiltin-mimalloc` | `mimalloc` 오버라이드 주입 활성화 (호스트 빌드에서 기본 켜짐) |
| `-fno-builtin-mimalloc` | `mimalloc` 주입 명시적 비활성화 |

| 매크로 | 값 | 정의 조건 |
|--------|----|---------| 
| `__NEVERC_MIMALLOC__` | `1` | `-fbuiltin-mimalloc` 활성 시 |

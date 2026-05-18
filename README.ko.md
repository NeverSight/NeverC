**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

<div align="center">

# NeverC

**LLVM 기반의 보안 연구용 C23 컴파일러**

통합 링커 · Shellcode 파이프라인 · 내장 `string` 타입

[![AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](LICENSE)
[![C23](https://img.shields.io/badge/Standard-C23-brightgreen.svg)](#기능)
[![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-informational.svg)](#windows로-크로스-컴파일)
[![Arch](https://img.shields.io/badge/Arch-x86__64%20%7C%20AArch64-orange.svg)](#기능)

[문서 색인](docs/README.ko.md) · [Shellcode 가이드](docs/shellcode-compiler/README.ko.md) · [내장 문자열](docs/builtin-string/README.ko.md)

</div>

---

> **참고:** GitHub는 저장소 홈에 항상 영어 `README.md`를 표시합니다(브라우저 언어 자동 전환 없음). 상단 언어 링크를 사용하고, [문서](docs/README.ko.md)·[shellcode 가이드](docs/shellcode-compiler/README.ko.md)에서는 페이지 언어 링크와 breadcrumb으로 같은 언어를 유지하세요.

## 개요

NeverC는 표준 C를 호스트 바이너리, 프리스탠딩 실행 파일, 위치 독립 shellcode로 컴파일합니다——모두 단일 툴체인에서 처리합니다. **x86_64** 및 **AArch64**(리틀 엔디안만)를 대상으로 합니다.

## 기능

- **[Shellcode 컴파일러](docs/shellcode-compiler/README.ko.md)** — 다단계 IR/MIR 파이프라인, 크로스 플랫폼 추출, 임포트/시스템 콜 저하, 커널 모드, 배드 바이트 감사, 플러그인 아키텍처
- **통합 링커** — 단일 바이너리에서 COFF, ELF, Mach-O; 외부 `ld` / `link.exe` 불필요
- **크로스 컴파일** — macOS/Linux에서 번들 MSVC SDK로 Windows PE 빌드
- **[내장 `string` 타입](docs/builtin-string/README.ko.md)** — 값 의미론 문자열, 점 표기 메서드 구문, 자동 메모리 관리, 네이티브 UTF-8 지원
- **경량 LLVM 빌드** — x86_64 / AArch64 백엔드만; C++/ObjC/OpenMP 경로 제거

## 빠른 예제

```c
#include <unistd.h>

int main(void) {
    string msg = "Hello " + "NeverC!";
    write(1, msg.c_str(), msg.len);
    return 0;
}
```

```bash
# macOS arm64 shellcode
neverc -fshellcode -mshellcode-syscall hello.c -o hello.bin

# Linux x86_64로 크로스 컴파일 — 같은 소스
neverc -fshellcode -target x86_64-linux-gnu -mshellcode-syscall hello.c -o hello.bin
```

상세 설계, 플랫폼 매트릭스, CLI 참조, 예제는 **[문서 색인](docs/README.ko.md)** 을 참조하세요.

## 빌드

### 요구 사항

- CMake 3.20+
- Ninja
- C++17 호스트 컴파일러(GCC, Clang 또는 MSVC)

### 구성

```bash
cmake -B build-neverc -G Ninja \
  -C neverc/cmake/caches/NeverC.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  llvm
```

### 빌드

```bash
cmake --build build-neverc --target neverc
```

`ccache` / `sccache`는 자동 감지되어 있으면 활성화됩니다.

### 테스트

```bash
tests/neverc/run_tests.sh build-neverc
```

### 검증

```bash
./build-neverc/bin/neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
./build-neverc/bin/neverc -c /tmp/hello.c -o /tmp/hello.o
```

## Windows로 크로스 컴파일

[xwin](https://github.com/Jake-Shadle/xwin) SDK splat을 `build-neverc/sdk/msvc/`에 배치한 뒤:

```bash
./build-neverc/bin/neverc --target=x86_64-pc-windows-msvc \
  -o hello.exe hello.c -lkernel32
```

Windows shellcode(`-fshellcode`, PEB 임포트 해석 등)는 [shellcode 컴파일러 문서](docs/shellcode-compiler/README.ko.md)를 참조하세요.

## 라이선스

[AGPL-3.0](LICENSE)

LLVM 구성 요소는 [Apache-2.0 WITH LLVM-exception](llvm/LICENSE.TXT) 라이선스를 유지합니다.

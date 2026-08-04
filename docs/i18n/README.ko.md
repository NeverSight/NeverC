**언어**: [English](../../README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

<div align="center">

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="../assets/neverc-logo-dark.svg">
  <img src="../assets/neverc-logo-light.svg" width="72" alt="NeverC">
</picture>

# NeverC

**AI 친화적인 보안 연구용 C23 컴파일러 — LLVM 기반**

통합 링커 · DynCode 파이프라인 · 내장 런타임（`string` · `mimalloc` · `xorstr` · `strhash`）

[![AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](../../LICENSE)
[![C23](https://img.shields.io/badge/Standard-C23-brightgreen.svg)](#기능)
![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-informational.svg)
[![Arch](https://img.shields.io/badge/Arch-x86__64%20%7C%20AArch64-orange.svg)](#기능)

[문서 색인](../README.ko.md) · [DynCode 가이드](../dyncode-compiler/README.ko.md) · [내장 런타임](../builtins/README.ko.md) · [플러그인 API](../plugin-api/README.ko.md) · [로드맵](../roadmap/README.ko.md)

</div>

---

> **참고:** GitHub는 저장소 홈에 항상 영어 `README.md`를 표시합니다(브라우저 언어 자동 전환 없음). 상단 언어 링크를 사용하고, [문서](../README.ko.md)·[dyncode 가이드](../dyncode-compiler/README.ko.md)에서는 페이지 언어 링크와 breadcrumb으로 같은 언어를 유지하세요.

## 개요

NeverC는 표준 C를 호스트 바이너리, 프리스탠딩 실행 파일, 위치 독립 dyncode로 컴파일합니다——모두 단일 툴체인에서 처리합니다. **x86_64** 및 **AArch64**(리틀 엔디안만)를 대상으로 합니다. 향후 릴리스에서 **EVM**(이더리움 스마트 컨트랙트)과 **Solana eBPF**(온체인 프로그램) 컴파일 타겟이 추가될 예정입니다.

## 왜 NeverC인가?

C는 이미 가장 단순한 시스템 프로그래밍 언어입니다. NeverC는 그것을 더 단순하게 만듭니다:

- **순수 C23, 그것뿐** — 템플릿 없음, RAII 없음, 연산자 오버로딩 없음, 숨겨진 제어 흐름 없음. 읽은 그대로 실행됩니다.
- **내장 `string`** — 값 의미론 문자열. `+`, `==`, `.starts_with()`와 자동 해제 지원——C++ 불필요.
- **예외 없음** — 에러 처리는 항상 명시적. 스택 해제 없음, 예측 불가능한 성능 저하 없음.
- **단일 바이너리** — 컴파일러 + 링커 + 런타임이 하나의 실행 파일에. 외부 의존성 제로.
- **LLM 친화적** — 최소한의 문법과 결정론적 의미론 덕분에 AI가 생성한 NeverC 코드가 C++보다 올바르게 컴파일될 확률이 높습니다.
- **진정한 크로스 컴파일** — macOS나 Linux에서 Windows PE, Linux ELF, macOS Mach-O, Android ELF, dyncode를 빌드——VM 불필요, 듀얼 부팅 불필요, SDK 찾기 불필요. 각 플랫폼 SDK가 컴파일러에 내장되어 있습니다.
- **제로 프릭션 확장** — 단 하나의 C 헤더와 130개의 이름 붙은 컴파일 페이즈로 IR 최적화부터 최종 바이너리 출력까지 모든 단계에 개입하는 [컴파일러 플러그인](../plugin-api/README.ko.md)을 작성 가능——LLVM 지식 불필요.
- **보안 연구 내장** — DynCode 컴파일, 컴파일 타임 문자열 암호화, 크로스 플랫폼 PE 생성이 컴파일러에 네이티브 통합——외부 스크립트로 덧붙인 것이 아닙니다.

## 기능

- **[DynCode 컴파일러](../dyncode-compiler/README.ko.md)** — 다단계 IR/MIR 파이프라인, 크로스 플랫폼 추출, 임포트/시스템 콜 저하, 커널 모드, 배드 바이트 감사, 플러그인 아키텍처
- **통합 링커** — 단일 바이너리에서 COFF, ELF, Mach-O; 외부 `ld` / `link.exe` 불필요
- **크로스 컴파일** — 모든 호스트에서 Windows PE, Linux ELF, macOS Mach-O, Android ELF 빌드 (플랫폼 SDK 내장)
- **[내장 런타임](../builtins/README.ko.md)** — 컴파일러 임베디드 LLVM bitcode 런타임: [`string`](../builtins/string/README.ko.md) (값 의미론 문자열, 자동 메모리 관리), [`mimalloc`](../builtins/mimalloc/README.ko.md) (투명 고성능 할당자 오버라이드, 커널 및 freestanding 타깃 외에는 기본 활성화), [`xorstr`](../builtins/xorstr/README.ko.md) (컴파일 타임 문자열 암호화, 시그니처 우회 복호화) 및 [`strhash`](../builtins/strhash/README.ko.md) (컴파일 타임 문자열 해시, 런타임과 동일 알고리즘)
- **[플러그인 API](../plugin-api/README.ko.md)** — 아웃오브트리 플러그인용 순수 C ABI; 단일 헤더 SDK, LLVM/CRT 의존성 제로, 드라이버·전처리기·AST·IR·MIR·MC·오브젝트·링크·LTO·dyncode 페이즈 전반을 포괄
- **[`.nc` 확장자](../nc-extension/README.ko.md)** — `.nc` 파일 확장자로 모든 NeverC 기능(`string`, Rust 스타일 정수 타입)을 자동 활성화, 추가 플래그 불필요
- **경량 LLVM 빌드** — x86_64 / AArch64 백엔드만; C++/ObjC/OpenMP 경로 제거

## 빠른 예제

```c
#include <stdio.h>

typedef struct { string user; string pass; } creds;

int main(void) {
    string msg = "Hello " + "NeverC!";
    printf("%s\n", msg.c_str());

    // Compile-time encryption — `strings ./bin` cannot find these literals
    creds login = {.user = "admin".encrypt(), .pass = "s3cret".encrypt()};
    string paths[] = {"/api/v1".encrypt(), "/api/v2".encrypt()};

    // Zero-allocation decrypt-and-compare (plaintext never fully in memory)
    if (login.user == "admin".encrypt() && login.pass == "s3cret".encrypt()) {
        for (int i = 0; i < 2; i++)
            if (msg.starts_with(paths[i]))
                printf("route matched: %s\n", paths[i].c_str());
    }
    return 0;
}
```

> **참고:** 내장 **`string`** 타입은 `.c` 파일에서 **`-fbuiltin-string`** 이 필요합니다. [**`.nc` 파일**](../nc-extension/README.ko.md) 또는 **`-fdyncode`** 모드에서는 자동으로 활성화됩니다.

```bash
# macOS arm64 / x86_64
neverc -fdyncode -target arm64-apple-macos hello.c -o hello.bin
neverc -fdyncode -target x86_64-apple-macos hello.c -o hello.bin

# iOS arm64
neverc -fdyncode -target arm64-apple-ios hello.c -o hello.bin

# Linux x86_64 / arm64
neverc -fdyncode -target x86_64-linux-gnu hello.c -o hello.bin
neverc -fdyncode -target aarch64-linux-gnu hello.c -o hello.bin

# Android arm64 / x86_64
neverc -fdyncode -target aarch64-linux-android hello.c -o hello.bin
neverc -fdyncode -target x86_64-linux-android hello.c -o hello.bin

# Windows x86_64 / arm64
neverc -fdyncode -target x86_64-pc-windows-msvc hello.c -o hello.bin
neverc -fdyncode -target aarch64-pc-windows-msvc hello.c -o hello.bin
```

상세 설계, 플랫폼 매트릭스, CLI 참조, 예제는 **[문서 색인](../README.ko.md)** 을 참조하세요. 빌드 가능한 샘플은 **[examples](../examples/README.ko.md)** 참조.

## 설치

**Linux x64/arm64** 및 **macOS arm64**에서는 다음 한 줄로 최신 release를 설치할 수 있습니다:

```bash
curl -fsSL https://raw.githubusercontent.com/NeverSight/NeverC/HEAD/install.sh | sh
```

설치 스크립트는 플랫폼용 release 아카이브를 내려받아 `SHA256SUMS`로 검증한 뒤 `~/.neverc`에 설치하고, shell `PATH` 앞에 `~/.neverc/bin`을 추가합니다.

특정 버전을 고정하려면:

```bash
curl -fsSL https://raw.githubusercontent.com/NeverSight/NeverC/v3389.1.2/install.sh | NEVERC_VERSION=v3389.1.2 sh
```

설치 확인:

```bash
neverc --version
neverc hello.c -o hello -fbuiltin-string
```

**Windows x64/arm64** 패키지는 [GitHub Releases](https://github.com/NeverSight/NeverC/releases)에서 수동으로 받으세요. macOS arm64 바이너리는 Apple Developer ID로 서명되고 공증되었습니다.

선택적 설치 환경 변수:

| 변수 | 용도 |
|------|------|
| `NEVERC_INSTALL_DIR` | 설치 prefix（기본：`~/.neverc`） |
| `NEVERC_VERSION` | Release 태그（예：`v3389.1.2`, 기본：최신） |
| `NEVERC_NO_MODIFY_PATH=1` | shell 설정 파일 변경 건너뛰기 |

크로스 컴파일 sysroot(Windows SDK, Linux sysroot 등)는 컴파일러가 `PATH`에 올라간 뒤 필요할 때 설치합니다:

```bash
neverc runtime install all
neverc runtime install windows-x64
neverc runtime list
```

## 소스에서 빌드

빌드 요구 사항, 빌드 명령, Windows 크로스 컴파일, PATH 설정, release 설치와 트리 내 빌드 전환은 **[로컬 개발](../local-dev/README.ko.md)**을 참조하세요.

## 기여

NeverC는 **설계상 C만** 지원합니다(C23). C++ / Objective-C / CUDA 등 유사 언어 프론트엔드는
범위 밖이며, 이를 추가하는 Pull Request는 닫힙니다. C++ 지향 LLVM 툴체인이 필요하면
[llvm-msvc](https://github.com/backengineering/llvm-msvc)를 고려하세요.

언어·ABI·런타임에 대한 대규모 변경은 Pull Request 전에 먼저 issue를 열어 범위를
논의하세요.

기본 개발 브랜치는 **`dev`** 입니다. 작업 전에 clone 후 checkout 하고, Pull Request는 `dev`로 보내 주세요.

```bash
git clone https://github.com/NeverSight/NeverC.git
cd NeverC
git checkout dev
```

## 라이선스

[AGPL-3.0](../../LICENSE)

LLVM 구성 요소는 [Apache-2.0 WITH LLVM-exception](../../llvm/LICENSE.TXT) 라이선스를 유지합니다.

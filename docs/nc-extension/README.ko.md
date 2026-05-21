**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 문서](../README.ko.md)

# `.nc` 파일 확장자

## 개요

NeverC는 `.nc`를 네이티브 소스 파일 확장자로 인식합니다. 컴파일러가 `.nc` 입력 파일을 감지하면 모든 NeverC 언어 확장을 **자동으로 활성화**합니다 — 추가 플래그가 필요 없습니다.

## 자동 활성화되는 기능

| 플래그 | 효과 |
|--------|------|
| `-fneverc-types` | Rust 스타일 정수 별칭 (`u8`, `u16`, `u32`, `u64`, `i8`, `i16`, `i32`, `i64`, `usize`, `isize`) |
| `-fbuiltin-string` | 내장 `string` 값 타입, 자동 메모리 관리, 점 호출 구문, UTF-8 지원 |

## 사용법

소스 파일에 `.nc` 확장자를 사용하기만 하면 됩니다:

```bash
# 자동 — 추가 플래그 불필요
neverc hello.nc -o hello

# 다음과 동일:
neverc -fneverc-types -fbuiltin-string hello.c -o hello
```

```c
// hello.nc
#include <stdio.h>

int main(void) {
    string greeting = "안녕하세요, NeverC!";
    printf("%s (len=%zu)\n", greeting.c_str(), greeting.len);

    u32 x = 42;
    i64 y = -100;

    string msg = greeting + " x=%u, y=%lld".format(x, y);
    printf("%s\n", msg.c_str());
    return 0;
}
```

## 작동 원리

감지는 컴파일러 파이프라인의 두 레이어에서 수행됩니다:

### 1. Driver / Toolchain 레이어

Driver는 컴파일러 호출을 구성하기 전에 각 입력 파일의 확장자를 검사합니다. `.nc` 파일의 경우, `-fneverc-types`와 `-fbuiltin-string`이 무조건적으로 명령줄에 주입됩니다 — 사용자가 수동으로 전달할 필요가 없습니다.

`.c` 파일에서는 플래그가 선택 사항입니다. 필요한 `-fneverc-types`, `-fbuiltin-string`을 명시적으로 전달하세요.

### 2. CompilerInvocation 레이어

안전 장치로, 프론트엔드도 호출 파싱 시 입력 파일 확장자를 확인합니다. 입력 중 하나라도 `.nc` 확장자를 가지면, `LangOpts.NeverCTypes`와 `LangOpts.BuiltinString`이 `1`로 설정되어, Driver 레이어를 우회하는 경우(예: `-cc1` 직접 호출)에도 기능이 활성화됩니다.

## 호환성

- `.nc` 파일은 C 소스로 처리됩니다 — 언어는 여전히 C(기본 C23)이며, 새로운 언어가 아닙니다
- 모든 표준 C 플래그(`-std=c11`, `-O2`, `-g`, `-Wall` 등)가 동일하게 작동합니다
- `-fshellcode`는 `.nc`와 자연스럽게 결합됩니다: shellcode 모드는 자체적으로 `string`을 활성화하고, `.nc`는 `neverc-types`도 활성화합니다
- 크로스 컴파일(`-target aarch64-linux-gnu` 등)도 동일하게 작동합니다
- `.c` 파일은 영향을 받지 않습니다 — 플래그를 명시적으로 전달하지 않는 한 이전과 동일하게 동작합니다

## `.nc` vs `.c` 사용 시기

| 시나리오 | 권장 |
|----------|------|
| `string`과 Rust 스타일 타입을 사용하는 새 NeverC 프로젝트 | `.nc` 사용 |
| 다른 컴파일러와의 호환성을 유지하려는 기존 C 코드베이스 | `.c` + 명시적 플래그 사용 |
| Shellcode 프로젝트 | 둘 다 가능 — `-fshellcode`는 `string`을 항상 활성화 |
| 혼합 코드베이스 | NeverC 전용 파일은 `.nc`, 이식 가능한 코드는 `.c` |

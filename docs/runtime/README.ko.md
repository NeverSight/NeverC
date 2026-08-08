**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 문서 색인](../README.ko.md) · [← NeverC 프로젝트](../../README.md)

# `neverc runtime`

[GitHub Releases](https://github.com/NeverSight/NeverC/releases)에서 받는
**크로스 컴파일 runtime**(sysroot / SDK)을 관리합니다. 패키지는 컴파일러 옆
`<NeverC-root>/runtime/`에 둡니다(기본 설치는 `~/.neverc/runtime/`).

`neverc-runtime-<target>.zip`을 직접 풀기보다
`neverc runtime install …`을 사용하세요.

## 구문

```text
neverc runtime install <target> [--version <tag>]
neverc runtime install all [--version <tag>]
neverc runtime update <target> [--version <tag>]
neverc runtime remove <target>
neverc runtime list
neverc runtime --help
```

별칭: `upgrade` → `update`; `uninstall` → `remove`; `ls` → `list`.

## 사용 가능한 대상

| 대상 | 내용 배치(`runtime/` 아래) |
|------|----------------------------|
| `windows-x64` | `windows/x64`(+ 공유 `windows/shared`) |
| `windows-arm64` | `windows/arm64`(+ 공유 `windows/shared`) |
| `linux-x64` | `linux/x64` |
| `linux-arm64` | `linux/arm64` |
| `macos-arm64` | `macos/arm64` |
| `android-arm64` | `android/arm64` |
| `android-kernel-arm64` | `android/kernel` |

## 하위 명령

### `install`

기본적으로 **컴파일러 release 태그**로 대상 하나를 설치합니다(또는
`--version <tag>`). 자산 이름: `neverc-runtime-<target>.zip`.

```bash
neverc runtime install windows-x64
neverc runtime install linux-arm64 --version v3389.1.2
```

이미 설치된 경우:

- 같은 태그 → 알리고 성공 종료.
- 다른 / 알 수 없는 태그 → `[Y/n]`으로 재설치 확인.

### `install all`

컴파일러 버전(또는 `--version`)으로 카탈로그의 **아직 없는** 대상을 모두
설치합니다. 이미 설치된 대상은 건너뜁니다. 핀을 바꾸려면 단일 대상에 다시
`install` 하세요.

```bash
neverc runtime install all
```

### `update` / `upgrade`

대화 없이 대상 하나를 강제 가져옵니다. 기본 버전은 **latest**입니다
(`install`이 컴파일러 태그를 따르는 것과 다름). `--version`으로 고정할 수 있습니다.

```bash
neverc runtime update windows-x64
neverc runtime update android-arm64 --version v3389.1.2
```

### `remove` / `uninstall`

설치된 대상 디렉터리를 삭제하고 `runtime/manifest.json`을 갱신합니다.

```bash
neverc runtime remove linux-x64
```

### `list` / `ls`

카탈로그 각 대상의 설치 상태(기록된 태그 포함)와 현재 컴파일러 태그를 보여 줍니다.

```bash
neverc runtime list
```

## 버전 규칙

| 명령 | `--version` 생략 시 기본값 |
|------|---------------------------|
| `install` / `install all` | 컴파일러 release 태그 |
| `update` | 해당 runtime 자산을 게시한 최신 release |

태그는 `vMAJOR.MINOR.PATCH` 형태입니다. 압축 해제 전 release의 `SHA256SUMS`로
검증합니다.

## `neverc update`와의 관계

- `neverc runtime …`은 **sysroot만** 바꿉니다.
- [`neverc update`](../update/README.ko.md)는 **컴파일러와 이미 설치된 모든
  runtime**을 한 트랜잭션으로 같은 태그로 맞춥니다.

`neverc update`로 컴파일러를 올린 뒤에는 설치된 runtime이 이미 맞춰져 있습니다.
**새** 대상에만 `runtime install`하면 됩니다.

## 관련 명령

| 명령 | 사용 시기 |
|------|-----------|
| [`neverc update`](../update/README.ko.md) | 컴파일러+설치된 runtime 함께 업/다운그레이드 |
| [`neverc build` / `make`](../build/README.ko.md) | 이 sysroot에 의존하는 크로스 컴파일 예제 빌드 |
| [예제](../examples/README.ko.md) | `--target=…`를 쓰는 샘플 `Makefile` |
| `neverc runtime --help` | 내장 사용법 요약 |

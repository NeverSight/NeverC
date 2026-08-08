**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 문서 색인](../README.ko.md) · [← NeverC 프로젝트](../../README.md)

# `neverc runtime`

[GitHub Releases](https://github.com/NeverSight/NeverC/releases)에서 받는
**교차 컴파일 runtime**(sysroot / SDK)을 관리합니다. 위치는 컴파일러 옆
`<NeverC-root>/runtime/`(기본 `~/.neverc/runtime/`).

`neverc-runtime-<target>.zip`를 수동 해제하지 말고 `neverc runtime install …`를 쓰세요.

## 구문

```text
neverc runtime install <target> [--version <tag>]
neverc runtime install all [--version <tag>]
neverc runtime update <target> [--version <tag>]
neverc runtime remove <target>
neverc runtime list
neverc runtime --help
```

별칭: `upgrade` → `update`, `uninstall` → `remove`, `ls` → `list`.

## 사용 가능한 대상

| 대상 | `runtime/` 아래 배치 |
|--------|-------------------------|
| `windows-x64` | `windows/x64` (+ shared `windows/shared`) |
| `windows-arm64` | `windows/arm64` (+ shared `windows/shared`) |
| `linux-x64` | `linux/x64` |
| `linux-arm64` | `linux/arm64` |
| `macos-arm64` | `macos/arm64` |
| `android-arm64` | `android/arm64` |
| `android-kernel-arm64` | `android/kernel` |

## 예

```bash
neverc runtime install windows-x64
neverc runtime install all
neverc runtime update linux-arm64 --version v3389.1.2
neverc runtime remove macos-arm64
neverc runtime list
```

## 하위 명령

- **`install`**: 기본은 **컴파일러 릴리스 태그**(또는 `--version`). 같은 태그면 성공 종료, 다르면 `[Y/n]`로 재설치 확인.
- **`install all`**: 카탈로그의 **미설치** 대상만 설치. 이미 있으면 건너뜀.
- **`update` / `upgrade`**: 비대화형 강제 갱신. 기본은 **latest**.
- **`remove` / `uninstall`**: 디렉터리 삭제 및 `manifest.json` 갱신.
- **`list` / `ls`**: 설치 상태와 컴파일러 태그 표시.

## 버전 규칙

| 명령 | `--version` 생략 시 기본 |
|------|---------------------------|
| `install` / `install all` | 컴파일러 릴리스 태그 |
| `update` | 해당 runtime 자산이 있는 최신 release |

태그는 `vMAJOR.MINOR.PATCH`. 해제 전 `SHA256SUMS`로 검증합니다.

## `neverc update`와의 관계

- `neverc runtime …`는 **sysroot만** 변경.
- [`neverc update`](../update/README.ko.md)는 컴파일러와 설치된 runtime을 한 트랜잭션으로 맞춤.

컴파일러 갱신 후 설치된 runtime은 이미 정렬됨. **새** 대상만 `runtime install`하면 됩니다.

## 관련 명령

| 명령 | 용도 |
|------|------|
| [`neverc update`](../update/README.ko.md) | 컴파일러+설치된 runtime 함께 업/다운 |
| [`neverc build` / `make`](../build/README.ko.md) | 교차 컴파일 예제 빌드 |
| [예제](../examples/README.ko.md) | `--target=…` Makefile |
| `neverc runtime --help` | 내장 도움말 |

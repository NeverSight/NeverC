**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 예제](../../docs/examples/README.ko.md)

# macOS 애플리케이션 예제

NeverC로 크로스 컴파일한 네이티브 macOS Mach-O 실행 파일입니다. sysctl, uname, Mach 커널 API를 사용하여 시스템 및 프로세스 정보를 조회합니다. macOS, Windows, Linux 어디서든 빌드 가능 — Xcode 불필요.

## 빌드

저장소에서 (기본 타겟: `arm64-apple-macos`):

```bash
cd examples/macos-app
neverc make          # debug: -g(첫 빌드 기본값)
neverc make release  # release: -O2 --strip
neverc make debug    # debug로 전환
```

Makefile이 `PROFILE`을 유지하므로 이후 `neverc make`도 같은
debug/release 선택을 사용합니다. release는 NeverC 내장 `--strip`으로
불필요한 정적 심볼 이름과 디버그 메타데이터를 제거하고, 로더/동적 ABI에
필요한 이름은 유지합니다. 자세한 내용:
[릴리스 빌드](../../docs/release-builds/README.ko.md).


Intel용 빌드:

```bash
neverc make TARGET=x86_64-apple-macos
```

독립형 NeverC 릴리스 사용:

```bash
neverc make NEVERC=/path/to/neverc
```

## 수동 빌드 (Make 미사용)

```bash
neverc --target=arm64-apple-macos -Wall -o macos-app main.c
```

## 실행

```bash
./macos-app
```

## 기능

- `uname`으로 커널 정보 조회
- `sysctl`로 하드웨어 정보 조회 (모델, CPU 수, 메모리 크기, 페이지 크기)
- 프로세스 정보 표시 (`getpid`, `getppid`, `getuid`)
- Mach `host_info`로 호스트 정보, `task_info`로 태스크 메모리 통계 조회

**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 예제](../../docs/examples/README.ko.md)

# macOS 동적 라이브러리 예제

NeverC로 크로스 컴파일한 네이티브 macOS `.dylib` 동적 라이브러리입니다. Mach 커널 인터페이스를 래핑하여 태스크 정보 조회와 가상 메모리 작업을 제공합니다 — 보안 연구용. macOS, Windows, Linux 어디서든 빌드 가능 — Xcode 불필요.

## 빌드

저장소에서 (기본 타겟: `arm64-apple-macos`):

```bash
cd examples/macos-dylib
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
neverc --target=arm64-apple-macos -Wall -dynamiclib -o libneverc.dylib lib.c
```

## 기능

- `nc_task_basic_info`로 Mach `task_info` 쿼리 래퍼 내보내기
- `nc_vm_read`/`nc_vm_write`로 Mach 가상 메모리 읽기/쓰기
- `nc_vm_alloc`/`nc_vm_dealloc`로 Mach VM 메모리 할당 및 해제
- XOR 버퍼 암호화 헬퍼 및 PID/태스크 조회 함수

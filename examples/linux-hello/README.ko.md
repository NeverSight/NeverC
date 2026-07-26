**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 예제](../../docs/examples/README.ko.md)

# Linux Hello World 예제

NeverC를 사용하여 Linux ELF로 크로스 컴파일하는 최소한의 C 프로그램. macOS, Windows, Linux에서 빌드 가능 — 대상 시스템 툴체인 불필요.

NeverC는 `runtime/linux/`에 Linux sysroot(Ubuntu 22.04, glibc 2.35)를 번들하여 한 번의 호출로 전처리, 컴파일, 최적화(auto-LTO), 내장 링커를 통한 링크를 완료합니다.

## 빌드

리포지토리에서 (기본 타겟: `x86_64-linux-gnu`):

```bash
cd examples/linux-hello
neverc make
```

AArch64용 빌드:

```bash
neverc make TARGET=aarch64-linux-gnu
```

독립형 NeverC 릴리스 사용:

```bash
neverc make NEVERC=/path/to/neverc
```

## 수동 빌드 (Make 없이)

```bash
neverc --target=x86_64-linux-gnu -Wall -o hello main.c
```

## 실행

`hello`를 Linux 머신(또는 Docker 컨테이너)에 복사하여 실행:

```bash
chmod +x hello
./hello
```

## 기능

- 명령줄 인수와 함께 인사 메시지 출력
- 번들된 libc의 `printf`, `strncpy`, `strlen`, `atoi` 시연
- 기본 정수/문자 연산 검증을 위한 XOR 문자열 변환

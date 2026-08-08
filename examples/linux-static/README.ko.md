**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 예제](../../docs/examples/README.ko.md)

# Linux 완전 정적 링크 예제

NeverC로 빌드한 자체 포함형 정적 링크 Linux 실행 파일. 런타임 의존성 없음.

NeverC는 `runtime/linux/`에 Linux sysroot(Ubuntu 22.04, glibc 2.35)를 번들합니다.

## 빌드

```bash
cd examples/linux-static
neverc make          # debug: -g(첫 빌드 기본값)
neverc make release  # release: -O2 --strip
neverc make debug    # debug로 전환
```

Makefile이 `PROFILE`을 유지하므로 이후 `neverc make`도 같은
debug/release 선택을 사용합니다. release는 NeverC 내장 `--strip`으로
불필요한 정적 심볼 이름과 디버그 메타데이터를 제거하고, 로더/동적 ABI에
필요한 이름은 유지합니다. 자세한 내용:
[릴리스 빌드](../../docs/release-builds/README.ko.md).


AArch64:

```bash
neverc make TARGET=aarch64-linux-gnu
```

## 수동 빌드

```bash
neverc --target=x86_64-linux-gnu -Wall -static -lm -o static-demo main.c
```

## 실행

```bash
chmod +x static-demo
./static-demo
```

## 기능

- 시스템 정보 출력
- 수학 함수: `sqrt`, `sin`, `pow`, `log`
- 문자열 연산, 동적 메모리 관리

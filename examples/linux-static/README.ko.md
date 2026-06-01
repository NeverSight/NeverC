**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Linux 완전 정적 링크 예제

NeverC로 빌드한 자체 포함형 정적 링크 Linux 실행 파일. 런타임 의존성 없음.

NeverC는 `runtime/linux/`에 Linux sysroot(Ubuntu 22.04, glibc 2.35)를 번들합니다.

## 빌드

```bash
cd examples/linux-static
neverc make
```

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

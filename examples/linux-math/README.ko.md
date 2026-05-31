**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Linux 수학 + zlib 예제

수학 라이브러리 함수와 zlib 압축 데모. `-lm`과 `-lz` 사용.

NeverC는 `runtime/linux/`에 Linux sysroot(Ubuntu 22.04, glibc 2.35)를 번들합니다.

## 빌드

```bash
cd examples/linux-math
make
```

AArch64:

```bash
make TARGET=aarch64-linux-gnu
```

## 수동 빌드

```bash
neverc --target=x86_64-linux-gnu -Wall -lm -lz -o math-demo main.c
```

## 실행

```bash
chmod +x math-demo
./math-demo
```

## 기능

- 삼각 함수: sin/cos/tan
- 특수 함수: `exp`, `tgamma`, `erf`
- zlib 압축/해제/CRC32

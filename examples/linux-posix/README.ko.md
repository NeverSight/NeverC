**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Linux POSIX API 예제

NeverC로 크로스 컴파일된 POSIX 시스템 프로그래밍: pthreads, mmap, pipe, 시그널 처리.

NeverC는 `runtime/linux/`에 Linux sysroot(Ubuntu 22.04, glibc 2.35)를 번들합니다.

## 빌드

```bash
cd examples/linux-posix
make
```

AArch64:

```bash
make TARGET=aarch64-linux-gnu
```

## 수동 빌드

```bash
neverc --target=x86_64-linux-gnu -Wall -lpthread -o posix-demo main.c
```

## 실행

```bash
chmod +x posix-demo
./posix-demo
```

## 기능

- **pthreads**: 4개의 워커 스레드 생성
- **mmap**: 익명 메모리 페이지 할당
- **pipe**: Unix 파이프를 통한 메시지 전송
- **signals**: `SIGUSR1` 핸들러 검증

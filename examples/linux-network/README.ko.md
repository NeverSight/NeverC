**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 예제](../../docs/examples/README.ko.md)

# Linux 네트워크 소켓 예제

NeverC로 크로스 컴파일된 TCP 클라이언트/서버 데모.

NeverC는 `runtime/linux/`에 Linux sysroot(Ubuntu 22.04, glibc 2.35)를 번들합니다.

## 빌드

```bash
cd examples/linux-network
neverc make
```

AArch64:

```bash
neverc make TARGET=aarch64-linux-gnu
```

## 수동 빌드

```bash
neverc --target=x86_64-linux-gnu -Wall -o network-demo main.c
```

## 실행

```bash
chmod +x network-demo
./network-demo
```

## 기능

- TCP 서버(127.0.0.1)
- 클라이언트 연결
- 3개 메시지 송수신
- `socket`, `bind`, `listen`, `accept` 시연

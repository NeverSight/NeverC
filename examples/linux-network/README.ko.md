**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 예제](../../docs/examples/README.ko.md)

# Linux 네트워크 소켓 예제

NeverC로 크로스 컴파일된 TCP 클라이언트/서버 데모.

NeverC는 `runtime/linux/`에 Linux sysroot(Ubuntu 22.04, glibc 2.35)를 번들합니다.

## 빌드

```bash
cd examples/linux-network
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

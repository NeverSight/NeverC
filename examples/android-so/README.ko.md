**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android 공유 라이브러리 예제

NeverC를 사용하여 Android용으로 크로스 컴파일한 ARM64 네이티브 `.so` 공유 라이브러리입니다. macOS, Windows, Linux에서 빌드 가능.

## 빌드

```bash
cd examples/android-so
make
```

## 수동 빌드

```bash
neverc --target=aarch64-linux-android -Wall -shared -fPIC -ldl -o libneverc.so lib.c
```

## 기능

- 게임 보안 연구용 헬퍼 함수: PID 조회, `/proc/self/maps` 읽기, RWX 메모리 할당, XOR 버퍼 암호화
- `dlopen`으로 `liblog.so`를 동적 로드
- `mmap` + `PROT_EXEC`로 실행 가능한 메모리를 할당하는 데모


**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 문서 색인](../README.ko.md) · [← NeverC 프로젝트](../../docs/i18n/README.ko.md)

# NeverC 예제

NeverC의 크로스 플랫폼 컴파일 기능을 보여주는 빌드 가능한 예제. 모두 macOS / Linux에서 크로스 컴파일 가능 — Windows 빌드 환경 불필요.

---

## 예제 목록

| 예제 | 설명 | 주요 기능 |
|------|------|---------|
| [Windows 커널 드라이버](../../examples/windows-driver/README.ko.md) | 최소 WDM 커널 드라이버 | macOS/Linux에서 `.sys` 크로스 컴파일, 자동 LTO, 내장 링커 |
| [Windows 드라이버 + CET](../../examples/windows-driver-cet/README.ko.md) | Intel CET 섀도 스택 커널 드라이버 | CET 호환 커널 코드, `/guard:ehcont` |
| [Windows 드라이버 + 부동 소수점](../../examples/windows-driver-float/README.ko.md) | 부동 소수점/SIMD 커널 드라이버 | 커널 모드 안전 부동 소수점 |
| [Windows Ring3 EXE](../../examples/windows-exe/README.ko.md) | 사용자 모드 콘솔 앱 | GetSystemInfo, 프로세스 열거, VirtualAlloc |
| [Windows Ring3 DLL](../../examples/windows-dll/README.ko.md) | 사용자 모드 DLL | ReadProcessMemory, VirtualAllocEx, 모듈 열거 |

### Linux

| 예제 | 설명 | 주요 기능 |
|------|------|--------|
| [Linux Hello World](../../examples/linux-hello/README.ko.md) | 최소한의 C 프로그램 | macOS/Windows에서 크로스 컴파일 |
| [Linux POSIX](../../examples/linux-posix/README.ko.md) | POSIX 시스템 프로그래밍 | pthreads, mmap, pipe, 시그널 |
| [Linux 정적](../../examples/linux-static/README.ko.md) | 완전 정적 바이너리 | `-static` 링크 |
| [Linux 네트워크](../../examples/linux-network/README.ko.md) | TCP 소켓 데모 | 클라이언트/서버 |
| [Linux 수학 + zlib](../../examples/linux-math/README.ko.md) | 수학 + 압축 | 삼각 함수, zlib, CRC32 |

### Android

| 예제 | 설명 | 주요 기능 |
|------|------|---------|
| [Android ELF](../../examples/android-elf/README.ko.md) | 루팅 기기용 네이티브 ARM64 바이너리 | Android 크로스 컴파일, dlopen/liblog, /proc 정보, root 확인 |
| [Android 공유 라이브러리](../../examples/android-so/README.ko.md) | 네이티브 ARM64 `.so` 라이브러리 | 공유 라이브러리, mmap RWX, XOR 암호화 |

---

## 빠른 시작

```bash
cd examples/<예제명>
neverc make
```

컴파일러 경로 지정: `make NEVERC=/path/to/neverc`

모든 예제는 **neverc**를 컴파일러로 사용하며 내장 링커를 통해 Windows PE 바이너리(`.sys`)를 생성합니다.

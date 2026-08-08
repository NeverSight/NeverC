**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 예제](../../docs/examples/README.ko.md)

# Windows Ring3 EXE 예제

NeverC로 크로스 컴파일한 Windows 사용자 모드 실행 파일입니다. Win32 API를 사용.

## 빌드

```bash
cd examples/windows-exe
neverc make          # debug: -g(첫 빌드 기본값)
neverc make release  # release: -O2 --strip
neverc make debug    # debug로 전환
```

Makefile이 `PROFILE`을 유지하므로 이후 `neverc make`도 같은
debug/release 선택을 사용합니다. release는 NeverC 내장 `--strip`으로
불필요한 정적 심볼 이름과 디버그 메타데이터를 제거하고, 로더/동적 ABI에
필요한 이름은 유지합니다. 자세한 내용:
[릴리스 빌드](../../docs/release-builds/README.ko.md).

## 수동 빌드

```bash
neverc --target=x86_64-pc-windows-msvc -Wall -Xlinker --subsystem=console -lkernel32 -luser32 -lmsvcrt -o example.exe main.c
```

## 기능

- `GetSystemInfo`로 시스템 정보 조회
- `CreateToolhelp32Snapshot`으로 프로세스 열거
- `VirtualAlloc`/`VirtualQuery`/`VirtualFree` 데모


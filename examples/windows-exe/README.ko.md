**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Windows Ring3 EXE 예제

NeverC로 크로스 컴파일한 Windows 사용자 모드 실행 파일입니다. Win32 API를 사용.

## 빌드

```bash
cd examples/windows-exe
make
```

## 수동 빌드

```bash
neverc --target=x86_64-pc-windows-msvc -Wall -fms-extensions -fms-compatibility -D_AMD64_ -Xlinker --subsystem=console -lkernel32 -luser32 -lmsvcrt -o example.exe main.c
```

## 기능

- `GetSystemInfo`로 시스템 정보 조회
- `CreateToolhelp32Snapshot`으로 프로세스 열거
- `VirtualAlloc`/`VirtualQuery`/`VirtualFree` 데모


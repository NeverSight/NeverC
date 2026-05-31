**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Windows Ring3 DLL 예제

NeverC로 크로스 컴파일한 Windows 사용자 모드 DLL입니다.

## 빌드

```bash
cd examples/windows-dll
make
```

## 수동 빌드

```bash
neverc --target=x86_64-pc-windows-msvc -Wall -shared -Xlinker --entry=DllMain -Xlinker --subsystem=windows -lkernel32 -luser32 -o example.dll dllmain.c
```

## 기능

- 크로스 프로세스 메모리 접근용 래퍼 내보내기
- 프로세스/모듈 열거
- XOR 버퍼 암호화 헬퍼


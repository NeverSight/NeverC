**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Windows 커널 드라이버 예제

NeverC로 빌드한 최소한의 WDM 커널 드라이버입니다. macOS / Linux에서 크로스 컴파일을 지원합니다.

NeverC는 올인원 컴파일러입니다 — 단일 호출로 전처리, 컴파일, 최적화(auto-LTO),
내장 링커를 통한 링킹을 처리합니다.

## 빌드

저장소에서:

```bash
cd examples/windows-driver
make
```

독립 실행형 NeverC 릴리스에서:

```bash
make NEVERC=/path/to/neverc
```

출력은 `ExampleDriver.sys`(auto-LTO 최적화)입니다.
기본 빌드에는 디버깅용 `-g`가 포함되어 있습니다. **릴리스 빌드에서는 `-g`를 제거**하여
디버그 심볼을 제거하고 바이너리 크기를 줄이세요 (~38 KB → ~3 KB).

## 수동 빌드 (Make 없이)

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fms-kernel -fms-extensions -fms-compatibility \
  -D_AMD64_ -DNTDDI_VERSION=0x06010000 -D_WIN32_WINNT=0x0601 \
  -Wall -nostdlib -shared \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -lntoskrnl -lhal \
  -o ExampleDriver.sys driver.c
```

> `-g`는 DWARF 디버그 정보를 PE에 포함합니다. `llvm-dwarfdump`로 검사할 수 있습니다.
> 릴리스 빌드에서는 바이너리 크기를 줄이기 위해 생략하세요.

## 기능

- `\Device\ExampleDriver`에 디바이스 오브젝트 생성
- `\DosDevices\ExampleDriver`에 심볼릭 링크 생성
- `IRP_MJ_CREATE`, `IRP_MJ_CLOSE`, `IRP_MJ_DEVICE_CONTROL` 처리
- `DbgPrint`를 통해 로드/언로드 메시지 출력

## 로드 (Windows 테스트 머신에서)

```cmd
sc create ExampleDriver type= kernel binPath= C:\path\to\ExampleDriver.sys
sc start ExampleDriver
sc stop ExampleDriver
sc delete ExampleDriver
```

테스트 서명을 활성화하거나 프로덕션용 코드 서명 인증서를 사용하세요.

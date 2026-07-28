**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 예제](../../docs/examples/README.ko.md)

# Windows 커널 드라이버 예제

NeverC로 빌드한 최소한의 WDM 커널 드라이버입니다. 기본적으로 **x64**를 대상으로 하며,
ARM64용으로도 빌드할 수 있습니다. macOS / Linux에서 크로스 컴파일을 지원합니다.

NeverC는 올인원 컴파일러입니다 — 단일 호출로 전처리, 컴파일, 최적화(auto-LTO),
내장 링커를 통한 링킹을 처리합니다.

## 빌드

저장소에서:

```bash
cd examples/windows-driver
neverc make
```

이렇게 하면 `ExampleDriver-x64.sys`가 생성됩니다. ARM64용으로, 또는 둘 다 빌드하려면:

```bash
neverc make ARCH=arm64
neverc make all-arch
```

독립 실행형 NeverC 릴리스에서:

```bash
neverc make NEVERC=/path/to/neverc
```

출력은 `ExampleDriver-<아키텍처>.sys`(auto-LTO 최적화)입니다.
기본 빌드에는 디버깅용 `-g`가 포함되어 있습니다. **릴리스 빌드에서는 `-g`를 제거**하여
디버그 심볼을 제거하고 바이너리 크기를 줄이세요 (~38 KB → ~3 KB).

## 수동 빌드 (Make 없이)

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fms-kernel \
  -Wall -nostdlib -shared \
  -Xlinker --driver \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -lntoskrnl -lhal \
  -o ExampleDriver-x64.sys driver.c
```

ARM64의 경우 target을 `aarch64-pc-windows-msvc`로 바꾸기만 하면 나머지는 동일합니다.
`-fms-kernel`이 대상에 맞는 WDK 헤더와 임포트 라이브러리를 선택하고 WDK가 요구하는
아키텍처 매크로도 정의하므로 직접 전달할 필요가 없습니다.
`--driver`는 이미지를 커널 모드로 표시합니다. 코드와 데이터가 비페이징이 되고,
임포트 테이블은 삭제 가능한 INIT 섹션으로 이동하며, 커널 로더가 검증하는 PE
체크섬이 기록됩니다.

> `-g`는 DWARF 디버그 정보를 PE에 포함합니다. `llvm-dwarfdump`로 검사할 수 있습니다.
> 릴리스 빌드에서는 바이너리 크기를 줄이기 위해 생략하세요.

## 테스트 서명

Windows는 서명되지 않은 커널 드라이버의 로드를 거부합니다. `-ftest-sign`은
Authenticode 서명을 첨부하여 테스트 머신에서 이 검사를 통과하게 합니다:

```bash
neverc make TESTSIGN=1
neverc make ARCH=arm64 TESTSIGN=1
```

수동 호출 시 `-ftest-sign`을 추가해도 됩니다. 이 옵션은 `-fms-kernel`과 함께
쓸 때만 허용됩니다. 테스트 서명은 사용자 모드 바이너리에는 의미가 없기 때문입니다.

서명 ID는 컴파일러에 내장되어 있습니다 — 자체 서명 인증서이며 그 개인 키는
설계상 공개되어 있습니다. 진정성을 보장하지 않으며, 의도적으로 제한을 푼
머신에서 코드 무결성 검사를 통과시킬 뿐입니다. 대상 머신에서 관리자 권한으로
한 번만 설정하세요:

```cmd
bcdedit /set testsigning on
certutil -addstore Root neverc-test-signing.cer
certutil -addstore TrustedPublisher neverc-test-signing.cer
```

그런 다음 재부팅하세요. 인증서는 컴파일러에서 직접 내보낼 수 있으며, 이렇게 하면
실제 서명에 사용되는 신원과 항상 일치합니다:

```bash
neverc --print-test-sign-cert > neverc-test-signing.cer
```

(소스 트리의 `utils/neverc-test-signing.cer`에도 사본이 있지만 릴리스 패키지에는
포함되지 않습니다.)

Windows 머신이 없어도 `osslsigncode`로 서명을 확인할 수 있습니다. `-CAfile`은 PEM을
요구하지만 인증서는 DER이므로 먼저 변환하세요. DER을 그대로 넘기면
"signature verification failed"라는 혼란스러운 오류가 나오지만 실제 원인은
"no certificate found"입니다:

```bash
openssl x509 -inform DER -in neverc-test-signing.cer -out nc.pem
osslsigncode verify -CAfile nc.pem ExampleDriver-x64.sys
```

**테스트 머신을 벗어나는 어떤 것에도 사용하지 마세요.** 프로덕션에서는 실제
코드 서명 인증서를 사용하세요(Windows 10 1607 이상에서는 Microsoft 하드웨어
개발자 센터의 증명 서명도 필요합니다).

## 기능

- `\Device\ExampleDriver`에 디바이스 오브젝트 생성
- `\DosDevices\ExampleDriver`에 심볼릭 링크 생성
- `IRP_MJ_CREATE`, `IRP_MJ_CLOSE`, `IRP_MJ_DEVICE_CONTROL` 처리
- `DbgPrint`를 통해 로드/언로드 메시지 출력

## 로드 (Windows 테스트 머신에서)

```cmd
sc create ExampleDriver type= kernel binPath= C:\path\to\ExampleDriver-x64.sys
sc start ExampleDriver
sc stop ExampleDriver
sc delete ExampleDriver
```

테스트 서명을 활성화하거나 프로덕션용 코드 서명 인증서를 사용하세요.

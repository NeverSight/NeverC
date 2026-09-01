**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 문서 색인](../README.ko.md) · [← NeverC 프로젝트](../i18n/README.ko.md)

# Windows의 VBS 엔클레이브 DLL

NeverC는 64비트 Windows 타깃용 Microsoft 호환 VBS 엔클레이브 DLL을 링크할 수 있습니다. 지원되는 링커 계약은 다음과 같습니다.

```text
/DLL /INCREMENTAL:NO /INTEGRITYCHECK /ENCLAVE /GUARD:MIXED
```

Microsoft 링커 옵션은 Windows 드라이버의 `-Xmslink` 또는 `-Wl,`을 통해 전달합니다.

```powershell
neverc.exe --target=x86_64-pc-windows-msvc -fno-lto -shared -nostdlib `
  enclave.obj guarded.obj legacy.obj `
  -lvertdll -lbcrypt -llibcmt -llibvcruntime -lucrt `
  -Xmslink /INCREMENTAL:NO `
  -Xmslink /NODEFAULTLIB `
  -Xmslink /ENCLAVE `
  -Xmslink /INTEGRITYCHECK `
  -Xmslink /GUARD:MIXED `
  -Xmslink /DYNAMICBASE `
  -Xmslink /MACHINE:X64 `
  -o game-security-enclave.dll
```

이 예제는 `-l`을 사용해 MSVC CRT 및 UCRT 라이브러리의 엔클레이브 버전을 명시적으로 선택합니다. 명시적인 `-vctoolsdir` 또는 `-winsysroot` 지정은 평소와 같은 우선순위를 유지합니다. 이러한 재정의가 없으면 macOS, Linux, Windows 어느 호스트에서든 모든 `/ENCLAVE` 링크는 NeverC에 번들된 타깃 런타임에서만 Windows 라이브러리를 확인합니다. 호스트에 설치된 Visual Studio 도구 집합이나 Windows SDK를 자동으로 탐지하거나 그쪽으로 폴백하지 않습니다.

## 번들 런타임을 사용한 크로스 호스트 빌드

컴파일과 COFF 링크는 호스트에 독립적입니다. 타깃 런타임을 설치하면 동일한 명령을 macOS, Linux 또는 Windows에서 실행할 수 있습니다.

```text
neverc runtime install windows-x64
neverc runtime install windows-arm64
```

타깃 패키지에는 Windows 헤더, 엔클레이브 CRT, 엔클레이브 UCRT, `vertdll.lib`, `bcrypt.lib` 및 그 밖의 필요한 Windows 임포트 라이브러리가 포함됩니다. 번들 런타임에서 확인하는 경우 명시적인 `/ENCLAVE`와 전역 `/NODEFAULTLIB`를 함께 지정해야만 NeverC가 일반 번들 CRT/UCRT 디렉터리에서 엔클레이브 CRT/UCRT 디렉터리로 전환합니다. 이 모드에서는 링크 전에 드라이버가 번들된 `libcmt.lib`, `libvcruntime.lib`, `ucrt.lib`, `vertdll.lib`, `bcrypt.lib`가 모두 존재하는지 검증합니다. 라이브러리는 여전히 `-l...`로 명시적으로 선택해야 합니다. `/ENCLAVE`만으로는 엔클레이브 CRT/UCRT 디렉터리를 활성화하거나 그 라이브러리를 선택하지 않으며, 일반 번들 런타임 검색 경로를 계속 사용합니다.

크로스 호스트 링크 단계에서는 서명되지 않고 처리되지 않은 엔클레이브 DLL을 생성합니다. VEIID 처리, SignTool 서명 및 `CreateEnclave`/`LoadEnclaveImage`를 통한 실제 로드는 계속 Windows에서만 수행할 수 있으므로 macOS 또는 Linux에서 링크한 DLL을 마지막 세 단계용 Windows 패키징 또는 테스트 머신으로 옮기십시오. 런타임 설치와 검색에 관한 내용은 [타깃 런타임](../runtime/README.ko.md)을 참조하십시오.

## 필수 이미지 입력

엔클레이브 링크에는 다음 두 이미지 데이터 정의가 모두 필요합니다.

- 이미지의 `IMAGE_ENCLAVE_CONFIG` 데이터를 담는 `__enclave_config`.
- `EnclaveConfigurationPointer`를 포함할 만큼 충분히 큰 load-config 구조를 갖는 `_load_config_used`.

NeverC는 미사용 코드 제거 과정에서도 `__enclave_config`를 유지하고, 필요하면 아카이브에서 이를 추출하며, 최종 재배치된 load-config 포인터가 해당 구성 객체의 가상 주소와 같은지 검증합니다. 정의가 누락되었거나, 절대 심볼이거나, 폐기되었거나, 잘렸거나, 잘못 재배치되었다면 링크 오류가 발생합니다.

`/GUARD:MIXED`는 보호된 객체 파일과 레거시 객체 파일이 섞인 입력에 CFG 출력을 활성화합니다. 4바이트 RVA와 1바이트 메타데이터로 구성된 5바이트 GFID 및 GIAT 항목을 생성하며, 현재 일반 대상의 메타데이터는 0입니다. `GuardFlags`에는 CFG 및 항목 크기 비트가 포함됩니다. 레거시 객체는 unwind 메타데이터를 제외하면서 재배치를 보수적으로 검사해 주소 취득 대상을 제공합니다.

명시적인 증분 링크 요청은 `/ENCLAVE`와 호환되지 않으므로 거부됩니다. 객체 파일 지시문에서 비롯된 옵션을 포함하여 마지막으로 유효한 `/INCREMENTAL` 옵션이 사용됩니다.

`/ENCLAVE`는 DLL 출력, CFG, 무결성 검사, 엔클레이브 CRT 라이브러리, VEIID 처리 또는 서명을 암시적으로 선택하지 않습니다. 빌드 파이프라인에서 이러한 선택을 명시적으로 유지하십시오. 번들 런타임 모드에서는 위에서 설명한 엔클레이브 CRT/UCRT 검색 경로와 5개 라이브러리 검증이 전역 `/NODEFAULTLIB`를 명시한 경우에만 활성화됩니다. 이 옵션이 없으면 일반 번들 Windows 런타임 경로를 계속 사용합니다. 명시적인 사용자 도구 체인 재정의는 평소와 같은 우선순위를 유지합니다.

## 빌드 및 배포 흐름

1. 보안에 민감한 소스는 예를 들어 `-fms-guard=cf`를 사용하여 CFG를 활성화한 상태로 컴파일합니다. 최종 링크에서 `/GUARD:MIXED`를 사용하면 레거시 객체는 계측되지 않은 상태로 남아 있어도 됩니다.
2. 엔클레이브 구성과 진입점을 정의한 다음, 엔클레이브 CRT/UCRT 및 필요한 Vertdll과 BCrypt 임포트 라이브러리와 링크합니다.
3. 서명되지 않은 PE 이미지를 검사하고 load-config 디렉터리, CFG 테이블, 엔클레이브 구성 포인터 및 베이스 재배치를 검증합니다.
4. Windows에서 완성된 이미지에 Windows SDK VEIID 도구를 실행합니다.
5. Windows에서 SignTool을 사용해 VEIID로 처리한 이미지에 서명합니다. 서명은 파일에 대한 마지막 변경이어야 합니다.
6. Windows 호스트에서 `IsEnclaveTypeSupported(ENCLAVE_TYPE_VBS)`를 확인하고, `CreateEnclave`로 엔클레이브를 할당하고, `LoadEnclaveImage`로 DLL을 로드한 뒤 `InitializeEnclave`를 호출합니다.

안티치트 시스템에서 엔클레이브는 일반 게임 프로세스와 더 강한 경계가 필요한 코드 및 비공개 상태를 가진 작은 검증 또는 키 처리 구성 요소에 적합합니다. 엔클레이브 인터페이스를 좁게 유지하고 호스트가 제공하는 모든 데이터를 검증하십시오. 호스트는 여전히 입력, 스케줄링, 스토리지 및 가용성을 제어합니다. VBS 엔클레이브는 서버 측 권한, 텔레메트리, 드라이버 방어 및 일반적인 프로세스 강화를 보완하며 이를 대체하지 않습니다.

## 검증

`VBS enclave differential CI` 워크플로는 Windows에서 실행됩니다. 정적 게이트는 다음 작업을 수행합니다.

- NeverC 링커와 범위를 좁힌 COFF 테스트를 빌드합니다.
- Microsoft로 링크한 엔클레이브 DLL과 이에 상응하는 NeverC 링크 DLL을 만듭니다.
- 공개된 PE/load-config/CFG 의미 체계를 비교합니다.
- PE 검증기에 변이 테스트를 실행합니다.
- 차등 런타임 프로브용 VEIID 처리 이미지를 준비합니다.

런타임 프로브는 Microsoft 이미지를 먼저 실행합니다. 호스팅 runner에 VBS 또는 사용 가능한 서명 환경이 없으면 결과가 환경 건너뛰기로 명시됩니다. Microsoft 참조 이미지가 성공적으로 로드된 뒤에는 어느 NeverC 후보든 실패할 경우 엄격한 테스트 실패가 됩니다. 구성된 셀프 호스팅 VBS runner에서는 런타임 성공을 필수 게이트로 지정할 수 있습니다.

링커는 x86-64 및 ARM64 COFF 엔클레이브 이미지를 지원합니다. 게시된 구성 포인터를 검증한 다음 최종 일반 DLL 가져오기 집합에서 연속된 80바이트 `IMAGE_ENCLAVE_IMPORT` 항목을 생성합니다. 항목은 처음에는 가져오기 이름만 가지며, VEIID가 바인딩할 수 있도록 식별 필드는 0입니다. 링커가 개수, 목록 및 항목 크기를 다시 기록합니다. 활성 지연 로드 가져오기는 거부됩니다. `IMAGE_ENCLAVE_CONFIG` 내부의 버전 관리 필드에는 추가 정책을 적용하지 않습니다.

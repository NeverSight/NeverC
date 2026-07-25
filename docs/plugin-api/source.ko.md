**언어**: [English](source.md) | [简体中文](source.zh-CN.md) | [繁體中文](source.zh-TW.md) | [日本語](source.ja.md) | [한국어](source.ko.md) | [Français](source.fr.md) | [Deutsch](source.de.md) | [Español](source.es.md) | [Italiano](source.it.md) | [Русский](source.ru.md) | [العربية](source.ar.md)

# Source 및 I/O 플러그인 API

첫 공개 플러그인 ABI는 소스 입력, 가상 파일, 의존성, 컴파일러 출력을
`PluginSource.h`를 통해 노출합니다. 모든 경로는 정규화된 VFS 경로이며 모든
핸들은 현재 `TranslationUnit` 태스크로 범위가 한정됩니다.

## Source 단계

안정적인 source 파이프라인은 다음과 같습니다.

1. `neverc.source.resolve_input`이 요청된 입력을 검증하고 정규화합니다.
2. `neverc.source.open`이 호스트와 플러그인이 합성된 VFS를 통해 그것을 엽니다.
3. `neverc.source.after_open`이 검증된 `SourceUnit`에 대한 읽기 전용 이벤트를
   게시합니다.

`resolve_input`은 관찰 가능하고 가로챌 수 있습니다. `open`은 교체도 가능합니다.
호스트는 모든 교체 결과를 `SourceUnit`으로 게시하기 전에 검증합니다. 플러그인은
`after_open`을 교체할 수 없습니다.

## VFS 프로바이더

플러그인 등록 중에 `NevercIOAPI`를 질의하고 `RegisterVFSProvider`를 호출하십시오.
프로바이더는 먼저 `MatchesPath`에 응답한 다음 자신이 담당하는 연산을 구현합니다.
`NEVERC_VFS_RESULT_NOT_HANDLED`를 반환하면 다음 프로바이더에 위임되고,
`HANDLED`를 반환하면 잘못된 상태나 내용이 조용한 폴백 대신 치명적 오류가 됩니다.

프로바이더가 반환한 버퍼는 콜백 동안만 빌려온 것입니다. NeverC는 수락된 바이트를
태스크가 소유한 저장소로 복사합니다. 프로바이더는 그 결과가 결정적이고 캐시
가능한지 여부를 선언해야 합니다.

빌드 가능한
[`VirtualHeaderPlugin.c`](../../pluginsdk/examples/VirtualHeaderPlugin.c)
예제는 호스트 VFS를 우회하지 않고 메모리 상의 헤더를 제공합니다.

## 출력 싱크와 의존성

파일 출력과 메모리 출력은 동일한 트랜잭션 싱크를 사용합니다.

- 후보에 기록합니다.
- finish를 호출해 검증 대상 자격을 부여합니다.
- 봉인된 호스트 게이트가 검증하도록 합니다.
- 태스크 성공 시 원자적으로 커밋하고, 오류나 취소 시 중단합니다.

플러그인은 목적지 경로에 직접 기록하는 방식으로 게시하지 않습니다. 롤백할 수 없는
스트리밍 목적지는 원자적 후보가 필요한 변환을 거부합니다. 의존성 레코드는 정규화된
VFS 신원을 사용하므로 네이티브 파일과 플러그인이 제공한 파일은 동일한 출처와 캐시
의미론을 가집니다.

## 안전 규칙

- 콜백이 끝난 뒤 source, file, buffer, sink, task 핸들을 계속 보유하지 마십시오.
- `NevercStringView`와 `NevercByteView`는 길이가 지정된 뷰로 취급하십시오.
- 데이터가 콜백보다 오래 살아야 한다면 호스트 할당자를 사용하십시오.
- VFS 계약 뒤에서 호스트 파일시스템 API를 사용하지 마십시오.
- 비용이 큰 프로바이더 작업 전에 취소 여부를 확인하십시오.

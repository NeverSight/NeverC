**언어**: [English](prep.md) | [简体中文](prep.zh-CN.md) | [繁體中文](prep.zh-TW.md) | [日本語](prep.ja.md) | [한국어](prep.ko.md) | [Français](prep.fr.md) | [Deutsch](prep.de.md) | [Español](prep.es.md) | [Italiano](prep.it.md) | [Русский](prep.ru.md) | [العربية](prep.ar.md)

# 전처리기 플러그인 API

`PluginPrep.h`는 안정적인 토큰, 식별자, 매크로, pragma, 토큰 스트림 스키마를
NeverC나 LLVM의 C++ 타입을 노출하지 않고 제공합니다. 생성된 스키마
`Schema/PluginPrepSchema.inc`가 안정적인 수치 종류, 범주, 철자, 구성 가능성에
대한 유일한 기준입니다.

## 확장 수준

플러그인은 세 가지 수준에서 참여할 수 있습니다.

- include, 매크로 확장, 조건부 컴파일, pragma, 파일 전환에 대한 읽기 전용
  전처리기 이벤트;
- 토큰, include, 매크로, pragma, 기능 질의 단계에 대한 타입 지정 인터셉터;
- 검증된 `TokenStream`을 게시하는 완전한 `neverc.prep.build_token_stream`
  프로바이더.

토큰 단계는 한도가 정해진 교체, 삭제, 확장을 지원합니다. 호스트는 확장 예산을
강제하고, 교체를 게시하기 전에 철자, 위치, 플래그, EOF 배치, 토큰 소유권을
검증합니다.

## 토큰 빌더

`CreateTokenBuilder`로 합성 토큰을 만들고, 토큰 페이로드를 정확히 하나만 설정하고,
태스크가 소유한 유효한 위치를 지정한 다음 `TokenBuilderCommit`을 호출하십시오. 모든
경로에서 빌더를 파괴해야 합니다. 커밋된 빌더는 불변이며, 커밋이 실패하면 토큰은
게시되지 않습니다.

토큰 스트림은 연속적이고 불변인 태스크 산출물입니다. 교체용 스트림은 마지막에 EOF
토큰을 정확히 하나 포함해야 하며 `NEVERC_PREP_TOKEN_STREAM_MAX_TOKENS`를 넘을 수
없습니다.

## 옵저버와 인터셉터 규칙

옵저버는 읽기 전용 이벤트 데이터를 받으며 전처리에 영향을 줄 수 없습니다.
인터셉터는 공통 연속(continuation) 계약을 따릅니다.

- `InvokeNext`를 최대 한 번 호출한 뒤 `CONTINUE`를 반환하거나,
- 그것을 호출하지 않고 검증된 교체 결과를 게시합니다.

연속 객체와 모든 전처리기 핸들은 선언된 콜백/태스크 범위 안에서만 유효합니다.
플러그인이 만든 스레드가 그 값들을 건드린다면 콜백이 반환하기 전에 join해야 합니다.

## 검증

토큰 정의를 변경한 뒤에는 생성 스키마와 커버리지 검사를 실행하십시오.

```sh
python3 utils/plugin-api/gen-prep-schema.py --check
python3 utils/plugin-api/check-coverage.py docs/plugin-api/coverage.json
```

`NEVERC_ENABLE_PLUGIN_FUZZERS=ON`을 켜면
`plugin-prep-token-builder-fuzzer`가 잘못된 토큰 빌더, 태스크 핸들, 출력 용량,
토큰 스트림 질의를 시험합니다.

**언어**: [English](ast-sema.md) | [简体中文](ast-sema.zh-CN.md) | [繁體中文](ast-sema.zh-TW.md) | [日本語](ast-sema.ja.md) | [한국어](ast-sema.ko.md) | [Français](ast-sema.fr.md) | [Deutsch](ast-sema.de.md) | [Español](ast-sema.es.md) | [Italiano](ast-sema.it.md) | [Русский](ast-sema.ru.md) | [العربية](ast-sema.ar.md)

# AST, 파서, 시맨틱 플러그인 API

`PluginAST.h`와 `PluginSema.h`는 프런트엔드 트리와 시맨틱 파이프라인에 대한 태스크
범위의 순수 C 접근을 제공합니다. 안정적인 노드, 속성, 자식 슬롯 ID는 NeverC의 구체
AST 정의에서 생성됩니다. 플러그인은 C++의 `Decl`, `Stmt`, `Type`, `Sema` 포인터를
결코 받지 않습니다.

## AST 노드 읽기와 만들기

`NevercASTAPI`로 노드 정보, 스키마 속성, 자식, 부모, 선언 컨텍스트, 타입, 어트리뷰트,
그리고 흔히 쓰이는 구체 노드의 세부 정보를 질의하십시오. 일괄 API는 원소 개수, 용량,
스트라이드를 명시적으로 요구합니다.

`NevercASTBuilder`는 스키마에 선언된 노드 종류만 생성합니다. 필수 속성과 자식 슬롯은
커밋 시점에 검증됩니다. 커밋에 성공하면 태스크가 소유한 노드가 게시되고, 실패하면
부분적으로 보이는 노드가 남지 않습니다. 커밋 성공 여부와 관계없이 모든 빌더를
파괴하십시오.

## 원자적 변경

AST 변경은 `BeginASTMutation`, 스테이징된 연산, `CommitASTMutation`을 사용합니다.
호스트는 트리를 바꾸기 전에 소유권, 슬롯 호환성, 카디널리티, 부모 링크, 순환,
의미론적 불변식을 검증합니다. `AbortASTMutation`은 스테이징된 모든 연산을 버립니다.
네이티브 `TreeMutationListener` 알림은 커밋이 성공한 뒤에만 전송됩니다.

빌드 가능한 [`ASTRewritePlugin.c`](../../pluginsdk/examples/ASTRewritePlugin.c)는
내장 파서를 호출하고 정수 리터럴을 만든 뒤 변수 초기화식을 원자적으로 교체하는 파서
인터셉터를 보여 줍니다.

## 파서와 Sema 교체

`neverc.syntax.parse`는 검증된 토큰 스트림을 `ASTUnit`으로 매핑합니다.
`neverc.sema.analyze`는 AST 산출물을 `SemanticUnit`으로 매핑합니다. 두 단계 모두
타입 지정 인터셉터와 프로바이더를 갖습니다. 프런트엔드의 일부만 교체하려는 경우
선언, 문, 식, 타입 이름, 어트리뷰트, 조회, 변환, 키워드 등 세분화된 확장 단계를 계속
사용할 수 있습니다.

내장된 융합 파서/Sema 경로는 교체 구현과 동일한 산출물 계약을 게시합니다. 시맨틱
재생은 NeverC가 스코프, 이름 조회, 재선언, 타입 검사 상태를 재구성할 수 있는 노드
종류만 받아들입니다. 지원되지 않는 구체 종류를 만나면
`NEVERC_STATUS_UNSUPPORTED_AST_KIND`를 반환하며, 부분적으로만 재생된 트리를 의미적으로
완전하다고 표시하는 일은 결코 없습니다.

## 수명 주기와 정리

AST와 Sema의 수명 주기 옵저버는 호스트의 `TreeConsumer` 브리지를 통해 소스 순서대로
전달됩니다. 구문 오류, 플러그인 오류, 취소가 발생해도 begin/end 이벤트는 짝을
유지합니다. 태스크 핸들은 마지막 읽기 전용 end 이벤트와 정리 콜백이 실행된 뒤에야
무효가 됩니다.

## 검증

```sh
python3 utils/plugin-api/gen-ast-schema.py --check
ctest --test-dir build-neverc \
  -R 'Plugin(AST|Parser|Sema|Frontend)' --output-on-failure
```

`NEVERC_ENABLE_PLUGIN_FUZZERS=ON`을 켜면 `plugin-ast-mutation-fuzzer`가 속성 디코딩,
잘못된 빌더, 위조된 핸들, 변경 롤백을 다룹니다.

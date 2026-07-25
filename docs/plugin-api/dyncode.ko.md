**언어**: [English](dyncode.md) | [简体中文](dyncode.zh-CN.md) | [繁體中文](dyncode.zh-TW.md) | [日本語](dyncode.ja.md) | [한국어](dyncode.ko.md) | [Français](dyncode.fr.md) | [Deutsch](dyncode.de.md) | [Español](dyncode.es.md) | [Italiano](dyncode.it.md) | [Русский](dyncode.ru.md) | [العربية](dyncode.ar.md)

# DynCode 플러그인

`-fdyncode`는 하나의 번역 단위를, 코드에 재배치가 전혀 없고 데이터 섹션도 없는
평평한 위치 독립 이미지(`.bin`)로 컴파일합니다. 대상은 macOS, Linux, Android,
Windows에서의 arm64/x86_64이며 실행 수준은 사용자 모드 또는 커널 모드입니다.
플러그인은 C를 그 이미지로 바꾸는 타입이 지정된 페이즈들을, 다른 도메인과 동일한
순수 C ABI를 통해 관찰하거나 가로채거나 대체합니다. LLVM C++ 객체, STL 타입, 예외,
그리고 수명이 API 테이블에 명시되지 않은 호스트 포인터는 등장하지 않습니다.

## 인터페이스

```c
#include "neverc/Plugin/PluginDynCode.h"
```

| 인터페이스 | 테이블 | 슬롯 | 용도 |
|---|---|--:|---|
| `NEVERC_INTERFACE_DYNCODE_{HIGH,LOW}` | `NevercDynCodeAPI` | 16 | 요청, 이미지, 리포트 및 섹션/심볼/재배치/외부 참조 맵을 읽기 |
| `NEVERC_INTERFACE_DYNCODE_REGISTRAR_{HIGH,LOW}` | `NevercDynCodeRegistrarAPI` | 5 | `RegisterTarget`, `RegisterImportProvider`, `RegisterExtractor`, `RegisterCharsetEncoder`, `RegisterBinaryVerifier` |
| `NEVERC_INTERFACE_DYNCODE_PHASE_{HIGH,LOW}` | `NevercDynCodePhaseAPI` | 4 | `GetPhaseInfo`, `GetRequest`, `GetImage`, `GetReport` |

셋 모두 메이저 1에서 `NEVERC_INTERFACE_STABLE`입니다. 페이즈 콜백 안에서는
`NevercDynCodePhaseAPI`가 진입점이며, 프레임을 다른 테이블이 소비하는 핸들로
바꿔 줍니다:

```c
NevercDynCodeRequestHandle Request;
Phase->GetRequest(Phase->Context, Frame, Frame->Input, &Request);

NevercDynCodeRequestInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info), NEVERC_DYNCODE_API_MAJOR,
                                     NEVERC_DYNCODE_API_MINOR, 0};
DynCode->GetRequestInfo(DynCode->Context, Task, Request, &Info);
```

네 가지 맵 계열 — 섹션 맵, 심볼 맵, 재배치, 외부 참조 — 은 모두 동일한
first/next/info 삼종 세트로 순회합니다. 예를 들어 `GetFirstRelocation`,
`GetNextRelocation`, `GetRelocationInfo`입니다. 이를 통해 플러그인은 리포트 JSON을
파싱하지 않고도 추출 단계가 내린 결정을 읽을 수 있습니다.

## DynCode는 `main()` 이후 단계가 아니라 컴파일 산출물이다

`-fdyncode`는 드라이버 DAG 안의 평범한 Action/Job입니다. 컴파일 잡은 검증된
인메모리 `ObjectGraph`를 발행하고, `-dyncode-extract` 잡이 그 그래프를 소비해
사용자의 `-o` 이미지를 씁니다. `-###`, 페이즈 출력, 잡 그래프 모두 이 추출 잡을
보여 주므로, 플러그인이 모드를 알아내기 위해 재작성된 argv를 재구성할 필요가 전혀
없습니다. 동결된 요청은 태스크 로컬로 인프로세스 코드 생성과 공유됩니다.
`getCurrentDynCodeOptions()`도, 프로세스 전역 모드 플래그도, 임시 오브젝트 왕복도
없습니다.

정확히 하나의 번역 단위가 하나의 이미지로 낮춰집니다. 다중 입력, `-c/-S/-E`, 그리고
지원되지 않는 트리플은 안정적인 진단과 함께 미리 거부됩니다.

## 호환성 등급

페이즈 ID, 아티팩트 ID, 요청/리포트/이미지 컨테이너, 그리고 콜백 계약은 첫 릴리스의
STABLE ABI입니다. 타깃별 재배치 종류와 오브젝트 포맷의 섹션/심볼 스키마는
LOCKSTEP입니다. 이들을 사용하기 전에 타깃 스키마 ID와 다이제스트를 비교하십시오.
NeverC는 스키마가 일치하지 않으면 프로바이더를 호출하기 전에 거부합니다.

## 동결된 요청

잡 시작 시 드라이버는 명령줄을 불변의 `DynCodeRequest`로 정규화하고 동결합니다.
하위 태스크는 그 스냅숏을 빌려 쓸 뿐 절대 변경하지 않습니다. 요청은 타깃 키와
오브젝트 포맷, 실행 수준(사용자/커널), 엔트리 정책(명시적 심볼, 기본 후보 목록,
오프셋 0 요구 사항), PIC/섹션 정책, 외부 참조 정책, 금지 바이트 집합/프로파일과
재작성 플래그, charset 프로바이더 ID, 그리고 최대 길이·정렬·패딩 바이트를 담습니다.

## 타입이 지정된 페이즈 그래프

DynCode는 34개 페이즈로 이루어진 고정 그래프입니다. 30개의 일반 전이는
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE`이고, 4개는
`OBSERVABLE | SEALED_HOST_GATE`입니다. 봉인된 게이트는 IR 최종 검증, MIR 최종 검증,
이미지 검증, 그리고 커밋입니다. 플러그인은 어떤 페이즈든 관찰할 수 있고, 대체 가능한
전이를 인터셉터로 감싸거나 그 프로바이더를 통째로 대체할 수 있지만, 봉인된 게이트를
대체하거나 건너뛰거나 우회할 수는 결코 없습니다. 또한 비활성화된 변환을 "호출되지
않은 콜백"으로 표현할 수도 없습니다. 비활성화된 변환은 명시적인 no-op 프로바이더를
실행하며, 그 동등한 출력을 호스트 검증기가 여전히 증명합니다.

페이즈는 순서대로 다음과 같습니다:

1. 요청 동결;
2. IR 변환들 — prepare, 간접 분기 낮추기, 메모리 인트린식 낮추기(힙 이전과 이후),
   문자열 런타임 낮추기, 힙 아레나, 세 개의 `compiler_rt` 위치(pre/post/final),
   syscall/PEB/커널 임포트 낮추기, 두 개의 `data_to_text` 위치(pre/post), 인라인
   최적화, 문자열 확정, stackify, 전면 `blr`화, 그리고 봉인된 IR 최종 검증;
3. MIR prepare 변환과 봉인된 MIR 최종 검증;
4. 오브젝트 임포트 — 검증된 `ObjectGraph`를 태스크에 바인딩;
5. 추출 — 계획, 레이아웃, 재배치, 그리고 후보 이미지 구축;
6. 범위가 제한된 바이너리 페이즈들 — post-extract, 금지 바이트 재작성, charset
   인코딩, 크기/정렬/패딩, 그리고 pre-verify;
7. 봉인된 이미지 검증;
8. 봉인된 커밋.

ID, 정책, 안정성 등급, 게이트의 규범적 출처는
`neverc/include/neverc/Plugin/Schema/PhaseSchema.json`이며, 실행 가능한 커버리지
계약은 `docs/plugin-api/coverage.json`입니다.

## 내장 변환도 프로바이더다

모든 내장 IR/MIR 패스는 타입이 지정된 프로바이더로 감싸여 있으며, LLVM 패스 객체가
C ABI를 건너 노출되는 일은 없습니다. 페이즈를 대체하면 내장 프로바이더는 실행되지
않습니다. 통과하는 테스트는 단순히 등록이 성공했다는 사실이 아니라 동작이나 트레이스
자체를 증명합니다. `mem_intrin`, `compiler_rt`, `data_to_text` 페이즈는 여러 위치에
등장하지만, 각 위치는 고유한 증명을 가진 별개의 페이즈 ID입니다. 따라서 재실행은
멱등적이며 숨겨진 패스 상태에 의존하지 않습니다.

## 일반 오브젝트 입력은 ObjectGraph뿐이다

추출은 타깃의 코드 생성 경로가 만들어 낸 검증된 `ObjectGraph`를 정확히 하나만
소비합니다. `dyncode.object.import`는 그 그래프를 바인딩하고 타깃 키와 출처를
확인합니다. 디스크에서 바이트를 다시 읽지도, 두 번째 오브젝트 파싱을 돌리지도
않습니다. 사용자 정의 오브젝트 포맷은 `ObjectGraph`로 읽어 들일 수 있고 그에 맞는
재배치·타깃 프로바이더를 갖추는 즉시 DynCode에 참여할 수 있습니다. 다중 오브젝트와
LTO 그래프 집합은 동결 시점에 안정적인 `CAPABILITY_UNAVAILABLE`로 거부됩니다.

## 외부 참조와 임포트 낮추기

요청의 허용 외부 집합은 "프로바이더가 이것을 처리해도 된다"는 뜻일 뿐이며, 해결되지
않은 재배치가 평평한 이미지까지 살아남도록 허용하지 않습니다. 모든 외부 참조는
결국 다음 중 하나로 끝나야 합니다: IR/MIR에서 제거, 이미지 내 심볼로 해결, 선언되고
검증기가 수용한 런타임 리졸버 계약으로 변환, 또는 하드 에러. syscall 스텁, PEB
임포트, 커널 임포트가 세 개의 내장 `ImportProvider`이며, 각각 타깃/수준/심볼 매처와
자신이 생성하는 ABI 계약을 선언합니다. 플러그인은 `ImportProvider`를 추가할 수
있지만, 대체 출처, 엔트리 ABI 변경, 리졸버 매개변수, 잔여 참조를 반드시 반환해야
합니다.

## 이미지, 리포트, 범위가 제한된 바이트 편집

추출은 `DynCodeImage`와 `DynCodeReport`를 만듭니다. 이미지는 범위가 제한된 바이트
빌더에 더해 엔트리 오프셋/심볼, 소스 섹션 및 소스 심볼 출력 맵, 재배치 처리 결과,
그리고 외부/런타임 계약 레코드를 담습니다. 모든 바이트 편집은 빌더의 검사되는
read/write/insert/append/resize API를 거치며, `uint8_t **`는 존재하지 않습니다.
편집은 이미지 세대를 갱신하고, 변경된 범위와 겹치는 재배치/PIC/엔트리 증명을
무효화합니다.

리포트는 불변이고 결정적인 감사 산출물입니다: 요청/경로/입력/출력 다이제스트,
페이즈별 프로바이더 저널, 선택되거나 거부된 섹션과 그 이유, 엔트리 선택, 패치된
/거부된/런타임 계약이 된 재배치, 남은 외부 참조, 크기/정렬/패딩, 금지 바이트 스캔,
그리고 검증기 체크리스트. `-fdyncode-report=<path>`는 정규 JSON을 씁니다. 상세
진단도 별도의 계수를 다시 세지 않고 같은 리포트에서 렌더링됩니다.

금지 바이트 재작성 체인은 동결된 위상 순서로 실행되며 각 단계는 변경 레코드를
반환합니다. charset 인코더는 정확한 안정 ID로 선택되고 디코더 스텁, 인코딩된
페이로드, 엔트리 갱신, 타깃 증명을 반환합니다. 알 수 없거나 모호한 ID는 하드
에러입니다. 재작성을 비활성화하면 명시적인 no-op 단계가 선택되며, 최종 감사는 그대로
수행됩니다.

## 최종 검증기와 finalize 이후 타이밍

쓰기 가능한 페이즈는 모두 봉인된 최종 검증기 이전에 끝납니다. 검증기는 처리되지 않은
외부 재배치/참조가 남아 있지 않은지, 금지된 데이터/TLS/언와인드/디버그/메타데이터
섹션이 없는지, 엔트리가 존재하고 올바르게 정렬되어 있으며 (필요할 경우) 오프셋 0에
있는지, 모든 재배치 지점이 범위 안에 있고 현재 이미지 바이트에 부합하는 PIC 증명을
갖는지, 섹션/심볼 맵이 겹치지 않는지, 길이/정렬/패딩 규칙이 지켜지는지, 그리고
디코더·헤더·패딩을 포함한 최종 바이트에 금지 바이트가 없는지를 확인합니다. 하나라도
실패하면 구조화된 진단을 반환하고 출력 번들 전체를 폐기합니다.

감사 이후에는 쓰기 가능한 훅이 없습니다. 바이트 변환이 실행 가능 영역을 건드린다면,
동결된 경로는 그에 맞는 바이너리 검증기 능력을 제공해야 하며, 호스트는 이를 호출해
최종적이고 불변인 이미지에 대한 PIC 증명을 다시 발행합니다.

## 드라이버 옵션

`-fdyncode`가 모드를 활성화합니다. `-fdyncode-entry=`는 엔트리 심볼을 고릅니다.
`-fdyncode-bad-bytes=` / `-fdyncode-bad-byte-profile=`은 금지 바이트를 설정하고,
`-fdyncode-bad-byte-rewrite`(기본값 켜짐)는 재작성 체인을 선택하며,
`-fdyncode-charset=`은 등록된 인코더를 선택합니다. `-fdyncode-max-length=`,
`-fdyncode-align=`, `-fdyncode-pad=`은 최종 크기를 제한합니다.
`-fdyncode-keep-obj=`는 중간 재배치 가능 오브젝트를 함께 남기고,
`-fdyncode-report=`는 감사 리포트를 씁니다. `-mdyncode-context=user|kernel`은 실행
수준을 선택합니다.

## 동시성과 실패 규칙

- 가변 상태는 호스트가 제공하는 프로세스/세션/태스크 스코프에 두십시오. 현재
  플러그인이나 현재 옵션을 가리키는 싱글턴은 절대 쓰지 마십시오.
- 콜백이 반환된 뒤에 태스크 핸들이나 빌린 뷰를 캐시하지 마십시오.
- 인터셉터 컨티뉴에이션은 콜백 스레드에서 최대 한 번만 호출하십시오.
- 원래의 `NevercStatus`를 반환하십시오. `REPLACE`를 선언한 처리가 실패해도 내장
  프로바이더로 조용히 대체되지 않습니다.
- 동시성 모드와 재진입 모드는 사실에 부합하는 가장 좁은 값으로 선언하십시오.

읽기 전용 페이즈 트레이서는 `pluginsdk/examples/DynCodeTracePlugin.c`를, charset
인코더는 `pluginsdk/examples/DynCodeEncoderPlugin.c`를 참고하십시오.

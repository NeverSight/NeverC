**언어**: [English](mir.md) | [简体中文](mir.zh-CN.md) | [繁體中文](mir.zh-TW.md) | [日本語](mir.ja.md) | [한국어](mir.ko.md) | [Français](mir.fr.md) | [Deutsch](mir.de.md) | [Español](mir.es.md) | [Italiano](mir.it.md) | [Русский](mir.ru.md) | [العربية](mir.ar.md)

# NeverC 플러그인 MIR API

첫 공개 플러그인 ABI는 `PluginMIR.h`를 통해 Machine IR을 노출합니다. 이 API는
안정적인 C 식별자와 불투명 핸들을 사용하며, 플러그인은 LLVM의 클래스 레이아웃,
열거형 번호, C++ ABI에 의존하지 않습니다.

## 협상

`NevercMIRAPI`는 `NEVERC_INTERFACE_MIR`에서, `NevercMIRPassAPI`는
`NEVERC_INTERFACE_MIR_PASS`에서 질의하십시오. 함수 포인터를 사용하기 전에 반환된
테이블 크기를 확인하고, 더 새로운 호스트가 덧붙인 필드는 무시하십시오.

스키마 다이제스트는 현재 사용 중인 안정 ID와 호스트 간의 정확한 매핑을 식별합니다.
`GetEntityInfo`, `GetOperandKindInfo`, `GetGenericOpcodeInfo`,
`GetMachinePropertyInfo`는 정규 이름과 해당 연산에 타깃 스키마가 필요한지 여부를
알려 줍니다.

## 안정 모델

불투명 핸들이 나타내는 것은 다음과 같습니다.

- 머신 함수와 기본 블록;
- 머신 명령어와 오퍼랜드;
- 변경 트랜잭션;
- 분석 결과;
- 상수 풀 항목, 프레임 객체, 점프 테이블, 메모리 오퍼랜드, 타깃 참조.

핸들은 하나의 코드 생성 태스크에 속합니다. 삭제된 엔티티, 롤백된 엔티티, 변경으로
무효화된 분석 결과는 만료됩니다.

일반 스키마는 타깃 독립적 명령코드, 오퍼랜드 종류, 머신 프로퍼티, 저수준 타입,
명령어 플래그, 레지스터 할당, 프레임 객체, 상수, 점프 테이블, 메모리 포인터 형식,
원자적 순서를 다룹니다. 타깃 고유 명령코드에는 명시적으로 협상된 타깃 스키마가
필요합니다.

## MIR 읽기

`NevercMIRAPI`가 지원하는 것은 다음과 같습니다.

- 머신 함수 프로퍼티와 블록 순회;
- 선행자, 후속자, live-in, 명령어, 오퍼랜드 열거;
- 명령어 명령코드와 플래그 질의;
- 공개된 모든 머신 오퍼랜드 형식;
- 가상 레지스터와 물리 레지스터 정보;
- 프레임, 상수 풀, 점프 테이블, 메모리 오퍼랜드 상태.

개수/질의 쌍과 한도가 있는 출력 버퍼를 사용하십시오. 반환된 뷰는 별도 언급이 없는
한 현재 콜백 동안만 빌려온 것입니다.

## 트랜잭션 변경

MIR 변경은 변경 리스(mutation lease) 아래에서 이루어집니다.

1. 머신 함수에 대해 `BeginMutation`.
2. 블록과 명령어를 생성, 이동, 삭제.
3. 오퍼랜드와 CFG 간선을 추가하거나 갱신.
4. 필요한 증명과 함께 머신 프로퍼티 변경을 적용.
5. `CommitMutation` 또는 `AbortMutation`.

커밋은 구조 사전 점검과 Machine IR 검증을 수행합니다. 잘못된 오퍼랜드, CFG, 일반
명령코드 사용, 프로퍼티 주장은 원자적으로 롤백됩니다. 중단은 블록 순서, 명령어,
오퍼랜드, CFG 간선, 머신 프로퍼티를 복원합니다.

프로퍼티 변경에는 `NevercMIRPropertyProof`를 사용합니다. 증명은 전제가 더 이상
유효하지 않은 프로퍼티를 무효화하거나, 프로퍼티를 확립하기 전에 구조 검사를 요청해야
합니다.

## 패스와 단계

`NevercMIRPassDescriptor.Level`은 MachineModule, MachineFunction,
MachineBasicBlock 어댑터를 지원합니다. 안정적인 훅은 다음과 같습니다.

- 명령어 선택 이후;
- 적법화(legalization) 이후;
- 스케줄러 전/후;
- 레지스터 할당 전/후;
- 프롤로그/에필로그 이후;
- pre-emit;
- 마지막 플러그인 슬롯.

함수 패스는 병렬 코드 생성 파티션에서 실행될 수 있습니다. 모듈 수준 패스는 직렬화된
파이프라인 배리어에서 실행됩니다. 플러그인이 선언한 동시성과 재진입성 계약은 그대로
적용됩니다.

모든 코드 생성 파이프라인은 마지막 플러그인 슬롯 다음에 호스트가 소유한
`MachineVerifier`로 끝납니다. 이는 봉인된 게이트이며 플러그인이 비활성화할 수
없습니다.

## 분석

분석 테이블은 활성 변수, 활성 구간, 슬롯 인덱스, 지배자 트리, 루프 정보, 레지스터
압력을 제공합니다. 가용 여부는 선택한 훅에 따라 다릅니다. 일부 LLVM 분석은 해당
네이티브 파이프라인 단계 전후에는 존재하지 않기 때문입니다.

패스 디스크립터에 필요한 분석과 보존할 분석을 선언하십시오. 커밋된 변경은 영향받는
결과 핸들을 무효화합니다. 변경 후에 preserve-all을 주장하면 거부됩니다.

## 최소 예제

`pluginsdk/examples/MachinePass.c`는 안정적인 pre-emit 훅에 읽기 전용 머신 함수
패스를 등록합니다.

```sh
cmake --build build-neverc --target neverc-plugin-example-machine-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/MachinePass.so \
  -O2 -fno-lto -c input.c -o input.o
```

CMake가 생성하는 플랫폼별 모듈 확장자를 사용하십시오.

## 안전 요구 사항

- 콜백 이후에 태스크 핸들, MIR 핸들, 빌려온 뷰를 보유하지 마십시오.
- 핸들 값이나 LLVM 명령코드 번호를 임의로 만들어 내지 마십시오.
- 리스 밖에서 변경하지 마십시오.
- 테이블 헤더와 예약 저장 공간을 초기화하십시오.
- C 경계를 넘어 상태를 반환하고, C++ 예외가 그것을 넘게 하지 마십시오.

규범적 선언과 커버리지 증거는 `PluginMIR.h`, `MIRSchema.json`,
`PluginPhaseSchema.h`, `coverage.json`을 참조하십시오.

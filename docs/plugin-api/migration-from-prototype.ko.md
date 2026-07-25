**언어**: [English](migration-from-prototype.md) | [简体中文](migration-from-prototype.zh-CN.md) | [繁體中文](migration-from-prototype.zh-TW.md) | [日本語](migration-from-prototype.ja.md) | [한국어](migration-from-prototype.ko.md) | [Français](migration-from-prototype.fr.md) | [Deutsch](migration-from-prototype.de.md) | [Español](migration-from-prototype.es.md) | [Italiano](migration-from-prototype.it.md) | [Русский](migration-from-prototype.ru.md) | [العربية](migration-from-prototype.ar.md)

# 프로토타입 플러그인 API에서 마이그레이션하기

공개되지 않았던 프로토타입 플러그인 API — `nevercGetPluginInfo` 진입점, 단일
`NevercHostAPI` vtable, `Register*Pass` 호출, `NEVERC_INTERPOSE_*` 훅, 그리고
`-fplugin-pass=` 로더 — 는 첫 공개 릴리스 이전에 모두 제거되었습니다. 첫 번째 공개
ABI는 [README.md](README.md)에 문서화된 페이즈 기반 디스크립터 ABI입니다. 플러그인은
`neverc_plugin_entry`를 익스포트하고 독립적으로 버전이 매겨진 기능 테이블을
협상합니다.

호환 심(shim)도 없고 `v1`/`v2` 분기도 없습니다. 플러그인 *소스*를 공개 헤더에 대해
다시 컴파일하십시오. 이 문서는 모든 프로토타입 구성 요소를 첫 버전의 대체물, 의미
변화, 또는 명시적인 미승계 항목으로 매핑합니다.

## 프로토타입 바이너리는 거부됩니다

프로토타입 공유 객체를 로드하면 안정적인 진단과 함께 실패합니다:

```
plugin exports the removed 'nevercGetPluginInfo' prototype ABI; migrate it to
the first public descriptor ABI and export 'neverc_plugin_entry'
```

두 진입점 중 어느 것도 익스포트하지 않는 라이브러리는
`plugin has no 'neverc_plugin_entry' entry`로 실패합니다. 소스를 이식하기 전까지는
아무것도 로드되지 않습니다.

## 진입점

| 프로토타입 | 첫 번째 공개 ABI |
|---|---|
| `NevercPluginInfo nevercGetPluginInfo(void)` | `NevercStatus NEVERC_CALL neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap, NevercPluginDescriptor *OutPlugin)` |

진입점은 더 이상 구조체를 값으로 *반환하지* 않습니다. 호출자가 제공한
`NevercPluginDescriptor`를 `OutPlugin->Header.StructSize`를 존중하며 채우고
`NevercStatus`를 반환합니다. 지원을 표명하기 전에 필요한 기능 테이블을
`Bootstrap`에서 조회하십시오.

## `NevercPluginInfo` 필드

| 프로토타입 필드 | 첫 버전 매핑 |
|---|---|
| `APIVersion` | `Descriptor.Header` (`StructSize`, `NEVERC_PLUGIN_ABI_MAJOR`, `NEVERC_PLUGIN_ABI_MINOR`를 가진 `NevercABITableHeader`) |
| `PluginName` | `Descriptor.DisplayName` (`NevercStringView`), 그리고 스코프별 상태의 키로 사용되는 안정적인 역 DNS 형식의 `Descriptor.PluginID` |
| `PluginVersion` | `Descriptor.Version` (`NevercSemanticVersion`) |
| `RegisterPasses(API, Reg)` | `Descriptor.Register(Core, Registrar, RegistrarContext, ProcessState)` 및 라이프사이클 콜백 `ProcessBegin`, `SessionBegin`/`SessionEnd`, `TaskBegin`/`TaskEnd` |
| `Destroy()` | `Descriptor.Destroy(Core, ProcessState)` |
| *(프로토타입에 대응 없음)* | `Descriptor.Concurrency`와 `Descriptor.Reentrancy`를 사실대로 선언해야 합니다 (예: `NEVERC_CONCURRENCY_SESSION_SERIAL`, `NEVERC_REENTRANCY_ALLOWED`) |

## 호스트 접근: 단일 vtable → 기능 테이블

프로토타입은 200개가 넘는 엔트리를 가진 단일 `NevercHostAPI` vtable을 모든 콜백에
전달하고 새 필드를 `NEVERC_API_FN`으로 보호했습니다. 첫 버전은 이를 독립적으로 버전이
매겨지고 필요할 때 조회하는 기능 테이블로 대체합니다:

```c
NevercInterfaceID Driver = { NEVERC_INTERFACE_DRIVER_HIGH,
                             NEVERC_INTERFACE_DRIVER_LOW };
const void *Table = NULL;
uint16_t Minor = 0;
uint64_t TableSize = 0;
NevercStatus S = Bootstrap->QueryInterface(
    Bootstrap->Context, Driver, NEVERC_DRIVER_API_MAJOR,
    NEVERC_DRIVER_API_MINOR, &Table, &Minor, &TableSize);
```

일치하는 메이저 버전을 요구하고, 필드를 읽기 전에 `offsetof`로 `TableSize`를
확인하십시오. 인터페이스는 도메인별로 범위가 지정됩니다: Core, Driver, Source, Prep,
AST, Sema, IR, MIR, Target, MC, Object, Link, LTO, DynCode.

## 등록: `Register*Pass` + 훅 → 옵저버/인터셉터/프로바이더

프로토타입 등록은 콜백을 훅에 붙였습니다:

```c
API->RegisterModulePass(Reg, NEVERC_INTERPOSE_PRE_OPT, myPass, ud, "my-pass");
```

첫 버전은 `Register` 안에서, 128비트 `NevercInterfaceID`로 식별되는 페이즈에 대해
타입이 지정된 핸들러를 등록합니다:

| 프로토타입 호출 | 첫 버전 레지스트라 호출 |
|---|---|
| 읽기 전용 패스 | `NEVERC_OBSERVER_BEFORE`/`NEVERC_OBSERVER_AFTER` 지점을 지정한 `Registrar->RegisterObserver(NevercObserverDescriptor)` |
| 페이즈를 감싸거나 단락시키는 패스 | `Registrar->RegisterInterceptor(NevercInterceptorDescriptor)`; `Continuation->InvokeNext`를 최대 한 번만 호출하고 `OutResult->Action`을 설정합니다 |
| 내장 변환을 대체하는 패스 | `REPLACEABLE` 페이즈에서의 `Registrar->RegisterProvider(...)` |
| `-fplugin-pass-arg=` 읽기 | `Registrar->RegisterOption(NevercOptionDescriptor)`로 실제 드라이버 옵션을 선언 |

프로토타입의 "`PRE_OPT`의 모듈 패스"는 IR 페이즈 `neverc.ir.pass.pre_opt`의 옵저버,
인터셉터 또는 프로바이더가 됩니다.

## 훅 → 페이즈 매핑

| 프로토타입 훅 | 첫 버전 페이즈 (이름) |
|---|---|
| `NEVERC_INTERPOSE_PRE_OPT` | `neverc.ir.pass.pre_opt` |
| `NEVERC_INTERPOSE_POST_OPT` | `neverc.ir.pass.post_opt` |
| `NEVERC_INTERPOSE_PIPELINE_START` | `neverc.ir.pass.pipeline_start` |
| `NEVERC_INTERPOSE_PIPELINE_LAST` | `neverc.ir.pass.optimizer_last` |
| `NEVERC_INTERPOSE_BEFORE_CODEGEN_PREEMIT` | `neverc.mir.pass.preemit` |
| `NEVERC_INTERPOSE_AFTER_CODEGEN_FINAL_MIR` | `neverc.mir.pass.final` |
| `NEVERC_INTERPOSE_LTO_PRE_OPT` / `LTO_POST_OPT` | LTO 페이즈 `neverc.link.lto_resolve` / `neverc.link.lto_generate` ([mir.md](mir.md) 참고) |
| `NEVERC_INTERPOSE_LINK_PRE_LAYOUT` / `LINK_POST_LAYOUT` | `BEFORE` / `AFTER`에서 관찰하는 `neverc.link.layout` |
| `NEVERC_INTERPOSE_LINK_POST_EMIT` | `neverc.link.post_emit` |
| `NEVERC_INTERPOSE_SC_*` (dyncode) | [dyncode.md](dyncode.md)의 타입이 지정된 dyncode 페이즈들 |

페이즈 ID, 정책, 안정성 등급, 검증 게이트의 규범적 목록은
`neverc/include/neverc/Plugin/Schema/PhaseSchema.json`입니다. 실행 가능한 커버리지
계약은 [coverage.json](coverage.json)입니다. 예전에 단일 지점이었던 훅이 각자 고유한
정책과 증명을 가진 둘 이상의 페이즈 ID에 매핑될 수 있습니다.

## 패스 콜백, 핸들, 바이트 편집

| 프로토타입 | 첫 버전 |
|---|---|
| `NevercModulePassFn(NevercModuleRef, API, ud)` 등 | 콜백은 `NevercPhaseFrame`을 받습니다. IR/MIR/AST/그래프 객체는 해당 기능 테이블에서 얻는 타입이 지정된, 스코프가 있는 불투명 핸들입니다 ([ir.md](ir.md), [mir.md](mir.md), [ast-sema.md](ast-sema.md), [target-mc-object.md](target-mc-object.md) 참고) |
| 범용 `NevercValueRef` | 타입이 지정된 IR 핸들로 대체되어 제거되었습니다 |
| 살아 있는 `Ref`의 제자리 변경 | 모든 변경은 트랜잭션 호스트 API를 거칩니다 |
| `NevercBinaryPassFn(uint8_t **Data, uint64_t *Len, ...)` | 제거되었습니다. dyncode 바이트 편집은 검사되는 이미지 빌더(read/write/insert/append/resize)를 사용합니다. [dyncode.md](dyncode.md) 참고 |

핸들과 빌린 뷰는 이전과 정확히 동일하게 콜백 스코프 내에서만 유효합니다. 콜백이
반환된 뒤에는 캐시하지 마십시오.

## 제거된 편의 계층

프로토타입은 범용 헬퍼를 vtable에 함께 담았습니다. 이들은 첫 번째 공개 ABI의 일부가
**아닙니다**:

| 프로토타입 | 첫 버전 |
|---|---|
| `ArenaCreate` / `StrMapCreate` / `IntMapCreate` / `StrBuilderCreate` / `ValueSetCreate` | 승계되지 않습니다. `Core->Allocate`/`Core->Deallocate`와 직접 만든 컨테이너, 또는 타입이 지정된 도메인 API를 사용하십시오 |
| `NEVERC_FOR_EACH_*` / `NEVERC_COLLECT_*` 매크로 | 각 도메인 기능 테이블의 타입이 지정된 순회로 대체되었습니다 |
| `API->PluginGetArg` / `-fplugin-pass-arg=` | `RegisterOption`으로 옵션을 선언하고 Driver API를 통해 읽으십시오 |
| `DiagNoteF` / `DiagWarningF` / `DiagErrorF` | `Core->EmitDiagnostic(NevercDiagnosticDescriptor)` |

## 로딩과 명령줄

| 프로토타입 | 첫 버전 |
|---|---|
| `-fplugin-pass=<path>` | `-fplugin=<path>` |
| `-fplugin-pass-arg=key=value` | `RegisterOption`에서 선언한 옵션 철자 (예: `--driver-trace` 또는 `--my-opt=value`) |
| 두 개의 로더 (`-fplugin` vs `-fplugin-pass`) | 하나의 로더. 모듈은 단일 로더에 전달됩니다 |

## 버전 관리

프로토타입은 단조롭게 커지는 단일 vtable과 `NEVERC_API_FN` 가드에 의존했습니다. 첫
버전에서는 각 기능 테이블이 개별적으로 버전 관리됩니다. 일치하는 메이저를 요구하고,
추가된 필드를 읽기 전에 `StructSize`/`TableSize`를 확인하십시오. 첫 ABI 메이저 내에서
새 함수는 테이블의 안정 프리픽스 뒤에 추가되므로, 더 낮은 마이너에 대해 빌드된
플러그인은 더 새로운 호스트에서도 계속 동작합니다.

## 실제 예제

`pluginsdk/examples/DriverTracePlugin.c`는 첫 버전의 전체 형태를 보여줍니다:
`neverc_plugin_entry` 디스크립터, `ProcessBegin`/`Session`/`Task` 라이프사이클, 실제
CLI 플래그를 위한 `RegisterOption`, `neverc.driver.raw_arguments`에 대한
`RegisterObserver`, 그리고 `InvokeNext`를 정확히 한 번 호출하는
`neverc.driver.execute_job`에 대한 `RegisterInterceptor`. 
`pluginsdk/examples/ExamplePlugin.c`는 IR, MIR, object, link 페이즈를 다룹니다.

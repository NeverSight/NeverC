**언어**: [English](link-lto.md) | [简体中文](link-lto.zh-CN.md) | [繁體中文](link-lto.zh-TW.md) | [日本語](link-lto.ja.md) | [한국어](link-lto.ko.md) | [Français](link-lto.fr.md) | [Deutsch](link-lto.de.md) | [Español](link-lto.es.md) | [Italiano](link-lto.it.md) | [Русский](link-lto.ru.md) | [العربية](link-lto.ar.md)

# NeverC 플러그인 Link 및 LTO API

링크는 **하나의 그래프 위에서 도는 상태 기계**로 모델링됩니다. `PluginLink.h`
는 그 그래프 — 입력, 섹션, 아톰, 심벌, 엣지, COMDAT, 임포트, 익스포트, 언와인드
레코드, 합성물, 레이아웃 제약 — 과 함께, 파일 목록에서 커밋된 바이너리 이미지
까지 진행시키는 스무 개의 단계를 공개합니다. `PluginLTO.h` 는 그 중간에서
비트코드가 오브젝트가 되는 두 단계를 다룹니다.

플러그인은 모든 단계를 관찰하고, 대부분을 가로채고, 한 단계를 교체하고, 링크
전체를 교체하거나 오브젝트를 병합할 수 있습니다. lld 자료구조를 보는 일은 결코
없습니다. 이 그래프는 ELF, COFF, Mach-O 백엔드가 모두 대응되는 정규화된
투영입니다.

## 인터페이스

```c
#include "neverc/Plugin/PluginLink.h"
#include "neverc/Plugin/PluginLTO.h"   /* PluginLink.h 를 포함 */
```

| 인터페이스 | 테이블 | 용도 |
|---|---|---|
| `NEVERC_INTERFACE_LINK_{HIGH,LOW}` | `NevercLinkAPI` | 링크 그래프 읽기와 변경 (52 슬롯) |
| `NEVERC_INTERFACE_LINK_REGISTRAR_{HIGH,LOW}` | `NevercLinkRegistrarAPI` | 링커·오브젝트 병합·이미지 검증 프로바이더 등록 |
| `NEVERC_INTERFACE_LINK_PHASE_{HIGH,LOW}` | `NevercLinkPhaseAPI` | `NevercArtifactHandle` 뒤의 그래프나 이미지에 접근 |
| `NEVERC_INTERFACE_LTO_{HIGH,LOW}` | `NevercLTOAPI` | LTO 요청·모듈·심벌 해석 읽기 |
| `NEVERC_INTERFACE_LTO_REGISTRAR_{HIGH,LOW}` | `NevercLTORegistrarAPI` | LTO 코드 생성 프로바이더 등록 |

다섯 개 모두 major 1 에서 `NEVERC_INTERFACE_STABLE` 이므로 더 새로운 호스트는
덧붙이는 것만 가능합니다. 각각을 대응하는 `NEVERC_LINK_API_MAJOR` /
`NEVERC_LTO_API_MAJOR` 와 짝지어 쓰고, 호출할 마지막 슬롯 기준으로 `TableSize`
를 확인하세요.

## 상태 기계

`NevercLinkGraphInfo.State` 는 열네 개 값 중 하나이며, 스무 단계 가운데 열세
개는 오직 그것을 한 칸 전진시키기 위해 존재합니다:

| 단계 | 결과 `NEVERC_LINK_STATE_…` | 호스트 검증기 |
|---|---|---|
| — | `INITIAL` | — |
| `neverc.link.input_probe` | `INPUT_PROBED` | `verify_input_probe` |
| `neverc.link.read_inputs` | `INPUTS_READ` | `verify_inputs` |
| `neverc.link.lto_resolve` | `LTO_RESOLUTION_READY` | |
| `neverc.link.lto_generate` | `LTO_GENERATED` | |
| `neverc.link.resolve_symbols` | `SYMBOLS_RESOLVED` | |
| `neverc.link.select_comdat` | `COMDAT_SELECTED` | |
| `neverc.link.gc` | `GC_COMPLETE` | `verify_liveness` |
| `neverc.link.icf` | `ICF_COMPLETE` | |
| `neverc.link.synthesize` | `SYNTHETICS_READY` | |
| `neverc.link.relax_thunks` | `THUNKS_RELAXED` | `verify_relaxation` |
| `neverc.link.layout` | `LAYOUT_COMPLETE` | `verify_layout` |
| `neverc.link.relocate` | `RELOCATIONS_APPLIED` | |
| `neverc.link.emit_image` | `IMAGE_EMITTED` | |

이 열세 단계는 모두
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE | SKIPPABLE_WITH_PROOF` 이므로,
프로바이더가 전이 자체를 제공할 수 있고 유효한 `NevercLinkProofHandle` 을 가진
플러그인은 그것을 건너뛸 수 있습니다.

나머지 일곱은 구조적입니다:

| 단계 | 정책 | 역할 |
|---|---|---|
| `neverc.link.full` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE | 링크 전체 교체, `INITIAL` 에서 바로 바이너리 이미지로 |
| `neverc.link.object_merge` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE | ObjectGraph 의 `-r` 재배치 가능 병합 |
| `neverc.link.post_emit` | OBSERVABLE, INTERCEPTABLE | 이미지 바이트를 만질 마지막 기회 |
| `neverc.link.image_verify` | OBSERVABLE, **SEALED** | 호스트 이미지 검증기 |
| `neverc.link.side_outputs_verify` | OBSERVABLE, **SEALED** | 맵 파일, dSYM, 부수 산출물 |
| `neverc.link.commit` | OBSERVABLE, **SEALED** | 출력 번들의 원자적 공개 |
| `neverc.link.after_commit` | OBSERVABLE | 커밋 후 통지 |

세 개의 봉인 게이트는 관찰할 수는 있지만 결코 가로채거나 교체하거나 건너뛸 수
없습니다. `NEVERC_BUILTIN_LINK_PHASE_COUNT` 는 20 입니다.

## 단계에서 그래프에 접근하기

`NevercLinkPhaseAPI` 는 프레임의 아티팩트를 쓸 수 있는 핸들로 바꿉니다:

```c
NevercLinkPhaseGraphInfo GraphInfo = {0};
GraphInfo.Header = (NevercABITableHeader){sizeof(GraphInfo),
                                          NEVERC_LINK_PHASE_API_MAJOR,
                                          NEVERC_LINK_PHASE_API_MINOR, 0};
LinkPhase->GetGraph(LinkPhase->Context, Frame, Frame->Input, &GraphInfo);
/* GraphInfo.Link, .Graph, .Proof, .State, .Generation */
```

`GraphInfo.Link` 는 이 태스크에 묶인 `NevercLinkAPI` 이므로 옵서버는 따로
`QueryInterface` 를 할 필요가 없습니다. 프로바이더는 `PublishGraph` 로 결과를
공개하고, `GetImage` 는 이미지 아티팩트에 대해 같은 일을 하여 이미지, 출력 번들,
`NevercBinaryImageState`(`CANDIDATE`, `VERIFIED`, `COMMITTED`, `ABORTED`,
`FAILED_PARTIAL`) 를 담은 `NevercLinkPhaseImageInfo` 를 돌려줍니다.

## 그래프 읽기

`NevercLinkGraphInfo` 는 요약입니다 — 타깃, 형식, 상태, 세대, 열일곱 개의 엔터티
개수, 그리고 32 바이트 `SemanticDigest`. 엔터티 자체는 종류마다 하나씩 있는
페이징 호출로 돌아오며, 모두 호출자가 소유한 페이지를 공유합니다:

```c
typedef struct NevercLinkEntityPage {
  NevercABITableHeader Header;
  void *Data;                /* 여러분이 제공하고 소유하는 배열 */
  uint64_t ElementCapacity;  /* 몇 개가 들어가는지              */
  uint64_t ElementStride;    /* 원소의 sizeof                   */
  uint64_t OutCount;         /* 호스트가 실제로 쓴 개수         */
  uint64_t NextCursor;       /* 이어가려면 다시 넘기세요        */
  NevercBool HasMore;
  uint32_t Reserved;
} NevercLinkEntityPage;
```

호스트는 `ElementCapacity` 개를 넘기지 않고 각 `ElementStride` 바이트만 쓰며
`Data` 를 보관하지 않으므로, 스택 배열로 충분합니다:

```c
NevercLinkSymbolInfo Symbols[64];
NevercLinkEntityPage Page = {0};
uint64_t Cursor = 0;

do {
  Page.Header = (NevercABITableHeader){sizeof(Page), NEVERC_LINK_API_MAJOR,
                                       NEVERC_LINK_API_MINOR, 0};
  Page.Data            = Symbols;
  Page.ElementCapacity = 64;
  Page.ElementStride   = sizeof(Symbols[0]);
  Status = Link->GetSymbolPage(Link->Context, Task, Graph, Cursor, &Page);
  if (Status.Code != NEVERC_STATUS_OK)
    break;
  for (uint64_t I = 0; I != Page.OutCount; ++I) {
    /* Symbols[I].Name, .Binding, .Definition, .IsPrevailing, … */
  }
  Cursor = Page.NextCursor;
} while (Page.HasMore);
```

열다섯 개의 그래프 페이저가 이 모양을 따릅니다 — `GetInputPage`,
`GetArchivePage`, `GetArchiveMemberPage`, `GetSharedLibraryPage`,
`GetBitcodeModulePage`, `GetSectionPage`, `GetAtomPage`, `GetSymbolPage`,
`GetEdgePage`, `GetComdatPage`, `GetImportPage`, `GetExportPage`,
`GetUnwindPage`, `GetSyntheticPage`, `GetConstraintPage`. 여기에
`GetBinarySegmentPage` 와 `GetBinarySectionPage` 두 개가 더해져 방출된 이미지를
페이징합니다. 각각에는 단일 핸들용 `Get…Info` 가 짝지어 있습니다.

모든 엔터티 정보는 `NevercLinkOrigin` 을 지닙니다:

```c
typedef struct NevercLinkOrigin {
  NevercABITableHeader Header;
  NevercLinkInputHandle Input;
  NevercLinkArchiveMemberHandle ArchiveMember;
  NevercObjectGraphHandle ObjectGraph;
  uint64_t ObjectEntityID;
  NevercInterfaceID CreatedByPhase;
  NevercStringView CreatedByProvider;
  NevercInterfaceID LastMutationPhase;
  NevercStringView LastMutationPlugin;
} NevercLinkOrigin;
```

링크를 감사 가능하게 만드는 것이 바로 이것입니다. 출력의 어떤 아톰에 대해서든
입력 파일, 그것이 끌려 나온 아카이브 멤버, 그것을 만든 단계, 마지막으로 손댄
플러그인을 짚어낼 수 있습니다.

### 엔터티들

| 종류 | Info 구조체 | 주요 필드 |
|---|---|---|
| Input | `NevercLinkInputInfo` | `Kind`(OBJECT, ARCHIVE, SHARED_LIBRARY, BITCODE, SCRIPT, BLOB), `Ordinal`, `ContentDigest`, `ReaderRoute` |
| Archive / member | `NevercLinkArchiveInfo`, `NevercLinkArchiveMemberInfo` | `Thin`, `Materialized`, `MaterializationReason` |
| Shared library | `NevercLinkSharedLibraryInfo` | `InstallName` |
| Bitcode module | `NevercLinkBitcodeModuleInfo` | `Summary` |
| Section | `NevercLinkSectionInfo` | `Kind`, `Flags`, `Alignment`, `Address`, `Size`, `Comdat` |
| Atom | `NevercLinkAtomInfo` | `Flags`, `Content`, `ZeroFillSize`, `FoldLeader` |
| Symbol | `NevercLinkSymbolInfo` | `Binding`, `Visibility`, `Definition`, `IsPrevailing`, `IsRoot` |
| Edge | `NevercLinkEdgeInfo` | `Kind`, `Offset`, `RelocationKind`, `Addend`, `TargetSymbol`, `TargetAtom` |
| COMDAT | `NevercLinkComdatInfo` | `Selection`, `Selected` |
| Import / export | `NevercLinkImportInfo`, `NevercLinkExportInfo` | `Library`, `Symbol` |
| Unwind | `NevercLinkUnwindInfo` | `PersonalitySymbol` |
| Synthetic | `NevercLinkSyntheticInfo` | `Role`, `Section`, `Atom` |
| Constraint | `NevercLinkConstraintInfo` | `Kind`, `SubjectID`, `Value`, `Required` |

아톰 플래그는 `LIVE`, `ROOT`, `SYNTHETIC`, `FOLDED`, `ADDRESS_SIGNIFICANT`,
`TLS`, `UNWIND` 입니다. 심벌 바인딩은 `LOCAL`, `GLOBAL`, `WEAK`, `COMMON`;
정의는 `UNDEFINED`, `DEFINED`, `ABSOLUTE`, `COMMON`, `SHARED`. 엣지 종류는
`RELOCATION`, `ASSOCIATION`, `KEEP_ALIVE`, `UNWIND`, `FORMAT_EXTENSION`.
COMDAT 선택은 `ANY`, `EXACT_MATCH`, `SAME_SIZE`, `LARGEST`, `NEWEST`,
`NO_DUPLICATES` 를 아우릅니다.

## 그래프 변경

변경은 트랜잭션 방식이며 언제나 하나의 그래프로 범위가 한정됩니다:

```c
NevercLinkMutationHandle Mutation;
Link->BeginMutation(Link->Context, Task, Graph, &Mutation);

Link->SetSymbolRoot(Link->Context, Task, Mutation, Symbol, NEVERC_TRUE);
Link->ReplaceAtomContent(Link->Context, Task, Mutation, Atom,
                         (NevercByteView){Bytes, Length},
                         /*ZeroFillSize=*/0);

Status = Link->CommitMutation(Link->Context, Task, Mutation);
if (Status.Code != NEVERC_STATUS_OK)
  Link->AbandonMutation(Link->Context, Task, Mutation);
```

커밋은 작업 사본에 스테이징하고 검증한 다음에야 공개하며 `Generation` 을
올립니다. `AbandonMutation` 은 전부 버립니다. 예컨대 그래프가 `GC_COMPLETE` 에
있을 때 커밋하면 생존성 검증기가 다시 돌므로, 살아 있는 아톰을 고립시킬 변경은
기록되지 않고 거부됩니다.

### 변경은 하류 상태를 무효화한다

여기가 사람들이 놀라는 대목입니다. 모든 스테이징 호출은 분류되며, 그 분류가
**무효가 되는 가장 이른 상태**를 정합니다. 호스트는 거기서부터 모든 단계를 다시
실행해야 합니다:

| 호출 | 무효화되는 가장 이른 상태 |
|---|---|
| `RebindSymbol`, `RetargetEdge` | `SYMBOLS_RESOLVED` |
| `SetSymbolResolution` | `COMDAT_SELECTED` |
| `SetSymbolRoot` | `GC_COMPLETE` |
| `SetAtomLive` | `ICF_COMPLETE` |
| `SetFoldLeader`, `ReplaceAtomContent` | `SYNTHETICS_READY` |
| `CreateSynthetic`, `ReplaceSynthetic`, `EraseSynthetic` | `SYNTHETICS_READY` |
| `CreateConstraint`, `ReplaceConstraint`, `EraseConstraint` | `LAYOUT_COMPLETE` |

여러 개를 건드리는 변경은 그중 가장 이른 것을 택합니다. 따라서 레이아웃 후에
심벌을 재바인딩하면 레이아웃·재배치·이미지 결과가 버려집니다. `gc` 중에는 싸고
`post_emit` 중에는 비쌉니다. 변경 내용이 허락하는 한 상태 기계의 이른 지점에서
바꾸세요.

`SetSymbolResolution` 은 심벌 전체가 아니라 작은 갱신 레코드를 받습니다. 그래야
해석 변경이 실수로 이름이나 값을 다시 쓰지 않습니다:

```c
NevercLinkSymbolResolutionUpdate Update = {0};
Update.Header = (NevercABITableHeader){sizeof(Update), NEVERC_LINK_API_MAJOR,
                                       NEVERC_LINK_API_MINOR, 0};
Update.Binding      = NEVERC_LINK_SYMBOL_BINDING_GLOBAL;
Update.Visibility   = NEVERC_LINK_SYMBOL_VISIBILITY_HIDDEN;
Update.Definition   = NEVERC_LINK_SYMBOL_DEFINED;
Update.IsPrevailing = NEVERC_TRUE;
Update.IsExported   = NEVERC_FALSE;
Link->SetSymbolResolution(Link->Context, Task, Mutation, Symbol, &Update);
```

## 증명으로 단계 건너뛰기

`SKIPPABLE_WITH_PROOF` 단계는 실행 대신 `NevercLinkProofHandle` 을 받습니다.
증명은 건너뛰기가 의존하는 모든 것을 고정합니다:

```c
typedef struct NevercLinkProofInfo {
  NevercABITableHeader Header;
  NevercLinkProofHandle Proof;
  NevercLinkGraphHandle Graph;
  NevercLinkState State;
  uint32_t Reserved;
  uint64_t GraphGeneration;
  NevercTargetID TargetID;
  NevercObjectFormatID FormatID;
  NevercInterfaceID OutputArtifact;
  uint8_t RouteDigest[32];
  uint8_t SemanticDigest[32];
  uint64_t ImageBase;
  uint64_t EntryAddress;
} NevercLinkProofInfo;
```

`GraphGeneration` 과 `SemanticDigest` 가 모두 기록되므로, 증명을 발급한 뒤 사용
하기 전에 커밋된 변경이 하나라도 있으면 증명은 낡은 것이 되고 호스트는 해당
단계를 실제로 실행합니다.

## 바이너리 이미지

`emit_image` 이후 산출물은 `NevercBinaryImageHandle` 입니다:

```c
NevercBinaryImageInfo Image = {0};
Image.Header = /* … */;
Link->GetBinaryImageInfo(Link->Context, Task, ImageHandle, &Image);
/* .State, .OutputKind, .EntryAddress, .ImageBase, .Size,
   .SegmentCount, .SectionCount, .ImportCount, .ExportCount,
   .DynamicRelocationCount, .ContentDigest                     */
```

출력 종류는 `RELOCATABLE`, `EXECUTABLE`, `SHARED_LIBRARY`, `BUNDLE`. 세그먼트
플래그는 `READ`, `WRITE`, `EXECUTE` 입니다.

`Image.Binary` 와 `Image.Builder` 는 `PluginObject.h` 의 경계 있는 트랜잭션
작성기입니다 — `Reserve`, `Write`, `WriteAt`, `Tell`, `ReadAt`, `Insert`,
`Append`, `Resize`. 바이트를 패치하는 `post_emit` 인터셉터는 반드시 이것을 거쳐야
합니다. 예약 경계를 넘는 쓰기는 파일을 키우는 대신 스테이징을 중단시킵니다.

## 프로바이더

등록은 `Register` 중에만 하고, 그 이후에는 하지 않습니다.

### 링커 교체

```c
NevercLinkerProviderDescriptor Provider = {0};
Provider.Header = (NevercABITableHeader){sizeof(Provider),
                                         NEVERC_LINK_REGISTRAR_API_MAJOR,
                                         NEVERC_LINK_REGISTRAR_API_MINOR, 0};
Provider.ProviderID   = SV("com.example.my-linker");
Provider.TargetID     = MyTargetID;
Provider.InputFormat  = ELFFormatID;
Provider.OutputFormat = ELFFormatID;
Provider.OutputKind   = NEVERC_LINK_OUTPUT_EXECUTABLE;
Provider.Flags        = NEVERC_LINK_PROVIDER_DETERMINISTIC |
                        NEVERC_LINK_PROVIDER_CACHEABLE;
Provider.Link         = my_link;
Provider.VerifyImage  = my_verify;      /* 선택 사항 */
LinkRegistrar->RegisterLinkerProvider(LinkRegistrar->Context,
                                      RegistrarContext, &Provider);
```

콜백은 요청과 원시 입력 집합을 받아 후보를 채웁니다:

```c
static NevercStatus NEVERC_CALL
my_link(void *UserData, NevercTaskHandle Task,
        const NevercLinkRequest *Request,
        const NevercRawLinkInputSet *Inputs,
        NevercLinkerProductCandidate *OutCandidate) {
  /* Request->Target, ->OutputKind, ->OutputURI, ->Options, ->RequestDigest
     Inputs->Inputs 는 NevercRawLinkInput[], Inputs->OrderDigest 가 순서를 고정 */
  OutCandidate->Image     = MyImage;
  OutCandidate->Outputs   = MyBundle;
  OutCandidate->ProductID = MyProductID;
  return neverc_status_ok();
}
```

`NevercLinkOptions` 는 링커가 실제로 분기에 쓰는 플래그를 담습니다 — `PIE`,
`STATIC`, `GC_SECTIONS`, `ICF`, `EXPORT_DYNAMIC`, `ALLOW_UNDEFINED`,
`WHOLE_ARCHIVE`, `DETERMINISTIC` — 여기에 `EntrySymbol`, `InstallName`,
`Soname`, `ImageBase`, `PageSize`, `ThreadBudget`, 탐색 경로, 라이브러리가
더해집니다. 입력별 플래그는 `WHOLE_ARCHIVE`, `AS_NEEDED`, `START_GROUP`,
`END_GROUP`, `LAZY` 입니다.

성공하면 호스트가 후보를 채택합니다. 실패하면 만든 것은 여전히 프로바이더의
소유입니다. 봉인된 검증과 커밋 게이트는 어느 쪽이든 실행됩니다.

### 오브젝트 병합과 이미지 검증

`RegisterObjectMergeProvider` 는 `-r` 을 처리합니다. 요청은 입력
`NevercObjectMergeInput[]` 과 미리 열린 출력 그래프 및 변경을 함께 전달하므로,
프로바이더는 파일을 만드는 대신 호스트 소유의 트랜잭션에 씁니다.

`RegisterBinaryImageVerifier` 는 호스트 자신의 이미지 검증기와 나란히 도는
읽기 전용 검사를 추가합니다. 그것을 대체할 수는 없습니다.

## LTO

`lto_resolve` 는 심벌 해석을 만들고, `lto_generate` 는 비트코드를 오브젝트로
바꿉니다. `NevercLTOAPI` 는 둘 다 읽습니다.

```c
NevercLTORequest Request = {0};
Request.Header = /* … */;
LTO->GetRequest(LTO->Context, Task, RequestHandle, &Request);
/* .LinkRequest, .LinkGraph, .Target, .OutputFormat, .Options,
   .Modules, .Resolutions, .ResolutionDigest, .RequestDigest */
```

`GetModulePage` 와 `GetResolutionPage` 는 동일한 `NevercLinkEntityPage` 프로토콜
을 써서 `NevercLTOInputModuleInfo` 와 `NevercLTOSymbolResolution` 을 채웁니다.
각 해석은 모듈, 심벌, 대응하는 `NevercLinkSymbolHandle`, 그리고 플래그를
가리킵니다:

| 플래그 | 의미 |
|---|---|
| `PREVAILING` | 이 모듈이 정의를 소유한다. |
| `VISIBLE_TO_REGULAR_OBJECT` | 비트코드가 아닌 오브젝트가 볼 수 있다. |
| `EXPORTED` | 동적 심벌 테이블에 존재한다. |
| `FINAL_DEFINITION` | 이후 어떤 정의도 대체할 수 없다. |
| `CAN_INLINE` | 경계를 넘는 인라이닝이 허용된다. |
| `CAN_INTERNALIZE` | 내부화가 허용된다. |
| `LINKER_REDEFINED` | 링커가 덮어썼다. |
| `REFERENCED_BY_REGULAR_OBJECT` | 일반 오브젝트가 참조한다. |

`NevercLTOOptions` 는 `NEVERC_LTO_FULL` 또는 `NEVERC_LTO_THIN`, 최적화 수준들,
`ThreadBudget`, `ThinBackendPartitions`, CPU 와 기능, 그리고 `DISABLED`, `TASK`,
`LOCAL_SHARED`, `REMOTE_SHARED` 중의 캐시 범위를 고릅니다. 옵션 플래그는
`EMIT_OPTIMIZED_BITCODE`, `EMIT_INDEX`, `SAVE_TEMPS`,
`WHOLE_PROGRAM_VISIBILITY`, `UNIFIED_LTO`, `DETERMINISTIC` 입니다.

### LTO 프로바이더

```c
NevercLTOProviderDescriptor Provider = {0};
Provider.Header = /* … */;
Provider.ProviderID    = SV("com.example.my-lto");
Provider.TargetID      = MyTargetID;
Provider.Flags         = NEVERC_LTO_PROVIDER_THIN |
                         NEVERC_LTO_PROVIDER_DETERMINISTIC |
                         NEVERC_LTO_PROVIDER_CACHEABLE;
Provider.BuildCacheKey = my_cache_key;
Provider.Codegen       = my_codegen;
LTORegistrar->RegisterProvider(LTORegistrar->Context, RegistrarContext,
                               &Provider);
```

`BuildCacheKey` 는 호출자가 준 `NevercMutableByteView` 에 쓰고 필요한 크기를
보고하므로, 호스트는 버퍼 크기를 맞춰 다시 시도할 수 있습니다. 이것은 요청의
순수 함수여야 하며, `RequestDigest` 와 `ResolutionDigest` 에서 파생시키는 것이
안전한 구성입니다. 요청의 일부를 무시하는 키로 `CACHEABLE` 을 선언하면 깨끗한
재빌드에도 살아남는 낡은 오브젝트가 생깁니다.

`Codegen` 은 `NevercLTOProductCandidate` 를 채웁니다: `NevercLTOObjectProduct`
배열(각각 원본 모듈, ObjectGraph, 아티팩트를 지정), 선택적으로
`OptimizedBitcode` 와 `ThinIndex`, 그리고 실제로 사용한 `CacheKey`.

## 규칙

- 핸들은 태스크 범위이며 호스트 소유입니다. 콜백을 넘겨 보관하지 말고, 다른
  태스크에서 쓰지 말고, 값을 지어내지 마세요.
- `NevercLinkEntityPage.Data` 는 여러분 것입니다. 호스트는 최대
  `ElementCapacity × ElementStride` 바이트를 쓰고 참조를 남기지 않습니다.
- 모든 `BeginMutation` 은 오류 경로를 포함해 정확히 한 번의 `CommitMutation`
  또는 `AbandonMutation` 에 도달합니다.
- 변경 내용이 허락하는 한 상태 기계의 이른 지점에서 바꾸세요. 늦은 변경은 모든
  하류 단계를 조용히 무효화합니다.
- 옵서버에서 변경하지 마세요. 옵서버는 읽기 전용 브리지를 받으며 시도는
  `NEVERC_STATUS_POLICY_VIOLATION` 으로 거부됩니다.
- 이미지 바이트는 `NevercBinaryImageInfo.Binary` 와 그 빌더를 통해서만 쓰세요.
  넘치면 출력을 키우는 대신 스테이징이 중단됩니다.
- 같은 요청 다이제스트가 언제나 바이트 단위로 동일한 출력을 낼 때만
  `DETERMINISTIC` 을 주장하고, 캐시 키가 그 출력을 바꿀 수 있는 모든 입력을
  포괄할 때만 `CACHEABLE` 을 주장하세요.
- `image_verify`, `side_outputs_verify`, `commit` 은 봉인되어 있습니다. 관찰만
  하고 가로채거나 건너뛰려 하지 마세요.

규범적 선언은 `PluginLink.h` 와 `PluginLTO.h`, 스무 단계의 정책은
`Schema/PhaseSchema.json`, 각각을 고정하는 테스트는 `coverage.json` 을
참고하세요.

**언어**: [English](source.md) | [简体中文](source.zh-CN.md) | [繁體中文](source.zh-TW.md) | [日本語](source.ja.md) | [한국어](source.ko.md) | [Français](source.fr.md) | [Deutsch](source.de.md) | [Español](source.es.md) | [Italiano](source.it.md) | [Русский](source.ru.md) | [العربية](source.ar.md)

# NeverC 플러그인 소스 및 I/O API

`PluginSource.h` 는 두 개의 테이블을 게시합니다. `NevercIOAPI` 는 파일 시스템
그 자체로, 가상 파일 제공자, 읽기, 디렉터리 순회, 출력 싱크, 의존성 기록을
담당합니다. `NevercSourceLocationAPI` 는 컴파일러 내부의 위치를 파일, 행,
철자 텍스트로 되돌려 매핑합니다. 이 둘을 합치면 플러그인은 메모리에만 존재하는
헤더를 제공하거나, 매크로 확장을 그 철자 위치까지 해석하거나, 빌드의 지속성
회계에 참여하는 사이드밴드 출력을 쓸 수 있습니다.

## 인터페이스

```c
#include "neverc/Plugin/PluginSource.h"
```

| 인터페이스 | 테이블 | 버전 매크로 |
|---|---|---|
| `NEVERC_INTERFACE_IO_{HIGH,LOW}` | `NevercIOAPI` | `NEVERC_IO_API_MAJOR` / `_MINOR` |
| `NEVERC_INTERFACE_SOURCE_LOCATION_{HIGH,LOW}` | `NevercSourceLocationAPI` | `NEVERC_SOURCE_LOCATION_API_MAJOR` / `_MINOR` |

`NEVERC_SOURCE_API_MAJOR` 와 `_MINOR` 는 source-location 쌍의 별칭입니다.

## 세 개의 소스 단계

| 단계 | 정책 | 의미 |
|---|---|---|
| `neverc.source.resolve_input` | OBSERVABLE, INTERCEPTABLE | 드라이버 입력을 소스 입력으로 바꾼다 |
| `neverc.source.open` | 추가로 REPLACEABLE | 입력에 대한 소스 단위를 생성한다 |
| `neverc.source.after_open` | OBSERVABLE | 단위를 사용할 수 있게 되었다는 통지 |

`neverc.source.open` 은 대체 가능하므로, 제공자는 자신이 합성한 바이트를 담은
단위를 돌려줄 수 있습니다. 이것이 디스크를 건드리지 않고 생성 코드를 주입하는,
지원되는 방법입니다.

## 가상 파일 시스템 제공자

VFS 제공자는 경로 접두사를 담당하고, 컴파일러가 파일에 대해 던지는 네 가지
질문에 답합니다.

```c
typedef struct NevercVFSProviderDescriptor {
  NevercABITableHeader Header;
  NevercStringView ProviderID;
  NevercStringView RoutePrefix;
  NevercBool Deterministic;
  NevercBool Cacheable;
  uint64_t Reserved;
  NevercVFSPathPredicateFn MatchesPath;
  NevercVFSProviderStatusFn Status;
  NevercVFSProviderOpenReadFn OpenRead;
  NevercVFSProviderReadDirectoryFn ReadDirectory;
  NevercVFSProviderCanonicalizeFn Canonicalize;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercVFSProviderDescriptor;
```

각 콜백은 결과를 채우며, 그 안의 `Disposition` 이 제공자가 해당 요청을
처리했는지를 알려줍니다:

```c
static NevercStatus NEVERC_CALL
open_read(NevercTaskHandle Task, NevercStringView Path, void *UserData,
          NevercVFSOpenReadResult *OutResult) {
  static const char Header[] = "#define GENERATED 1\n";
  if (!path_matches(Path)) {
    OutResult->Disposition = NEVERC_VFS_RESULT_NOT_HANDLED;
    return neverc_status_ok();
  }
  OutResult->Disposition   = NEVERC_VFS_RESULT_HANDLED;
  OutResult->Status.Type   = NEVERC_VFS_FILE_REGULAR;
  OutResult->Status.Size   = sizeof(Header) - 1;
  OutResult->Content.Data  = (const uint8_t *)Header;
  OutResult->Content.Length = sizeof(Header) - 1;
  OutResult->Content.NullTerminated = NEVERC_TRUE;
  return neverc_status_ok();
}
```

`NEVERC_VFS_RESULT_NOT_HANDLED` 를 반환하면 다음 제공자로, 최종적으로는 실제
파일 시스템으로 넘어갑니다. 파일 유형은 `NEVERC_VFS_FILE_UNKNOWN`, `REGULAR`,
`DIRECTORY`, `SYMLINK`, `OTHER` 입니다.

`Register` 안에서 등록합니다:

```c
IO->RegisterVFSProvider(IO->Context, RegistrarContext, &Descriptor);
```

한 세션 동안만 존재하면 되는 단일 인메모리 파일이라면 제공자를 아예 건너뛰어도
됩니다:

```c
IO->AddMemoryFile(IO->Context, Session, SV("/virtual/config.h"),
                  Content, ModificationTime);
```

[`pluginsdk/examples/VirtualHeaderPlugin.c`](../../pluginsdk/examples/VirtualHeaderPlugin.c)
가 완전히 동작하는 제공자입니다.

## 파일 읽기

```c
NevercVFSStatus Status;
IO->Stat(IO->Context, Task, Path, &Status);

NevercFileHandle File;
IO->OpenFileForRead(IO->Context, Task, Path, &File);

NevercBufferHandle Buffer;
IO->ReadFile(IO->Context, Task, File, /*Offset=*/0, /*Length=*/Status.Size,
             &Buffer);

NevercBufferView View;
IO->GetBufferView(IO->Context, Task, Buffer, &View);
/* View.Data / View.Length / View.NullTerminated */

IO->ReleaseBuffer(IO->Context, Task, Buffer);
IO->CloseFile(IO->Context, Task, File);
```

`CopyBuffer` 는 여러분이 소유한 바이트를 호스트 버퍼로 바꾸고, `Canonicalize` 는
경로를 해석하며, `GetWorkingDirectory` / `SetWorkingDirectory` 는 태스크의 현재
디렉터리를 다룹니다. 디렉터리는 `OpenDirectory`, `ReadDirectory`(끝에 도달하면
`OutHasEntry` 를 `NEVERC_FALSE` 로 설정), `CloseDirectory` 로 순회합니다.

I/O 오류 코드는 `NevercStatus.Detail` 에 보고됩니다:
`NEVERC_IO_ERROR_NOT_FOUND`, `PERMISSION_DENIED`, `NOT_DIRECTORY`,
`IS_DIRECTORY`, `INVALID_PATH`, `IO`.

## 출력 쓰기

출력은 트랜잭션 방식입니다. 싱크를 열고, 쓰고, finish 하여 봉인(seal) — 크기와
32바이트 다이제스트 — 을 얻습니다. 빌드 시스템은 이를 검증할 수 있습니다.

```c
NevercOutputSinkHandle Sink;
IO->BeginFileOutput(IO->Context, Task, SV("out.json"), /*SizeBudget=*/0, &Sink);
IO->OutputWrite(IO->Context, Task, Sink, Bytes);
IO->OutputMetadataSet(IO->Context, Task, Sink, SV("content-type"),
                      SV("application/json"));

NevercOutputSeal Seal = {0};
Seal.Header = (NevercABITableHeader){sizeof(Seal), NEVERC_IO_API_MAJOR,
                                     NEVERC_IO_API_MINOR, 0};
IO->OutputFinish(IO->Context, Task, Sink, &Seal);
```

| 함수 | 용도 |
|---|---|
| `BeginMemoryOutput` | 메모리로 뒷받침되고 논리 이름을 가지는 싱크 |
| `BeginFileOutput` | 최종 경로에 원자적으로 안착하는 싱크 |
| `BeginStreamOutput` | `NEVERC_OUTPUT_STREAM_STDOUT` 또는 `_STDERR` 위의 싱크 |
| `OutputWrite`, `OutputWriteAt` | 덧붙여 쓰기, 또는 지정 오프셋에 쓰기 |
| `OutputTell`, `OutputTruncate` | 위치와 크기 제어 |
| `OutputMetadataSet` | 출력에 키/값 쌍을 붙인다 |
| `OutputFinish` | 출력을 봉인하고 `NevercOutputSeal` 을 만든다 |
| `OutputAbort` | 쓴 것을 모두 버린다 |
| `OutputGetSummary` | 상태, 플래그, 크기, 다이제스트를 언제든 살펴본다 |

`NevercOutputSummary.State` 는 `NEVERC_OUTPUT_OPEN`, `FINISHED`, `COMMITTED`,
`ABORTED`, `FAILED_PARTIAL` 사이를 오가고, `Flags` 는 `PUBLISHED`, `DURABLE`,
`MAY_BE_PARTIAL`, `RECOVERY_REQUIRED`, `DURABILITY_UNCONFIRMED` 를 기록합니다.
이 플래그들은 드라이버가 `NevercStatus.Flags` 로 드러내는 것과 같은 정보이므로,
쓰기 도중의 크래시와 깔끔한 실패를 구별할 수 있습니다.

`SizeBudget` 이 0 이면 상한이 없습니다. 0 이 아닌 예산을 주면 초과 시 디스크를
가득 채우는 대신 `NEVERC_STATUS_RESOURCE_EXHAUSTED` 로 실패합니다.

## 의존성 기록

빌드 시스템이 추적해야 할 무언가를 플러그인이 읽었다면, 그렇다고 말하십시오.
그렇지 않으면 그 입력이 바뀌어도 증분 빌드는 다시 빌드하지 않습니다.

```c
NevercDependencyDescriptor Dependency = {0};
Dependency.Header = (NevercABITableHeader){sizeof(Dependency),
                                           NEVERC_IO_API_MAJOR,
                                           NEVERC_IO_API_MINOR, 0};
Dependency.CanonicalPath = SV("/etc/mytool/rules.txt");
Dependency.ContentDigest = Digest;
Dependency.Kind          = NEVERC_INPUT_DEPENDENCY_RESOURCE;
Dependency.System        = NEVERC_FALSE;
Dependency.ProviderID    = SV("com.example.myplugin");

NevercDependencyHandle Handle;
IO->RecordDependency(IO->Context, Task, &Dependency, &Handle);
```

종류는 `NEVERC_INPUT_DEPENDENCY_SOURCE`, `INCLUDE`, `MODULE`, `RESOURCE`,
`TOOL`, `PLUGIN` 입니다.

## 소스 위치

`NevercSourceLocation` 은 불투명합니다. 위치 테이블이 그것을 출력하거나 비교할
수 있는 무언가로 바꿔 줍니다.

```c
NevercSourceLocationInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info),
                                     NEVERC_SOURCE_LOCATION_API_MAJOR,
                                     NEVERC_SOURCE_LOCATION_API_MINOR, 0};
Source->GetLocationInfo(Source->Context, Task, Location, &Info);
/* Info.Kind 는 NEVERC_SOURCE_LOCATION_FILE 또는 _MACRO;
   이어서 Info.FileOffset, Info.Line, Info.Column. */
```

위치의 여러 관점 사이를 오가는 변환은 네 가지이며, 모두
`NevercTransformSourceLocationFn` 시그니처를 공유합니다:

| 함수 | 반환하는 것 |
|---|---|
| `GetSpellingLocation` | 토큰의 문자가 실제로 쓰여 있는 곳 |
| `GetExpansionLocation` | 매크로 확장이 소스에 나타나는 곳 |
| `GetFileLocation` | 가장 가까운 파일 위치 |
| `GetIncludeLocation` | 그 파일을 끌어들인 `#include` |
| `GetTokenEnd` | 토큰 마지막 문자의 바로 다음 |

`GetPresumedLocation` 은 `#line` 지시문을 적용해 파일명, 행, 열, include 위치를
내놓습니다. `GetLocationFile` 과 `GetFileInfo` 를 함께 쓰면 정규 경로, 크기,
수정 시각, 고유 ID, 그리고 그 파일이 사용자·시스템·extern-C 시스템 중 무엇인지를
얻습니다:

```c
typedef struct NevercFileInfo {
  NevercABITableHeader Header;
  NevercStringView Path;
  NevercStringView CanonicalPath;
  uint64_t Size;
  int64_t ModificationTime;
  NevercFileUniqueID UniqueID;      /* {Device, File} */
  NevercFileCharacteristic Characteristic;
  NevercBool NamedPipe;
} NevercFileInfo;
```

범위는 `GetRangeInfo` 로 읽고(`Begin`, `End`, 그리고 그 범위가
`NEVERC_SOURCE_RANGE_CHARACTER` 인지 `_TOKEN` 인지를 보고합니다), 바이트 자체는
`GetSourceText` 나 `GetCharacterData` 로 읽습니다.

한 번에 많은 위치가 필요할 때 — 예컨대 함수 전체를 진단으로 훑을 때 — 는 위치마다
호출하는 대신 배치 형태를 쓰십시오:

```c
Source->GetLocationInfoBatch(Source->Context, Task, Locations, LocationCount,
                             OutInfos, OutInfoCapacity);
```

## 소스 단위

입력과 그 바이트를 단계 수준에서 바라본 모습입니다:

```c
NevercSourceInputInfo Input = {0};
Source->GetSourceInput(Source->Context, Frame, Frame->Input, &Input);
/* Input.Path, .Kind(FILE 또는 BUFFER), .Language, .System, .Preprocessed */
```

`neverc.source.open` 의 제공자는 메모리로 뒷받침되는 단위로 응답합니다:

```c
NevercMemorySourceUnitDescriptor Unit = {0};
Unit.Header = (NevercABITableHeader){sizeof(Unit),
                                     NEVERC_SOURCE_LOCATION_API_MAJOR,
                                     NEVERC_SOURCE_LOCATION_API_MINOR, 0};
Unit.LogicalPath      = SV("/virtual/generated.c");
Unit.CanonicalIdentity = SV("com.example:generated:v1");
Unit.Content          = Bytes;
Unit.ProviderID       = SV("com.example.myplugin");
Unit.Deterministic    = NEVERC_TRUE;
Unit.Cacheable        = NEVERC_TRUE;

NevercArtifactHandle Output;
Source->CreateMemorySourceUnit(Source->Context, Frame, Frame->Input, &Unit,
                               &Output);
```

캐시가 키로 삼는 것은 `CanonicalIdentity` 이므로, 내용이 바뀌면 반드시 함께
바뀌어야 합니다. `GetSourceUnit` 은 단위를 되읽으면서 `MemoryBacked` 도 함께
보고합니다.

## 규칙

- `ReadFile`, `CopyBuffer`, `PathToBuffer` 가 준 버퍼는 호스트 소유입니다. 하나도
  빠짐없이 `ReleaseBuffer` 로 해제하십시오.
- 모든 `OpenFileForRead` 에는 `CloseFile` 이, 모든 `OpenDirectory` 에는
  `CloseDirectory` 가, 모든 출력 싱크에는 `OutputFinish` 또는 `OutputAbort` 가
  필요합니다.
- `NevercFileInfo`, `NevercVFSStatus`, 위치 결과 안의 뷰는 해당 콜백 동안만
  빌려온 것입니다.
- VFS 제공자 콜백은 태스크 스레드에서 실행되며 컴파일러를 다시 호출해서는 안
  됩니다. 이미 가지고 있는 데이터로 답하십시오.
- `Deterministic` 과 `Cacheable` 은 정직하게 선언하십시오. 시계나 환경을 읽으면서
  결정성을 주장하는 제공자는 오염된 빌드 캐시를 만듭니다.
- `AddMemoryFile` 은 세션 범위입니다. 내용이 태스크에 따라 달라진다면 제공자가
  올바른 도구입니다.

규범적 선언은 `PluginSource.h` 를, 완전한 제공자 예제는
`pluginsdk/examples/VirtualHeaderPlugin.c` 를 참조하십시오.

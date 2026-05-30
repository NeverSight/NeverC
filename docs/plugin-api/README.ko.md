**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# NeverC Out-of-Tree 플러그인 API

NeverC는 out-of-tree 패스 플러그인을 위한 **순수 C ABI**를 제공합니다. 플러그인은 공유 라이브러리(`.dll` / `.so` / `.dylib`)로서, 컴파일 파이프라인의 지정된 지점에 커스텀 패스를 등록합니다. 플러그인 컴파일에 필요한 것은 **헤더 하나**(`NevercPluginAPI.h`)뿐이며, LLVM이나 CRT 의존성이 **전혀** 없습니다 — 모든 기능은 호스트가 제공하는 vtable을 통해 라우팅됩니다.

## 1. 빠른 시작

### 최소 플러그인

```c
#include "NevercPluginAPI.h"

static int myPass(NevercModuleRef M, const NevercHostAPI *API, void *UD) {
    (void)UD;
    unsigned Count = 0;
    NEVERC_FOR_EACH_DEFINED_FUNCTION(API, M, F) {
        (void)F;
        Count++;
    }
    API->DiagNoteF("[my-plugin] %u defined functions", Count);
    return 0;
}

static void registerPasses(const NevercHostAPI *API, void *Reg) {
    API->RegisterModulePass(Reg, NEVERC_HOOK_PRE_OPT, myPass, NULL, "my-pass");
}

NEVERC_EXPORT NevercPluginInfo nevercGetPluginInfo(void) {
    NevercPluginInfo Info;
    Info.APIVersion     = NEVERC_PLUGIN_API_VERSION;
    Info.PluginName     = "my-plugin";
    Info.PluginVersion  = "1.0.0";
    Info.RegisterPasses = registerPasses;
    Info.Destroy        = NULL;
    return Info;
}
```

### 빌드

```bash
cc -shared -o MyPlugin.dll MyPlugin.c -I/path/to/pluginsdk/include

cmake -S /path/to/pluginsdk/examples -B build
cmake --build build
```

### 실행

```bash
neverc -fplugin-pass=./MyPlugin.dll input.c -o output.obj
```

## 2. 아키텍처

**핵심 특성:**

- **단일 헤더 SDK**: 플러그인 컴파일에 `NevercPluginAPI.h`만 필요.
- **제로 의존성**: LLVM 헤더 불필요, CRT 링크 불필요. 모든 작업은 vtable 경유.
- **순수 C ABI**: C, C++, Zig, Rust(FFI) 등 C 링키지 공유 라이브러리를 생성할 수 있는 모든 언어로 작성 가능.
- **버전 안전**: `NEVERC_API_FN(api, Field)`로 호출 전 선택적 vtable 항목 확인.

## 3. 플러그인 진입점

모든 플러그인은 다음 함수를 내보내야 합니다:

```c
NEVERC_EXPORT NevercPluginInfo nevercGetPluginInfo(void);
```

| 필드 | 타입 | 설명 |
|------|------|------|
| `APIVersion` | `uint32_t` | `NEVERC_PLUGIN_API_VERSION`이어야 함 |
| `PluginName` | `const char *` | 사람이 읽을 수 있는 이름 |
| `PluginVersion` | `const char *` | 시맨틱 버전 문자열 |
| `RegisterPasses` | 함수 포인터 | 모든 패스를 등록하기 위해 한 번 호출 |
| `Destroy` | 함수 포인터 | 선택적 정리, `NULL` 가능 |

## 4. 패스 유형

- **Module Pass (IR)**: LLVM IR 모듈 조작. IR 읽기 및 수정 가능.
- **Machine Pass (MIR)**: 명령어 선택 후 머신 레벨 IR 조작.
- **Binary Pass**: 원시 바이트 조작 (쉘코드 추출, 바이너리 패치).
- **Linker Pass**: 링크 시 동작, 심볼 및 섹션 접근.

## 5. 훅 포인트

### 일반 흐름

| 훅 | 레벨 | 설명 |
|----|------|------|
| `NEVERC_HOOK_PRE_OPT` | IR | LLVM 최적화 패스 전 |
| `NEVERC_HOOK_POST_OPT` | IR | LLVM 최적화 패스 후 |
| `NEVERC_HOOK_PIPELINE_START` | IR | 파이프라인 시작 |
| `NEVERC_HOOK_PIPELINE_LAST` | IR | IR 파이프라인 끝 |
| `NEVERC_HOOK_BEFORE_CODEGEN_PREEMIT` | MIR | pre-emit 머신 패스 전 |
| `NEVERC_HOOK_AFTER_CODEGEN_FINAL_MIR` | MIR | 모든 머신 패스 후 |

### 쉘코드 / LTO / 링커 흐름

쉘코드 훅은 `NEVERC_HOOK_SC_*`, LTO 훅은 `NEVERC_HOOK_LTO_*`, 링커 훅은 `NEVERC_HOOK_LINK_*` 접두사를 사용합니다.

## 6. 불투명 핸들 타입

모든 IR/MIR 객체는 불투명 핸들을 통해 접근합니다. 핸들은 수신한 **패스 콜백의 스코프 내에서만 유효**합니다.

## 7. 데이터 구조

호스트는 vtable을 통해 고성능 데이터 구조를 제공합니다: **Arena** (범프 포인터 할당자), **StrMap** (문자열 키 해시 맵), **IntMap** (정수 키 해시 맵), **StrBuilder** (점진적 문자열 구성), **ValueSet** (값 해시 셋).

## 8. 버전 호환성

```c
if (NEVERC_API_FN(API, SomeNewFunction)) {
    API->SomeNewFunction(...);
}
```

## 9. 플러그인 인수

```bash
neverc -fplugin-pass=./MyPlugin.dll \
       -fplugin-pass-arg=verbose=1 \
       input.c -o output.obj
```

## 10. 수명 규칙

| 리소스 | 수명 | 정리 |
|--------|------|------|
| 불투명 핸들 | 패스 콜백 내 | 해제 불필요 |
| `NevercBuilderRef` | `BuilderCreate`로 생성 | `BuilderDispose` |
| 힙 문자열 | 호출자 소유 | `Free` |
| Arena 할당 | Arena 소유 | `ArenaDestroy` |

## 11. 모범 사례

1. **Arena 우선 할당**: 임시 데이터에는 `NEVERC_TRY_ARENA` 사용. 하나의 `ArenaDestroy`로 N개의 `Free` 대체.
2. **버전 가드**: 새로운 vtable 호출은 항상 `NEVERC_API_FN`으로 감싸기.
3. **콜백 반복 우선**: `ModuleForEachDefinedFunction`이 매크로보다 빠름.
4. **CRT 의존성 없음**: 모든 작업은 vtable 경유. `malloc` / `printf` / `qsort` 직접 호출 금지.
5. **깨끗한 반환**: 패스 반환 전 모든 리소스 해제.

## 12. Plugin SDK 내용

```
pluginsdk/
├── include/
│   └── neverc/
│       └── Plugin/
│           └── NevercPluginAPI.h    # 필요한 유일한 헤더
└── examples/
    ├── CMakeLists.txt       # 독립 빌드 템플릿
    ├── ExamplePlugin.c      # 종합 데모
    ├── CrtShimPlugin.c      # 제로 CRT 의존성 개념 증명
    └── BenchPlugin.c        # HostAPI 처리량 마이크로벤치마크
```

## 13. 관련 문서

- [Shellcode 플러그인 인터페이스](../shellcode-compiler/plugin-interface/README.ko.md) — shellcode 파이프라인의 in-tree C++ 확장 포인트.

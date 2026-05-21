**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 문서](../README.ko.md)

# NeverC 내장 런타임 시스템

NeverC는 옵트인 방식의 내장 런타임으로 표준 C를 확장합니다. 이 런타임들은 LLVM bitcode로 컴파일러 바이너리에 직접 임베드됩니다. 컴파일러 플래그로 활성화하면, 해당 런타임이 컴파일 시 사용자 IR에 병합됩니다 — 외부 헤더, 라이브러리, 링크 시 의존성이 필요 없습니다.

## 사용 가능한 내장 기능

| 내장 기능 | 플래그 | 기본값 | 설명 |
|----------|--------|--------|------|
| [**`string`**](string/README.ko.md) | `-fbuiltin-string` | 꺼짐 | 값 의미론 문자열 타입. 도트 호출 메서드, 자동 메모리 관리, 네이티브 UTF-8 |
| [**`mimalloc`**](mimalloc/README.ko.md) | `-fbuiltin-mimalloc` | **켜짐** | 고성능 메모리 할당자. `malloc`/`free`/`calloc`/`realloc` 투명 대체 |

두 내장 기능은 기본적으로 비활성화되며 명시적 옵트인이 필요합니다. 조합 사용 가능:

```bash
neverc -fbuiltin-string -fbuiltin-mimalloc main.c -o main
```

---

## 아키텍처 개요

모든 내장 기능은 동일한 4계층 아키텍처를 공유합니다:

1. **언어 옵션 및 드라이버 플래그** — `LangOptions.def`에 `LangOption` 정의
2. **Foundation API** — `getEmbeddedBitcode()` 및 `isSupported()` 제공
3. **CMake 부트스트랩** — 2단계 부트스트랩으로 bitcode 생성
4. **IR 병합 패스** — `PipelineStartEP`에서 bitcode를 사용자 모듈에 병합

---

## 내장 기능 간 설계 차이

| 측면 | `string` | `mimalloc` |
|------|----------|------------|
| **병합 전략** | 온디맨드 (BFS 호출 그래프, 미사용 제거) | 전체 아카이브 (모든 심볼 유지) |
| **플랫폼 bitcode** | 단일 (아키텍처 독립) | OS별 (Linux / Darwin / Windows) |
| **심볼 처리** | 모두 내부화 | 오버라이드 진입점은 외부 링크 유지 |
| **전처리기 매크로** | *(없음)* | `__NEVERC_MIMALLOC__` |
| **셸코드 모드** | 자동 활성화, 아레나 재작성 | 억제 (HeapArenaPass가 힙 처리) |
| **최적화 레벨** | `-O0` (bitcode 컴파일) | `-O2` (성능 중요 할당자) |
| **DCE** | 병합 전 프루닝 + 병합 후 마크 앤 스윕 | DCE 없음 (전체 아카이브 시맨틱스) |

---

## 안전 인터록

| 조건 | 효과 | 이유 |
|------|------|------|
| `-fno-builtin` | mimalloc 억제 | CRT 오버라이드 시나리오 없음 |
| `-mkernel` | mimalloc 억제 | 커널에 유저스페이스 힙 없음 |
| `-fshellcode-mode` | mimalloc 억제 | HeapArenaPass로 대체 (아레나 기반) |
| `-ffreestanding` | mimalloc 억제 | 오버라이드할 libc 없음 |

`string` 내장에는 자체 억제 로직이 있습니다 (셸코드 파이프라인에서 아레나 재작성이 힙 할당을 대체).

### HeapArenaPass (셸코드 힙 할당)

`-fshellcode-mode`가 활성화되면, `mimalloc`은 억제되지만 `malloc`/`free`/`calloc`/`realloc` 호출은 `HeapArenaPass`에 의해 자동으로 재작성됩니다 (기본 활성화). 이 패스는 하이브리드 전략을 사용합니다:

- **소규모 할당 (≤ 64 KB)**: `string` 내장 런타임과 공유하는 스택 상주 아레나에서 할당 (범프 할당자 + 프리 리스트 재사용).
- **대규모 할당 (> 64 KB) 또는 아레나 OOM**: OS 할당자로 폴백:
  - **Windows**: `malloc`/`free`를 PEB walk를 통해 `msvcrt.dll`에서 해결 (`-mshellcode-win-peb-import`).
  - **Linux / macOS / Android**: `mmap`/`munmap`을 네이티브 시스콜로 인라인 (`-mshellcode-syscall`).
  - **임포트 패스 미활성화**: 아레나 전용; OOM 시 `NULL` 반환.

드라이버 플래그로 제어:

```bash
neverc -fshellcode test.c -o test.bin                     # HeapArenaPass 켜짐 (기본값)
neverc -fshellcode -fno-shellcode-heap-arena test.c       # HeapArenaPass 꺼짐 (기존 동작)
```

---

## 전처리기 매크로

내장 기능이 활성 상태일 때, 해당 전처리기 매크로가 정의됩니다:

```c
#ifdef __NEVERC_MIMALLOC__
// mimalloc 활성 — malloc/free가 투명하게 오버라이드됨
#endif
```

---

## 파일 구조

```
neverc/
├── include/neverc/Foundation/
│   ├── LangOpts/LangOptions.def          # LANGOPT 선언
│   └── Builtin/
│       ├── BuiltinString.h               # string API
│       ├── BuiltinMimalloc.h             # mimalloc API
│       └── ...
│
├── lib/Foundation/
│   ├── CMakeLists.txt                    # 모든 내장의 부트스트랩 타겟
│   └── Builtin/
│       ├── BuiltinString.cpp             # string bitcode 임베딩
│       ├── BuiltinMimalloc.cpp           # mimalloc OS별 bitcode 임베딩
│       ├── bin2c.py                      # .bc → C 헤더 변환기 (공유)
│       ├── gen_string_runtime.py         # string 소스 생성기
│       └── gen_mimalloc_source.py        # mimalloc 소스 생성기
│
├── lib/Emit/Backend/
│   ├── BackendUtil.cpp                   # PipelineStartEP 등록
│   ├── StringRuntimeLinker.{h,cpp}       # string IR 병합 패스
│   └── MimallocRuntimeLinker.{h,cpp}     # mimalloc IR 병합 패스
│
├── lib/Invoke/ToolChains/
│   └── NeverC.cpp                        # addNeverCFeatureFlags()
│
└── lib/Compiler/Preprocessor/
    └── InitPreprocessor.cpp              # __NEVERC_MIMALLOC__ 매크로
```

---

## 새 내장 기능 추가

1. `LangOptions.def`에 `LANGOPT` 추가
2. `Options.td.h`에 드라이버 플래그 추가
3. Foundation API 생성 (`BuiltinFoo.h` + `.cpp`)
4. 소스 생성기 작성
5. CMake 부트스트랩 타겟 추가
6. IR 패스 생성 및 `PipelineStartEP` 등록
7. 전처리기 매크로 정의
8. 안전 검사 추가
9. 테스트 추가
10. 문서 및 i18n 번역 추가

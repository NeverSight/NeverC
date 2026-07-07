**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# 커스텀 호출 규약

NeverC는 **데이터 기반 커스텀 호출 규약**을 지원합니다. 외부 플러그인이나 소스 코드 속성을 통해 모든 함수의 인수와 반환값에 임의의 물리 레지스터를 할당할 수 있으며, 컴파일러 자체나 TableGen 정의를 수정할 필요가 없습니다.

## 개요

기존 LLVM 호출 규약은 `.td` / `.inc` 파일로 백엔드에 하드코딩됩니다. NeverC는 이를 **런타임 데이터 기반** 방식으로 대체합니다:

- **레지스터 할당 스펙**(문자열)이 함수의 문자열 속성으로 첨부됩니다.
- 백엔드가 이 스펙을 읽고 지정된 물리 레지스터에 인수/반환값을 할당합니다.
- 스펙은 **외부 플러그인**(IR 패스), **소스 코드 속성**(`__attribute__` / `__declspec`), 또는 둘 다에서 제공할 수 있습니다.

## 스펙 형식

세미콜론으로 구분된 문자열이며, 각 세그먼트는 키와 쉼표로 구분된 레지스터 이름으로 구성됩니다(대소문자 구분 없음, 공백 허용):

```
gpr:rcx,rdx,r8,r9; xmm:xmm0,xmm1; ret:rax; ret_xmm:xmm0
```

| 세그먼트 | 별칭 | 의미 |
|---|---|---|
| `args` | | **위치 모드**: 각 토큰은 레지스터 이름 또는 `stack`/`mem` |
| `gpr` | `arg_gpr` | **풀 모드**: 정수/포인터 인수 레지스터 (순서대로, 소진 시 스택으로) |
| `xmm` | `arg_xmm` | **풀 모드**: 부동소수점/벡터 인수 레지스터 |
| `fpr` | `arg_fpr` | AArch64에서의 `xmm` 별칭 |
| `ret_gpr` | `ret` | 정수/포인터 반환값 레지스터 |
| `ret_xmm` | | 부동소수점/벡터 반환값 레지스터 |
| `ret_fpr` | | AArch64에서의 `ret_xmm` 별칭 |
| `csr` | | 커스텀 callee-saved 레지스터 집합 (기본값: 표준 ABI 집합) |

### 두 가지 인수 모드

**풀 모드**(`gpr:` / `xmm:`): 정수 인수는 `gpr` 풀에서, 부동소수점 인수는 `xmm` 풀에서 순서대로 가져옵니다. 풀 소진 후 나머지는 스택으로 스필합니다.

**위치 모드**(`args:`): *i*번째 인수는 *i*번째 토큰을 사용합니다. `stack` / `mem`으로 해당 인수를 강제로 스택에 배치:

```
args:rcx,stack,r8;ret:rax   # 인수0→rcx, 인수1→스택, 인수2→r8, 반환→rax
```

### 지원 아키텍처

| 아키텍처 | GPR 이름 | SIMD 이름 | 비트 폭 선택 |
|---|---|---|---|
| **x86-64** | `rax`, `rcx`, `rdx`, `rsi`, `rdi`, `r8`–`r11` | `xmm0`–`xmm15` | i32→32비트, i64→64비트 |
| **AArch64** | `x0`–`x28` | `v0`–`v31` | i32→`w`, i64→`x`, f16→`h`, f32→`s`, f64→`d`, f128/vec→`q` |

### 제약

- **Callee-saved**: 기본값은 표준 ABI 집합. `csr:r12,r13`으로 커스텀 집합 선언 (x86-64 / AArch64 모두 지원).
- **예약 레지스터**: 스택 포인터(`rsp` / `sp`)와 AArch64의 `x29`/`x30`(FP/LR)는 인수/반환 레지스터로 지정할 수 없습니다(스펙에 써도 무시됨).
- **csr 충돌**: 어떤 레지스터가 `csr`와 인수/반환 목록에 모두 나타나면 bridge가 경고를 출력합니다.
- **가변 인수 함수**: 미지원 — 명확한 오류 출력.
- **간접 호출**: 함수 포인터 호출은 커스텀 규약 적용 불가. 주소 취득 시 경고, 간접 호출은 표준 규약으로 폴백.
- **꼬리 호출**: 커스텀 규약 함수에서 자동 비활성화.

## 사용법

### 1. 플러그인 기반 (권장)

```bash
cd pluginsdk/examples && make CustomCallConvPlugin.dylib
# 속성 모드 (기본값)
neverc -fplugin-pass=./CustomCallConvPlugin.dylib input.c -o output.o
# 전역 모드
neverc -fplugin-pass=./CustomCallConvPlugin.dylib \
       -fplugin-pass-arg=cc-all=1 \
       -fplugin-pass-arg=ccspec="gpr:r10,r11,rsi;ret:rdx" \
       input.c -o output.o
```

### 2. 소스 코드 속성

```c
__attribute__((custom_attr("neverc-callconv", "gpr:r10,r11,rsi;ret:rdx")))
int add3(int a, int b, int c) { return a + b + c; }

__declspec(custom_attr("neverc-callconv", "gpr:r10;ret:rdx"))
int msfunc(int a) { return a; }
```

### 3. 혼합 사용

소스 속성과 플러그인 인수를 동시에 사용할 수 있습니다. 각 함수는 최대 1회 처리됩니다.

## LTO 지원

플러그인은 `NEVERC_INTERPOSE_POST_OPT`와 `NEVERC_INTERPOSE_LTO_POST_OPT` 모두에 등록됩니다. LTO로 번역 단위가 병합된 후에도 커스텀 규약을 적용할 수 있습니다.

## 플러그인 API

```c
API->FunctionSetCustomCallConv(F, "gpr:r10,r11,rsi;ret:rdx");
```

`CallingConv::NeverC_Custom`(CC 1000)을 설정하고, 속성을 기록하며, **모든 직접 호출 사이트를 동기화**합니다. `NULL` 또는 `""`를 전달하면 해제됩니다.

## 테스트

GoogleTest 스위트 (22개 테스트, 모두 PASS):

```bash
ninja -C build-neverc neverc-tests
build-neverc/bin/neverc-tests --gtest_filter='CustomCallConvTest.*'
```

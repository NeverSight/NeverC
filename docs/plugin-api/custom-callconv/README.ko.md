**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 플러그인 ABI](../README.ko.md)

# 사용자 정의 호출 규약

NeverC는 **데이터 기반 사용자 정의 호출 규약**을 지원합니다. 컴파일러 본체나 TableGen 정의를 전혀 건드리지 않고, 아웃오브트리 플러그인이나 소스 수준 속성만으로 임의 함수의 인자와 반환값에 임의의 물리 레지스터를 배정할 수 있습니다.

## 개요

전통적인 LLVM 호출 규약은 `.td` / `.inc` 파일을 통해 백엔드에 박혀 있습니다. 하나를 추가하거나 수정하려면 컴파일러 소스를 편집하고 TableGen을 다시 실행해야 합니다. NeverC는 이를 두 계층으로 구성된 **런타임 데이터 기반** 모델로 대체합니다.

- **spec** — `gpr:rcx,rdx;ret:rax` 같은, 사람이 직접 쓸 수 있는 짧은 문자열 — 이 플러그인이나 소스 수준 속성에 의해 `"neverc-callconv"` 문자열 속성으로 함수에 붙습니다.
- 코드 생성 전에 호스트가 그 spec을 `"neverc-cc-plan-v1"` 속성으로 **구체화(materialize)** 합니다. 이는 특정 타깃 스키마에 묶인, 불변이며 검증된 정확한 위치 표입니다. 백엔드는 plan만 소비합니다.

spec은 여러분이 쓰는 것이고, plan은 백엔드가 신뢰하는 것입니다. 덕분에 호출 규약은 "컴파일 타임에 백엔드에 하드코딩"에서 "런타임에 외부 정책이 주도"로 옮겨 가면서도 검증을 포기하지 않습니다.

## Spec 형식

spec은 세미콜론으로 구분된 문자열입니다. 각 구간은 키와 쉼표로 구분된 레지스터 이름 목록으로 이루어집니다(대소문자 구분 없음, 공백 허용).

```
gpr:rcx,rdx,r8,r9; xmm:xmm0,xmm1; ret:rax; ret_xmm:xmm0
```

| 구간 | 별칭 | 의미 |
|---|---|---|
| `args` | | **위치 모드**: 각 토큰은 레지스터 이름 또는 `stack`/`mem`이며 인자 인덱스 순으로 대응합니다 |
| `gpr` | `arg_gpr` | **풀 모드**: 정수/포인터 인자 레지스터. 순서대로 소비하며, 소진되면 스택으로 넘칩니다 |
| `xmm` | `arg_xmm` | **풀 모드**: 부동소수점/벡터 인자 레지스터 |
| `fpr` | | `xmm`의 타깃 중립 별칭 |
| `ret_gpr` | `ret` | 정수/포인터 반환 레지스터 |
| `ret_xmm` | | 부동소수점/벡터 반환 레지스터 |
| `ret_fpr` | | `ret_xmm`의 타깃 중립 별칭 |
| `csr` | | 사용자 정의 callee-saved 레지스터 집합(기본값: 표준 ABI 집합) |

어떤 구간이든 생략할 수 있고, 인식되지 않는 구간은 무시됩니다. 이 키들은 [`llvm/include/llvm/CodeGen/NeverCCallConv.h`]에 한 번만 정의되므로 생산자와 파서가 어긋날 수 없습니다.

### 두 가지 인자 모드

**풀 모드**(`gpr:` / `xmm:`): 정수 인자는 `gpr` 풀에서 순서대로 레지스터를 가져가고, 부동소수점과 벡터 인자는 `xmm`에서 가져갑니다. 풀이 소진되면 남은 인자는 스택으로 넘칩니다.

**위치 모드**(`args:`): *i* 번째 인자는 *i* 번째 토큰을 사용합니다. 각 토큰은 레지스터 이름이거나, 그 인자를 강제로 스택에 두는 `stack` / `mem`입니다.

```
args:rcx,stack,r8;ret:rax   # 인자0→rcx, 인자1→스택, 인자2→r8, 반환→rax
```

`args`가 있으면 `gpr` / `xmm`보다 우선합니다. 인자 타입에 맞지 않는 레지스터 클래스를 지목한 토큰, 토큰 목록을 벗어난 인덱스, 이미 할당된 레지스터는 모두 빌드를 실패시키지 않고 스택 슬롯으로 폴백합니다.

### 지원 아키텍처

레지스터 이름은 타깃별 표를 통해 해석되며, 이 표가 spec에 쓸 수 있는 이름의 유일한 근거입니다.

| 아키텍처 | GPR 이름 | SIMD 이름 | 폭 선택 |
|---|---|---|---|
| **x86-64** | `rax`, `rbx`, `rcx`, `rdx`, `rsi`, `rdi`, `rbp`, `r8`–`r15` | `xmm0`–`xmm15` | i32 → 32비트 서브레지스터, i64/포인터 → 64비트 |
| **AArch64** | `x0`–`x28` | `v0`–`v31` | i32→`w`, i64→`x`, f16→`h`, f32→`s`, f64→`d`, f128/벡터→`q` |

GPR은 항상 64비트 표기로 적으며, 백엔드가 각 값의 타입에 맞는 서브레지스터로 좁힙니다. AArch64의 벡터 레지스터는 `v0`–`v31`로 적고, 백엔드가 타입에 따라 `H`/`S`/`D`/`Q` 형태를 고릅니다.

### 제약

- **예약 레지스터**: 스택 포인터는 두 표 어디에도 없습니다(x86-64의 `rsp`, AArch64의 `sp`/`x31`). AArch64의 `x29`/`x30`(FP/LR)도 마찬가지입니다. spec에서 이들을 지목하면 그냥 건너뛰고, 해당 값은 다음 유효한 위치로 갑니다.
- **프레임 포인터**: x86-64의 `rbp`는 정당한 callee-saved 레지스터이므로 선택**할 수 있습니다**. 다만 인자 레지스터로 쓰는 것이 온전한 경우는 `-fomit-frame-pointer` 아래뿐입니다. 사용은 각자의 책임입니다.
- **Callee-saved**: 기본값은 표준 ABI 집합입니다. `csr:r12,r13`은 사용자 정의 집합을 선언하며, 호출자는 이에 대응하는 보존 레지스터 마스크를 만들어 어떤 레지스터가 호출을 넘어 살아남는지 파악합니다. x86-64와 AArch64 모두 지원합니다.
- **csr 충돌**: 어떤 레지스터가 `csr`과 인자/반환 목록에 동시에 나타나면 플러그인이 경고합니다. callee가 그것을 복원해 값 전달 역할을 망가뜨리기 때문입니다. 컴파일 자체는 성공합니다.
- **가변 인자 함수**: 지원하지 않습니다. 가변 부분을 조용히 잘못 전달하는 대신 두 백엔드 모두 명확한 진단을 냅니다.
- **간접 호출**: 함수 포인터 호출은 사용자 정의 규약을 실어 나를 수 없습니다. 사용자 정의 규약 함수의 주소가 취해지면 플러그인이 경고하며, 간접 호출은 표준 규약으로 폴백합니다.
- **꼬리 호출**: 호출의 어느 한쪽이라도 사용자 정의 규약을 쓰면 두 백엔드 모두에서 비활성화됩니다.
- **plan이 다루지 않는 값**: plan이 덮지 않은 인자나 반환값은 타깃의 표준 규약(x86-64는 SysV, AArch64는 AAPCS)으로 폴백합니다.

## 사용법

### 1. 플러그인 주도(권장)

참조 플러그인 [`CustomCallConvPlugin.c`]는 `pluginsdk/examples/`에 있습니다. `neverc.ir.pass.post_opt` 단계에 모듈 수준 IR 패스를 등록합니다.

**플러그인 빌드:**

```bash
cd pluginsdk/examples && make CustomCallConvPlugin.dylib   # 또는 .so / .dll
```

**속성 모드**(기본) — `custom_attr` 소스 주석이 달린 함수만 영향을 받습니다.

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib input.c -o output.o
```

**전역 모드** — 정의된 모든 함수에 하나의 spec을 적용합니다(`cc-all`을 명시해야 합니다).

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib \
       -fplugin-arg=org.neverc.example.custom-callconv:cc-all \
       -fplugin-arg=org.neverc.example.custom-callconv:ccspec="gpr:r10,r11,rsi;ret:rdx" \
       input.c -o output.o
```

**이름 접두사로 필터링:**

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib \
       -fplugin-arg=org.neverc.example.custom-callconv:cc-all \
       -fplugin-arg=org.neverc.example.custom-callconv:ccprefix=secret_ \
       -fplugin-arg=org.neverc.example.custom-callconv:ccspec="gpr:r9,r8;ret:rax" \
       input.c -o output.o
```

**다양화** — 내장된 네 가지 배치를 번갈아 적용해 함수들이 같은 배치를 공유하지 않게 합니다(역공학 대응).

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib \
       -fplugin-arg=org.neverc.example.custom-callconv:cc-all \
       -fplugin-arg=org.neverc.example.custom-callconv:ccshuffle \
       input.c -o output.o
```

플러그인이 등록하는 옵션은 네 가지입니다. `cc-all`과 `ccshuffle`(플래그이므로 `=1`이나 `=true`는 생략 가능), 그리고 `ccspec`과 `ccprefix`(문자열 값)입니다. `ccspec`을 주지 않으면 전역 모드는 기본값 `gpr:r10,r11,rsi,rdi;ret:rdx`를 씁니다.

### 2. 소스 수준 속성

`custom_attr` 속성으로 C 소스에서 직접 함수에 주석을 답니다. GNU와 Microsoft 문법을 모두 지원합니다.

```c
// GNU 문법
__attribute__((custom_attr("neverc-callconv", "gpr:r10,r11,rsi;ret:rdx")))
int add3(int a, int b, int c) { return a + b + c; }

// Microsoft 문법
__declspec(custom_attr("neverc-callconv", "gpr:r10;ret:rdx"))
int msfunc(int a) { return a; }
```

`custom_attr("key", "value")`는 깔끔한 함수 문자열 속성(`"key"="value"`)을 만들며 경고를 **내지 않고** `llvm.global.annotations`에도 **들어가지 않습니다**. 이는 **범용** 메커니즘이라 호출 규약뿐 아니라 임의의 키/값 쌍에 쓸 수 있습니다. IR과 MIR 패스는 `F.getFnAttribute("key")`로 되읽습니다.

### 3. 병용

소스 속성과 플러그인 인자는 함께 쓸 수 있습니다. `custom_attr`를 가진 함수는 플러그인의 속성 모드 경로로 처리되고, `cc-all`이 나머지를 덮습니다. 각 함수는 최대 한 번만 처리됩니다.

## 구체화된 plan

spec은 레지스터를 지목할 뿐, 각 값의 각 바이트가 어디에 놓이는지는 말하지 않습니다. 최적화 파이프라인 이후, 코드 생성 이전에 호스트는 `materializeCallingConventionPlans`를 실행해 모든 `CallingConv::NeverC_Custom` 함수를 정확하고 검증된 plan으로 바꿉니다.

- 이미 `"neverc-cc-plan-v1"` 속성을 가진 함수는 **검증만 되고 재생성되지 않습니다**. 스키마 다이제스트, 타깃 ID, 규약 ID가 현재 타깃과 일치해야 합니다.
- `"neverc-callconv"` spec을 가진 함수는 레지스터 이름이 타깃 레지스터 표에 대조되어 해석됩니다. 만들어진 plan이 spec을 대체하고, spec은 IR에서 제거됩니다.
- 둘 다 없지만 해당 타깃이 플러그인 ABI를 통해 호출 규약을 등록한 함수는 그 규약의 `PlanCallingConvention` 콜백이 계획합니다.

모든 직접 호출 지점은 callee의 plan을 물려받으며, 이것이 번역 단위를 넘어 호출자와 피호출자의 배치를 일치시켜 주는 장치입니다. plan은 평평한 문자열입니다.

```
neverc-cc-plan-v1;schema=<다이제스트>;target=<high>:<low>;cc=<high>:<low>;stack=<바이트>;returns=<위치>;arguments=<위치>;callee-saved=<레지스터 번호>
```

각 위치는 `<r|s>,<값 인덱스>,<조각 오프셋>,<크기>,<정렬>,<레지스터 번호>,<스택 오프셋>,<플래그>` 형식이고, 여러 위치는 `|`로 구분합니다. 내장 경로의 스키마 다이제스트는 `llvm-<타깃 트리플>`이며, 플러그인이 등록한 타깃은 자체 다이제스트를 제공합니다.

레지스터 번호는 그것을 정의한 스키마 아래에서만 의미가 있으므로, 불일치는 조용한 오컴파일이 아니라 하드 에러가 됩니다.

| 상황 | 진단 |
|---|---|
| plan 문자열이 파싱되지 않음 | `malformed NeverC calling convention plan` |
| 스키마 다이제스트가 다름 | `NeverC calling convention plan belongs to a foreign target schema` |
| 타깃 ID가 다름 | `NeverC calling convention plan has a foreign target ID` |
| 규약 ID가 다름 | `NeverC calling convention plan has a foreign convention ID` |

바로 이 점 덕분에 plan을 비트코드에 안전하게 심어 LTO를 통과시킬 수 있습니다. 다른 타깃용으로 만들어진 plan이 실수로 적용될 수는 없습니다.

## 플러그인 API

예제 플러그인은 안정된 IR core 테이블만 사용하며, 호출 규약 전용 진입점은 존재하지 않습니다. 함수에 규약을 적용하는 것은 세 번의 호출과 호출 지점 동기화입니다.

```c
NevercIRAttributeHandle Attribute = {0};
Core->CreateStringAttribute(Core->Context, Task, SV("neverc-callconv"), Spec,
                            &Attribute);
Core->AddFunctionAttribute(Core->Context, Task, Function,
                           NEVERC_IR_ATTRIBUTE_LOCATION_FUNCTION, 0, Attribute);
Core->SetFunctionCallingConvention(Core->Context, Task, Function,
                                   NEVERC_IR_CALLING_CONVENTION_NEVER_C_CUSTOM);
```

`NEVERC_IR_CALLING_CONVENTION_NEVER_C_CUSTOM`은 `CallingConv::NeverC_Custom`(LLVM 값 1000)의 ABI 안정 이름입니다. 이어서 플러그인은 `GetValueUseCount` / `GetValueUse`로 그 함수의 사용처를 훑고, `call`, `invoke`, `callbr`의 피호출 피연산자인 사용처마다 `SetInstructionProperty`와 `NEVERC_IR_PROPERTY_CALLING_CONVENTION`으로 명령에 같은 규약을 설정합니다. 그 밖의 사용처는 주소가 새어 나갔다는 뜻이고, 이것이 "주소가 취해졌다"는 경고의 출처입니다.

자체 타깃을 등록하는 플러그인은 대신 `NevercCallingConventionDescriptor`에 `PlanCallingConvention` 콜백을 제공해 spec 계층을 건너뛰고 plan을 직접 만들 수도 있습니다. [타깃, MC, 어셈블리, 오브젝트](../target-mc-object.ko.md#abi와-호출-규약)를 참고하세요.

## 테스트

GoogleTest 스위트는 [`tests/neverc/CustomCallConvTests.cpp`]에 있으며 26개의 테스트를 담고 있습니다. 각 테스트는 예제 플러그인을 빌드하고, 주어진 spec 아래에서 작은 프로그램을 어셈블리로 컴파일한 뒤, 결과 레지스터 또는 스택 배치를 확인합니다.

```bash
ninja -C build-neverc neverc-tests
build-neverc/bin/neverc-tests --gtest_filter='CustomCallConvTest.*'
```

커버리지:

| 범주 | 테스트 수 |
|---|---|
| x86-64 풀 / 위치 / 스택 / 넘침 / i64 / sret / byval / 폴백 | 9 |
| AArch64 GPR / FPR / 스택 / `csr` / 서로 다른 spec 간 호출 | 5 |
| 프런트엔드 `custom_attr`(GNU / `__declspec` / 엔드투엔드) | 3 |
| plan 구체화와 스키마 거부 | 3 |
| 강화(`csr`, 두 타깃의 가변 인자, 간접 호출, `rsp`, csr 충돌) | 6 |

## 아키텍처

```
소스 속성                      플러그인 IR 패스
custom_attr(...)              (neverc.ir.pass.post_opt)
       │                            │
       └─────────────┬──────────────┘
                     ▼
   "neverc-callconv" = spec, CallingConv::NeverC_Custom
   함수와 그 직접 호출 지점에 부여
                     │
                     ▼
   ┌──────────────────────────────────────────┐
   │ materializeCallingConventionPlans        │
   │ (최적화 이후, 코드 생성 이전)            │
   │                                          │
   │  spec       → 이름을 물리 레지스터로     │
   │  플러그인 규약 → PlanCallingConvention   │
   │  기존 plan  → 스키마 / 타깃 검증         │
   └──────────────────────────────────────────┘
                     │
                     ▼
   "neverc-cc-plan-v1" = 검증된 위치 표
   spec은 제거되고, plan은 직접 호출 지점으로 복사
                     │
                     ▼
   ┌──────────────────────────────────────────┐
   │ 백엔드 CCAssignFn (타깃마다 하나)        │
   │  CC_X86_NeverC     / RetCC_X86_NeverC    │
   │  CC_AArch64_NeverC / RetCC_AArch64_NeverC│
   │                                          │
   │  plan을 읽어 → 위치 배정                 │
   │  다루지 않는 값 → 표준 규약              │
   │  꼬리 호출 비활성화                      │
   └──────────────────────────────────────────┘
                     │
                     ▼
   사용자 정의 레지스터 배치를 가진 기계어
```

백엔드 실행기는 **한 번만 구현하는 것**이며, 모든 정책 결정은 플러그인에 있습니다. 새 규약을 추가하는 데 NeverC를 다시 빌드할 일은 결코 없습니다.

위에서 사용한 코어 테이블은 [`PluginIR.h`] 를, `NevercCallingConventionDescriptor` 는 [`PluginTarget.h`] 를, 이 pass 가 붙는 `neverc.ir.pass.post_opt` 페이즈는 [`Schema/PhaseSchema.json`] 을 참조하십시오.

<!-- reference links -->
[`CustomCallConvPlugin.c`]: ../../../pluginsdk/examples/CustomCallConvPlugin.c
[`llvm/include/llvm/CodeGen/NeverCCallConv.h`]: ../../../llvm/include/llvm/CodeGen/NeverCCallConv.h
[`PluginIR.h`]: ../../../neverc/include/neverc/Plugin/PluginIR.h
[`PluginTarget.h`]: ../../../neverc/include/neverc/Plugin/PluginTarget.h
[`Schema/PhaseSchema.json`]: ../../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`tests/neverc/CustomCallConvTests.cpp`]: ../../../tests/neverc/CustomCallConvTests.cpp

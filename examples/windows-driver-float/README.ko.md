# 부동소수점 연산을 사용하는 Windows 커널 드라이버

NeverC로 빌드한 WDM 커널 드라이버로, **커널 모드에서 부동소수점 / SIMD를
안전하게 사용하는 방법**을 보여줍니다. macOS / Linux에서 크로스 컴파일을 지원합니다.

## 빌드

```bash
cd examples/windows-driver-float
make
```

독립 실행형 NeverC 릴리스에서:

```bash
make NEVERC=/path/to/neverc
```

출력은 `FloatDriver.sys`(auto-LTO 최적화)입니다.
기본 빌드에는 디버깅용 `-g`가 포함되어 있습니다. 릴리스 빌드에서는 `-g`를 제거하세요.

---

## 처리해야 할 두 가지 문제

커널 모드 부동소수점에는 두 가지 독립적인 문제가 있습니다:

### 문제 1 — `_fltused` ABI 마커 (컴파일/링크 시점)

MSVC 컴파일러는 번역 단위가 부동소수점 연산을 수행할 때마다 `_fltused`
심볼에 대한 미정의 참조를 생성합니다. 사용자 모드 프로그램에서는
`libcmt.lib`가 이 심볼을 제공하여 링커가 만족하고, 일부 FP 관련 CRT
부분이 가져와집니다.

커널 드라이버는 `libcmt`에 링크되지 **않습니다**(`-nostdlib`와
`-Xlinker --nodefaultlib`를 전달). 따라서 미해결 `_fltused`는 링크 오류를
일으킵니다.

**NeverC의 해결 방법**: `-fms-kernel` 사용 시 LLVM의 X86 백엔드가
`_fltused`를 로컬에서 0으로 정의합니다. 생성된 어셈블리에서 확인할 수 있습니다:

```asm
# 사용자 모드 타겟:
    .globl  _fltused              # 외부 참조 -- libcmt 필요
```

```asm
# -fms-kernel 타겟:
    .globl  _fltused
    .set    _fltused, 0           # 로컬 정의! 외부 심볼 불필요
```

따라서 드라이버에 **`int _fltused = 0;`를 수동으로 작성할 필요가 전혀 없습니다**.

### 문제 2 — 커널은 FP/SIMD 레지스터를 보존하지 않음 (런타임)

Windows 커널은 기본적으로 컨텍스트 스위치 시 x87 / XMM / YMM / ZMM
레지스터를 저장/복원하지 **않습니다**. 드라이버가 임의의 커널 코드에서
이 레지스터들을 건드리면, 해당 CPU에서 실행 중인 사용자 모드 스레드의
SIMD 상태를 조용히 손상시킵니다.

**해결 방법**: 모든 부동소수점 / SIMD 영역을
[`KeSaveExtendedProcessorState`](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kesaveextendedprocessorstate)
와 `KeRestoreExtendedProcessorState`로 감쌉니다:

```c
XSTATE_SAVE save;
NTSTATUS status = KeSaveExtendedProcessorState(XSTATE_MASK_LEGACY, &save);
if (!NT_SUCCESS(status))
    return status;

// ... 여기에 FP / SIMD 코드 ...

KeRestoreExtendedProcessorState(&save);
```

### XSTATE 마스크

| 마스크 | 커버 범위 |
|--------|---------|
| `XSTATE_MASK_LEGACY_FLOATING_POINT` (비트 0) | x87 스택 |
| `XSTATE_MASK_LEGACY_SSE` (비트 1) | XMM0–15 |
| `XSTATE_MASK_LEGACY` | 비트 0 \| 비트 1 (일반 `double` / SSE 코드 대부분 커버) |
| `XSTATE_MASK_GSSE` / AVX (비트 2) | YMM0–15 상위 절반 |
| `XSTATE_MASK_AVX512` | AVX-512 ZMM 레지스터 |

코드가 사용하는 가장 넓은 레지스터에 맞게 OR 결합한 마스크를 전달하세요.

---

## 이 드라이버의 동작

- `\Device\FloatDriver`에 디바이스 오브젝트, `\DosDevices\FloatDriver`에
  심볼릭 링크 생성
- `DriverEntry`에서 `ComputeAreaSafe()`(FP 상태 저장/복원으로 `ComputeArea()`를
  감싼 함수)를 `radius=1.0`과 `radius=5.0`으로 두 번 호출
- `DbgPrint`로 double의 원시 비트를 출력 (`DbgPrint`는 `%f`를 지원하지 않으므로
  `RtlCopyMemory`로 64비트 패턴을 추출)
- `-fms-kernel`을 통해 암시적으로 `_fltused` 정의

## `_fltused` 출력 검증

`-fms-kernel` 사용/미사용 시 컴파일러 출력 비교:

```bash
# 사용자 모드 (libcmt 필요):
neverc --target=x86_64-pc-windows-msvc -S /tmp/foo.c -o - | grep fltused
#  .globl  _fltused

# 커널 (0으로 로컬 정의):
neverc --target=x86_64-pc-windows-msvc -fms-kernel -S /tmp/foo.c -o - | grep fltused
#  .globl  _fltused
#  .set    _fltused, 0
```

## 로드 (Windows 테스트 머신에서)

```cmd
sc create FloatDriver type= kernel binPath= C:\path\to\FloatDriver.sys
sc start FloatDriver
sc stop FloatDriver
sc delete FloatDriver
```

테스트 서명을 활성화하거나 프로덕션용 코드 서명 인증서를 사용하세요.

## 주의 사항

- **`DbgPrint`는 `%f`를 지원하지 않습니다** -- 커널 디버그 출력 루틴은
  부동소수점 포맷팅 기능이 없습니다. double을 고정소수점 정수로 변환하여
  표시하거나 이 예제처럼 원시 비트를 출력하세요.
- **IRQL ≥ DISPATCH_LEVEL에서는 부동소수점을 사용하지 마세요**(절대 필요한 경우 제외).
  `KeSaveExtendedProcessorState` 문서에 IRQL 제약이 명시되어 있습니다.
- **성능**: 상태 저장/복원은 무료가 아닙니다. 핫 경로에서는 FP 작업을
  단일 감싸진 영역으로 일괄 처리하는 것을 고려하세요.

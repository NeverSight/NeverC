# CET 섀도우 스택 지원 Windows 커널 드라이버

NeverC로 빌드한 최소한의 WDM 커널 드라이버로, Intel CET(제어 흐름 강화 기술)
커널 섀도우 스택이 활성화되어 있습니다. macOS / Linux에서 크로스 컴파일을 지원합니다.

## 빌드

```bash
cd examples/windows-driver-cet
make
```

독립 실행형 NeverC 릴리스에서:

```bash
make NEVERC=/path/to/neverc
```

출력은 `CetDriver.sys`(auto-LTO 최적화)입니다.
기본 빌드에는 디버깅용 `-g`가 포함되어 있습니다. **릴리스 빌드에서는 `-g`를 제거**하여
디버그 심볼을 제거하고 바이너리 크기를 줄이세요.

## CET 전용 플래그

| 플래그 | 레이어 | 용도 |
|--------|--------|------|
| `-fcf-protection=return` | 컴파일러 | 섀도우 스택 호환 코드 생성 |
| `-Xlinker --cetcompat` | 링커 | PE에 `IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT` 설정 |

## 수동 빌드 (Make 없이)

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fcf-protection=return \
  -fms-kernel -fms-extensions -fms-compatibility \
  -D_AMD64_ -DNTDDI_VERSION=0x06010000 -D_WIN32_WINNT=0x0601 \
  -Wall -nostdlib -shared \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -Xlinker --cetcompat \
  -lntoskrnl -lhal \
  -o CetDriver.sys driver.c
```

## 기능

- `\Device\CetDriver`에 디바이스 오브젝트 생성
- `\DosDevices\CetDriver`에 심볼릭 링크 생성
- 간접 호출(`ComputeFn` 함수 포인터)을 통해 CET 호환성 검증 — 섀도우 스택이 이러한 호출의 반환 주소를 보호
- `DbgPrint`를 통해 로드/언로드 메시지 출력

---

## CET 기술 상세

CET에는 **두 가지 독립적인 보호 메커니즘**이 있습니다:

### 1. 섀도우 스택 — 후방 에지 보호 (RET)

하드웨어가 CALL/RET를 미러링하는 두 번째 스택(섀도우 스택)을 유지합니다.
**함수 진입점에 특별한 명령어가 필요 없습니다** — 완전히 투명합니다:

```
┌─ CALL target ─────────────────────────────────┐
│                                                │
│  일반 스택:    PUSH return_addr  (RSP)          │
│  섀도우 스택:  PUSH return_addr  (SSP, HW)      │
│                                                │
└────────────────────────────────────────────────┘

┌─ RET ─────────────────────────────────────────┐
│                                                │
│  일반 스택:    POP return_addr_A  (RSP)         │
│  섀도우 스택:  POP return_addr_B  (SSP, HW)     │
│                                                │
│  비교: return_addr_A == return_addr_B ?          │
│    ✓ 일치   → 정상 반환                         │
│    ✗ 불일치 → #CP 예외 (BUGCHECK)               │
│                                                │
└────────────────────────────────────────────────┘
```

섀도우 스택 관리 명령어 (OS가 컨텍스트 스위치에 사용, 함수 헤더에 배치하지 않음):

```asm
RDSSPQ  rax         ; 현재 섀도우 스택 포인터 읽기
INCSSPQ rax         ; SSP 전진 (항목 폐기)
SAVEPREVSSP         ; 이전 섀도우 스택 토큰 저장
RSTORSSP [addr]     ; 저장된 섀도우 스택으로 복원
WRSS  [addr], rax   ; 슈퍼바이저 섀도우 스택에 쓰기
WRUSS [addr], rax   ; 사용자 섀도우 스택에 쓰기 (ring 0만)
SETSSBSY            ; 현재 섀도우 스택을 바쁨으로 설정
CLRSSBSY [addr]     ; 바쁨 플래그 해제
```

### 2. 간접 분기 추적 (IBT) — 전방 에지 보호 (간접 CALL/JMP)

모든 유효한 간접 호출/점프 대상에 `ENDBR64` 명령어(`F3 0F 1E FA`, 4바이트)가 필요합니다.
CET를 지원하지 않는 CPU에서 `ENDBR64`는 NOP으로 동작합니다.

```
┌─ 간접 CALL/JMP ──────────────────────────────┐
│                                               │
│  CPU 내부 TRACKER = WAIT_FOR_ENDBR 설정       │
│  대상 주소로 점프...                            │
│                                               │
│  대상의 첫 명령어가 ENDBR64 ?                   │
│    ✓ 예 → TRACKER 해제, 정상 실행               │
│    ✗ 아니오 → #CP 예외                         │
│                                               │
│  직접 CALL/JMP는 TRACKER를 설정하지 않음        │
│                                               │
└───────────────────────────────────────────────┘
```

### Windows 커널의 선택

| 보호 | 메커니즘 | Windows 커널 사용 여부 |
|------|---------|----------------------|
| 후방 에지 (RET) | CET 섀도우 스택 | **예** (KCET) |
| 전방 에지 (간접 CALL/JMP) | CET IBT (ENDBR) | **아니오** — CFG로 대체 |

따라서 기본값은 `-fcf-protection=return`: 섀도우 스택만, ENDBR64 미생성.
ENDBR64가 필요한 경우 `-fcf-protection=full`로 변경하세요 (Windows에서는 무해한 NOP이지만 Linux 이식 시 호환성 제공).

### 어셈블리 비교: `-fcf-protection` 각 모드

소스 코드:

```c
unsigned long rotate13(unsigned long val) {
    return (val << 13) | (val >> 19);
}
```

#### `-fcf-protection=none` (CET 없음)

```asm
rotate13:
    mov  eax, ecx
    rol  eax, 13
    ret
```

#### `-fcf-protection=return` (섀도우 스택만 — 이 예제에서 사용)

```asm
rotate13:
    mov  eax, ecx      ; "none"과 완전히 동일!
    rol  eax, 13        ; 섀도우 스택은 완전히 투명 —
    ret                 ; 하드웨어가 CALL/RET 시 자동으로 동작
```

코드 생성은 `none`과 **완전히 동일**합니다. 유일한 차이는 링커 플래그 `--cetcompat`가
PE 디버그 디렉터리에 `IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT` 비트를 설정하여
이 바이너리가 섀도우 스택과 호환됨을 Windows에 알리는 것입니다.

#### `-fcf-protection=full` (섀도우 스택 + IBT)

```asm
rotate13:
    endbr64             ; ← IBT 마커 (F3 0F 1E FA)
    mov  eax, ecx       ;    비CET CPU에서는 NOP
    rol  eax, 13        ;    Windows에서 미사용 (CFG가 전방 에지 처리)
    ret
```

`ENDBR64`가 모든 함수 진입점에 나타납니다. Windows에서는 함수당 4바이트의 낭비이지만 문제를 일으키지 않습니다.

---

## 대상 머신에서 KCET 활성화

```cmd
reg add HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity /v Enabled /t REG_DWORD /d 1 /f
reg add HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\KernelShadowStacks /v Enabled /t REG_DWORD /d 1 /f
```

재부팅이 필요합니다. `msinfo32.exe` → "커널 모드 하드웨어 강제 스택 보호"로 확인하세요.

**요구 사항:**

- 대상 머신에서 HVCI 활성화
- Windows 빌드 21389 이상
- CET 지원 CPU (Intel Tiger Lake+ / AMD Zen 3+)

## 로드

```cmd
sc create CetDriver type= kernel binPath= C:\path\to\CetDriver.sys
sc start CetDriver
sc stop CetDriver
sc delete CetDriver
```

테스트 서명을 활성화하거나 프로덕션용 코드 서명 인증서를 사용하세요.

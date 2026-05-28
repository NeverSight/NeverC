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

## 컴파일러 vs bin2bin: 누가 CET에 친화적인가?

CET는 **소스 레벨 컴파일러**와 **bin2bin 도구**(패커, 난독화기, 후커,
dump+rebuild) 사이에 명확한 경계선을 그립니다. 하드웨어 Shadow Stack은
세 가지 규칙을 강제하여 전체 보호 / 난독화 산업을 재편합니다:

> 1. **반환 주소를 수정하지 말 것.**
> 2. **코드를 자체 패치하지 말 것**(HVCI가 코드 페이지에 W^X 강제).
> 3. **규칙 1, 2를 존중하는 강력한 난독화 변환을 찾을 것.**

### 컴파일러가 "반환 주소를 암호화"할 수 있는가?

**아니오.** 이는 흔한 오해입니다. Shadow Stack은 OS가 아니라 CPU에 의해
강제되며 사용자 모드 코드에는 보이지 않습니다. 함수 에필로그에서 일반
스택의 반환 주소를 XOR 암호화해도:

```c
void my_func() {
    // ... 함수 본문 ...
    // 에필로그가 반환 주소를 암호화하려고 시도:
    // XOR [rsp], 0xDEADBEEF
    // RET           <- 하드웨어가 일반 스택 vs 섀도우 스택 비교
                     //   더 이상 일치하지 않음 -> #CP 예외 -> BUGCHECK
}
```

섀도우 스택은 원래 반환 주소를 그대로 유지합니다. RET가 하드웨어 비교를
트리거하고, 불일치 시 `#CP`가 발생하여 커널이 BUGCHECK됩니다. 컴파일러는
**섀도우 스택에 도달할 수 없습니다**:

- 사용자 모드: 섀도우 스택에 쓸 수 있는 명령어 없음
- 커널 모드: `WRSSQ`는 특권 명령어, `ntoskrnl`만 사용

### 컴파일러가 할 수 있는 CET 친화적 난독화

| 변환 | 왜 CET 안전한가 |
|------|--------------|
| **제어 흐름 평탄화** | switch 디스패처는 직접 CALL/JMP 사용; cases에 필요시 ENDBR64 |
| **VM 기반 가상화** | 핸들러는 간접 JMP(ENDBR64 포함)로 연결, push+ret 미사용 |
| **문자열 / 상수 암호화** | 순수 데이터 변환, 제어 흐름에 영향 없음 |
| **MBA 표현식** | `x + y → (x ^ y) + 2*(x & y)` — 데이터만 |
| **불투명 조건자** | 직접 점프를 통한 조건 분기 |
| **함수 복제 / 인라인** | 호출 스택 시맨틱 변경 없음 |
| **명령어 치환** | `MOV → XOR + ADD` — 스택 효과 없음 |

### CET 적대적 패턴 (KCET에서 죽음)

| 패턴 | 왜 깨지는가 |
|------|----------|
| **반환 주소 암호화** | 섀도우 스택 불일치 → `#CP` |
| **PUSH addr; RET 디스패처** (고전적인 VMProtect / Themida 스타일) | 동일 — 섀도우 스택에 `addr` 항목 없음 |
| **스택 피보팅** (ROP 가젯 체인) | 섀도우 스택이 피봇을 따라갈 수 없음 |
| **자가 수정 코드** | HVCI가 실행 가능 페이지로의 쓰기 차단 |
| **런타임 코드 생성** | 동일 — HVCI W^X 위반 |
| **트램폴린 기반 인라인 후크** | 함수 프롤로그 수정이 HVCI 트리거; HVCI 우회해도 트램폴린 RET에서 섀도우 스택 깨짐 |

### bin2bin 도구가 구조적으로 불리한 이유

컴파일러는 시맨틱 IR에서 CET 정확한 코드를 생성합니다. bin2bin 도구는
컴파일된 바이트에서 시맨틱을 **재발견**해야 합니다:

1. **명령어 경계 모호성** — x86은 가변 길이. 잘못된 오프셋에 ENDBR64(4바이트) 추가는 모든 RIP 상대 주소 지정과 재배치를 깨뜨림.
2. **간접 대상 식별** — bin2bin은 `.data` 내 어떤 주소가 점프 테이블 항목이고 어떤 것이 원시 데이터인지 항상 구분할 수 없음. 과도 표시(코드 비대화, 새 ROP 가젯 시드)하거나 표시 부족(런타임 `#CP`).
3. **자기 증명의 위험** — `IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT` 설정은 약속. bin2bin 출력이 CET 적대 패턴을 포함하면 드라이버는 비 CET 머신에서 잘 로드되지만 KCET 호스트에서 즉시 BSOD.
4. **CFG 완전성** — 컴파일러는 전체 호출 그래프를 봄; bin2bin은 추론해야 하고, 정확한 대상 없는 간접 호출은 보수적 ENDBR 배치를 강요.

### 업계 현황

| 도구 / 분류 | CET 상태 |
|------------|---------|
| **NeverC / Clang / MSVC (컴파일러)** | `-fcf-protection` + 링커 플래그로 기본적으로 CET 친화적 |
| **OLLVM / Tigress / NeverC 패스** | IR 레벨 변환 → 자연스럽게 CET 안전 |
| **Microsoft Detours (4.0+)** | CET 호환으로 업데이트됨 |
| **VMProtect / Themida (구버전)** | Push+RET 디스패처가 KCET 호스트의 드라이버를 죽임 |
| **VMProtect / Themida (신버전)** | ENDBR 인식 디스패처 추가 중, 혼합 지원 |
| **Manual map / dump+rebuild 로더** | 모든 ENDBR 마커 재구성 필요 — 오류 발생 쉬움 |

### 게임 보안 관점

안티 치트 드라이버(EAC, BattlEye, FACEIT AC, Vanguard)는 출시 시
`--cetcompat`이 설정되어 있어 KCET 활성화 머신에서 깔끔하게 실행됩니다.
치트 드라이버 — 일반적으로 bin2bin 도구로 패킹, 후킹 또는 트램폴린 주입
— 는 CET 준수를 유지하기 어렵습니다. KCET + HVCI는 **"컴파일러 친화적,
bin2bin 적대적"인 하드웨어 벽**을 형성하여 잘 설계된 보안 소프트웨어에
악성코드 스타일 코드에 비해 비대칭적인 이점을 제공합니다.

이것이 Microsoft가 커널 소프트웨어에 대해 KCET를 강하게 추진하는 더 깊은
이유입니다: 합법적인 커널 코드를 더 쉽게 강화하면서 공격자 기술을 점진적으로
더 어렵게 만듭니다.

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

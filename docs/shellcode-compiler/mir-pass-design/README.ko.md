**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode 컴파일러](../README.ko.md)

# MIR Pass 설계 — 원칙과 훅 포인트

> [ir-pass-design.md](../ir-pass-design/README.ko.md)의 동반 문서. IR 계층은 IR 수준에서 명백히 재배치를 생성하는 구조를 제거합니다. MIR 계층은 명령 선택과 레지스터 할당 후의 **캐치올**로, 코드 생성이 도입한 의사/메타데이터 명령을 제거하고 서드파티 난독화 pass가 최종 명령 수준 변환을 수행할 훅 포인트를 노출합니다.
>
> 구현: `neverc/lib/Shellcode/MIR/MIRPrepPass.cpp` + `Pipeline.cpp`.
> 훅 인터페이스: `neverc/include/neverc/Shellcode/Pipeline/Pipeline.h`.

---

## 0. 왜 MIR 계층이 필요한가

IR 계층이 이미 제거한 것:
- 상수 GV → 스택화 / 즉시값 (Data2TextPass)
- `memcpy` / `memset` / `str*` / `abs*` → 인라인 바이트 루프 (MemIntrinPass)
- `__int128` compiler-rt 헬퍼 → 인라인 always_inline (CompilerRtPass)
- extern libc syscall → 인라인 svc / syscall (SyscallStubPass)
- Win32 extern → PEB 워크 + 익스포트 해시 (WinPEBImportPass)
- 변경 가능 전역 변수 → 진입 스택 프레임 (ZeroRelocPass)
- 계산 점프 → switch (IndirectBrPass)

하지만 LLVM 백엔드는 **IR → MIR 하강** 중 shellcode가 수용할 수 없는 추가 구조를 도입합니다:

1. **CFI / EH_LABEL 의사 명령**: `-g`나 기본 언와인드 정보 활성화 시 생성.
2. **XRay / 패치 가능 함수 스텁**.
3. **Sanitizer 메타데이터**: StackMap / PatchPoint / PseudoProbe.
4. **백엔드 MC 수준 픽스업**.

MIR 훅의 핵심 목적: **서드파티 명령 수준 난독화 활성화** (명령 치환, 레지스터 리네이밍). IR에서는 불가능 (가상 레지스터와 추상 명령만 있음).

---

## 1. LLVM과의 통합 (네이티브 훅)

LLVM의 `TargetPassConfig`에 전역 콜백 리스트가 있습니다. `Pipeline.cpp`에서 등록:

```cpp
ListRegisterTargetPassConfigCallbacks.push_back(
    [](TargetPassConfig &TPC) {
      const ShellcodeOptions &Opts = currentShellcodeOptionsStorage();
      const ObfuscationHooks &H = getShellcodeObfuscationHooks();
      runMIRHook(H.RunBeforePreEmit, TPC, Opts);
      TPC.addExternalPass(createShellcodeMIRPrepPass(Opts));
      runMIRHook(H.RunAfterPreEmit, TPC, Opts);
    });
```

---

## 2. 내장 MIRPrepPass

크로스 플랫폼, 단일 책임: 각 `MachineBasicBlock`을 스캔하여 3가지 범주의 의사 명령을 삭제합니다. 실제 기계 명령은 **절대 건드리지 않습니다**.

### 2.1 부속 섹션 메타데이터 (`TargetOpcode::*`, 크로스 플랫폼)

| 연산 코드 | 소스 | 미제거 시 결과 |
|-----------|------|------------|
| `CFI_INSTRUCTION` | 모든 플랫폼의 프레임 하강 / `-g` | `.eh_frame` / `__compact_unwind` / `.pdata` 비어있지 않음 |
| `EH_LABEL` | EH / try-catch setjmp 포인트 | LSDA 부속 섹션 비어있지 않음 |
| `STATEPOINT` / `STACKMAP` / `PATCHPOINT` | GC / 샌드박스 stackmap | `.llvm_stackmaps` |
| `PSEUDO_PROBE` | `-fprofile-sample-use` | `.pseudo_probe` |
| `PATCHABLE_*` 패밀리 | XRay / Kcov 스텁 | `.xray_instr_map` |
| `FENTRY_CALL` | `-mfentry` 진입 프로브 | extern `__fentry__` 호출 |
| `LOCAL_ESCAPE` | Microsoft SEH | SEH 핸들러 참조 유입 |

### 2.2 Windows SEH (`TargetInstrInfo::getName()` 접두사 매칭)

```cpp
StringRef Name = TII->getName(Opcode);
if (Name.starts_with("SEH_"))
  eraseFromParent();
```

### 2.3 명령 리라이트 테이블 (`MIRRewritePatterns.def`)

등록된 2개 패턴:

1. **`aarch64-cpi-fp-to-fmov-imm`**: `ADRP + LDRSui/LDRDui [base, #:lo12:CPI]` → `FMOV Sd/Dd, #imm8`.
2. **`x86-cpi-zero-fp-to-xorps`**: `movss/movsd xmm, [rip+CPI]` (+0.0) → `FsFLD0SS/FsFLD0SD`.

---

## 3. 사용자 난독화 훅

`ObfuscationHooks`는 **11개 훅 포인트**를 노출합니다: 6 IR + 3 MIR + 2 바이트 수준.

- `RunBeforePreEmit`: MIR에 **CFI/EH 의사 명령이 아직 있음** — 프롤로그/에필로그 메타데이터 조작용.
- `RunAfterPreEmit`: **정리된 MIR** — AsmPrinter에 가장 가까움, 명령 치환 / 레지스터 리네이밍에 적합.
- `RunPostExtract`: **순수 바이트 스트림** — XOR/RC4 래핑, 정크 바이트, 커스텀 헤더용.

---

## 4. 완전 실행 순서

```
[IR PassBuilder]
  ├─ RunBeforePrep → ZeroRelocPass(Prep) → RunAfterPrep
  ├─ IndirectBrPass / MemIntrinPass / CompilerRtPass
  ├─ SyscallStubPass / WinPEBImportPass / KernelImportPass
  ├─ Data2TextPass #1 → RunBeforeInlining
  │  (LLVM: AlwaysInliner / SROA / SLP)
  ├─ RunAfterInlining → Data2TextPass #2 / ZeroReloc(Stackify)
  ├─ RunAfterStackify → AllBlrPass(opt)
[Codegen]
  ├─ RunBeforePreEmit → ShellcodeMIRPrepPass → RunAfterPreEmit
[AsmPrinter → .o]
[ShellcodeExtractor]
  ├─ RunPostExtract → flat .bin
```

## 5. 설계 근거

| 문제 | IR 계층? | MIR 계층? |
|------|---------|---------|
| 상수 GV 제거 | 예 | 불필요 |
| extern libc 제거 | 예 | 불필요 |
| CFI 의사 명령 | 아니오 | 예 (스캔 후 삭제) |
| 명령 수준 난독화 | 아니오 | 예 (실제 레지스터/MI 있음) |
| 레지스터 리네이밍 | 아니오 | 예 |

## 6. 확장 가이드

- **내장 의사 명령 제거 추가**: `isShellcodeStripPseudo` switch에 1 case 추가.
- **내장 MIR 리라이트 추가**: `tryRewriteXxx` 작성 후 `MIRRewritePatterns.def` + `MIRRewriteOpcodes.def`에 추가.
- **서드파티 난독화**: [Plugin API](../../plugin-api/README.ko.md) (`NEVERC_HOOK_SC_*` 훅)로 등록.

## 7. ShellcodeExtractor와의 관계

| 계층 | 타이밍 | 능력 |
|------|--------|------|
| MIR | AsmPrinter **전** | MachineInstr 삽입/삭제 가능 |
| 추출기 | AsmPrinter **후** | 바이트 수정 또는 거부만 가능 |

**원칙**: MIR에서 먼저 수정 (아직 명령 조작 가능); 바이트 수준 패치만 추출기로 폴백.

## 8. 능동 수정 vs 진단 패스스루

1. **능동 수정**: MachineInstr 직접 변경. 저비용, 타겟 독립.
2. **진단 패스스루**: 문제 감지 → MIR 수준 오류 보고 → 추출기에서 바이트 수준 거부.
3. **추출기 폴백**: 남은 외부 reloc 또는 비어있지 않은 데이터 섹션에서 하드 실패.

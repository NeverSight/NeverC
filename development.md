# X86 Privileged Intrinsics — InlineAsm to LLVM Intrinsics Migration

## Overview

All x86 privileged builtins in `BuiltinEmitterX86.cpp` have been migrated from
InlineAsm emission to proper LLVM Intrinsic calls. This gives the backend full
visibility into the instructions for scheduling, register allocation, and
optimization, whereas InlineAsm was an opaque black box.

**Result:** 0 InlineAsm (`#APP`/`#NOAPP`) markers remain in the emitter.

## Architecture

```
Frontend (BuiltinEmitterX86.cpp)
  └─ Builder.CreateIntrinsic(Intrinsic::x86_*)
       └─ LLVM IR:  call void @llvm.x86.<name>(...)
            └─ SelectionDAG (X86ISelLowering.cpp)
                 └─ LowerINTRINSIC_W_CHAIN / INTRINSIC_VOID
                      └─ DAG.getMachineNode(X86::<MCINST>, ...)
                           └─ MachineInstr → asm
```

### Key files

| File | Role |
|------|------|
| `llvm/include/llvm/IR/IntrinsicEnums.inc` | Total intrinsic count (`num_intrinsics`) |
| `llvm/include/llvm/IR/IntrinsicImpl.inc` | Name table, IIT type encoding, attribute map |
| `llvm/include/llvm/IR/IntrinsicsX86.h` | X86 intrinsic enum (sorted alphabetically) |
| `llvm/lib/Target/X86/X86ISelLowering.cpp` | SelectionDAG lowering for each intrinsic |
| `llvm/lib/Target/X86/X86GenDAGISel.inc` | ISel pattern-matching table (hardcoded IDs) |
| `llvm/lib/Target/X86/X86RegisterInfo.cpp` | CR/DR register reservation (prevents DCE) |
| `llvm/lib/Target/X86/X86InstrInfo.cpp` | Physical register copy support |
| `neverc/lib/Emit/Builtin/BuiltinEmitterX86.cpp` | Frontend intrinsic emission |

## Critical Invariant: Enum ID ↔ ISel Table Synchronization

**This is the single most important thing to understand when modifying x86
intrinsics.**

`IntrinsicsX86.h` defines a C++ enum with implicit sequential numbering:

```cpp
enum X86Intrinsics : unsigned {
    x86_3dnow_pavgusb = 1803,   // ID = 1803
    x86_3dnow_pf2id,            // ID = 1804
    ...
    x86_sse2_mfence,            // ID = 1803 + N
    ...
};
```

`X86GenDAGISel.inc` contains the ISel pattern-matching table with **hardcoded
intrinsic ID values** encoded as VBR integers:

```
OPC_CheckChild1Integer, 10|128,47,   // checks intrinsic ID == 3013
```

**If you insert entries in the middle of the sorted enum, all subsequent IDs
shift, and every ISel pattern referencing those IDs silently breaks.** The
symptom is `LLVM ERROR: Cannot select: intrinsic %llvm.x86.<name>` — the ISel
table can't find the pattern because the ID it checks no longer matches.

### Rules

1. **After adding/removing/reordering entries in `IntrinsicsX86.h`, you MUST
   update `X86GenDAGISel.inc`** to match the new enum values.

2. All four tables in `IntrinsicImpl.inc` are flat arrays indexed by intrinsic
   ID. The name-to-ID lookup (`lookupIntrinsicID`) computes the ID directly
   from the array position. There is no indirection — **the enum value IS the
   array index**.

3. The name table in `IntrinsicImpl.inc` must be sorted alphabetically (binary
   search). The enum in `IntrinsicsX86.h` must match this order. Use
   `scripts/sort-x86-intrinsics.py --write` after adding entries.

4. `num_intrinsics` in `IntrinsicEnums.inc` must equal the total count across
   all targets. The x86 count is tracked in `IntrinsicImpl.inc` (currently
   1487).

### How to update X86GenDAGISel.inc after enum changes

The `OPC_CheckChild1Integer` directives use VBR encoding of `intrinsic_id * 2`.
A 2-byte VBR `A|128,B` decodes to `A + B*128`, and the intrinsic ID is
`decoded / 2`.

To remap after inserting N entries at various sorted positions:

1. Build old→new ID mapping by comparing the old and new sorted enum lists
2. For each `OPC_CheckChild1Integer` in the file, decode the VBR value
3. If `decoded/2` matches an old x86 intrinsic ID, replace with `new_id * 2`
4. Re-encode as VBR

A reference script was used for this: read the old enum from
`git show <pre-change-commit>^:IntrinsicsX86.h`, diff against current, compute
shifts, and patch all 115 affected entries.

## Intrinsic Lowering Patterns

### Simple void instructions (cli, sti, wbinvd, vmxoff, xend)

```cpp
SDNode *N = DAG.getMachineNode(X86::CLI, dl, MVT::Other, Chain);
return SDValue(N, 0);
```

### Memory-operand instructions (sidt, lidt, sgdt, lgdt, invlpg, vmptrst)

```cpp
SDValue Scale = DAG.getTargetConstant(1, dl, MVT::i8);
SDValue Index = DAG.getRegister(0, MVT::i64);
SDValue Disp  = DAG.getTargetConstant(0, dl, MVT::i32);
SDValue Seg   = DAG.getRegister(0, MVT::i16);
SDNode *N = DAG.getMachineNode(Opc, dl, MVT::Other,
    {Ptr, Scale, Index, Disp, Seg, Chain});
```

### Register + memory (invpcid)

INVPCID64 takes a GR64 type selector + m128 descriptor. The type arg from the
intrinsic is i32, so it must be zero-extended to i64:

```cpp
SDValue Type = DAG.getNode(ISD::ZERO_EXTEND, dl, MVT::i64, Op.getOperand(2));
```

### Implicit-use instructions via CopyToReg + Glue (wrmsr, xsetbv, cpuid)

When a machine instruction uses implicit physical registers (e.g., XSETBV reads
ECX/EAX/EDX), the pattern is:

```cpp
SDValue Glue;
Chain = DAG.getCopyToReg(Chain, dl, X86::ECX, XCR, Glue);
Glue = Chain.getValue(1);
Chain = DAG.getCopyToReg(Chain, dl, X86::EAX, Lo, Glue);
Glue = Chain.getValue(1);
Chain = DAG.getCopyToReg(Chain, dl, X86::EDX, Hi, Glue);
Glue = Chain.getValue(1);
SDVTList Tys = DAG.getVTList(MVT::Other, MVT::Glue);
SDNode *N = DAG.getMachineNode(X86::XSETBV, dl, Tys, {Chain, Glue});
```

The Glue chain prevents the scheduler from reordering the CopyToReg nodes past
the instruction that consumes them.

### CR/DR register ops

CR and DR registers must be **reserved** in `X86RegisterInfo.cpp` to prevent
the register allocator from treating writes as dead:

```cpp
for (MCPhysReg Reg : X86::CONTROL_REGRegClass)
    Reserved.set(Reg);
for (MCPhysReg Reg : X86::DEBUG_REGRegClass)
    Reserved.set(Reg);
```

Without this, `mov cr3, rcx` gets DCE'd because the allocator sees no
downstream use of CR3.

### EFLAGS (pushfq / popfq)

Uses `RDFLAGS64` / `WRFLAGS64` pseudo-instructions (not a direct passthrough):

```cpp
SDNode *N = DAG.getMachineNode(X86::RDFLAGS64, dl, VTs, Chain);
```

A bare `return Op` passthrough causes "Cannot select" because there is no ISel
pattern for the raw intrinsic node.

### VMX status encoding

VMX operations (vmlaunch, vmresume, vmwrite, etc.) report success/failure via
EFLAGS. The lowering emits `sete` + `setb` after the VMX instruction and
encodes the result as: 0 = success, 1 = fail (ZF), 2 = fail (CF).

### TSX (xbegin, xabort, xend, xtest)

- **xbegin**: XBEGIN pseudo has `UsesCustomInserter` flag. The custom inserter
  (already in `EmitInstrWithCustomInserter`) handles the multi-BB expansion.
  Lowering emits `getMachineNode(X86::XBEGIN, ...)` with 1 def + 1 operand.
  The frontend passes `-1` as the fallback EAX value. Since the frontend
  declares it as void return, only the chain is returned from lowering.

- **xabort**: Takes a single i8 immediate operand.

### MCInstrDesc NumDefs

If an instruction's `NumDefs` field in `X86GenInstrInfo.inc` is wrong, it
causes segfaults during instruction emission. Example: VMWRITE64rr had
`NumDefs=1` (wrong — vmwrite has no register output). Fixed to `NumDefs=0`.

## REP Pseudo-Instructions (Port I/O String Ops)

Port I/O string operations (`__inbytestring`, `__outbytestring`, etc.) are
implemented via REP pseudo-instructions: `REP_INSB_64`, `REP_INSW_64`,
`REP_INSL_64`, `REP_OUTSB_64`, `REP_OUTSW_64`, `REP_OUTSL_64`.

### Checklist for Adding REP Pseudo-Instructions

Adding a REP pseudo-instruction requires synchronized changes across the
following files. **Missing any one of these causes compile failures or runtime
segfaults:**

| # | File | Change |
|---|------|--------|
| 1 | `X86GenInstrInfo.inc` | Add enum opcode + update `INSTRUCTION_LIST_END` |
| 2 | `X86GenInstrInfo.inc` | Update `Insts[]` array size (must match `INSTRUCTION_LIST_END`) |
| 3 | `X86GenInstrInfo.inc` | ImplicitOps array: add implicit register uses/defs |
| 4 | `X86GenInstrInfo.inc` | MCInstrDesc table: one entry per instruction, inserted at top in **reverse order** |
| 5 | `X86GenInstrInfo.inc` | InstrNameData / InstrNameIndices / InitMCInstrInfo count |
| 6 | `X86GenInstrInfo.inc` | Sched/Offsets / Feature bits / assert update |
| 7 | `X86GenAsmWriter.inc` | AsmStrs (AT&T syntax strings), OpInfo0, OpInfo1, **OpInfo2** |
| 8 | `X86GenAsmWriter1.inc` | AsmStrs (Intel syntax strings), OpInfo0, OpInfo1 |
| 9 | `X86ISelLowering.cpp` | ISel lowering (CopyToReg + getMachineNode) |

### Key Lessons

1. **TSFlags must correctly encode each instruction's opcode.** Never copy
   TSFlags from an existing pseudo-instruction verbatim — the MCCodeEmitter
   derives machine code bytes from the Opcode field in TSFlags. `INSB` has
   opcode `0x6C`, `MOVSB` has `0xA4`; using the wrong one silently emits
   incorrect bytes. Local builds may not crash, but the output is functionally
   wrong.

   TSFlags encoding formula (see `MCTargetDesc/X86BaseInfo.h`):
   ```
   RawFrm(1) | AdSize64(3<<9) | REP(1<<26) | OpSize(x<<7) | Opcode(op<<31)
   ```
   - Byte variant: OpSize=0
   - Word variant: OpSize=1 (OpSize16)
   - Dword variant: OpSize=2 (OpSize32)

   | Instruction | Opcode | TSFlags |
   |-------------|--------|---------|
   | REP_INSB_64 | 0x6C | `0x3604000601` |
   | REP_INSW_64 | 0x6D | `0x3684000681` |
   | REP_INSL_64 | 0x6D | `0x3684000701` |
   | REP_OUTSB_64 | 0x6E | `0x3704000601` |
   | REP_OUTSW_64 | 0x6F | `0x3784000681` |
   | REP_OUTSL_64 | 0x6F | `0x3784000701` |

2. **Windows `unsigned long` is 32-bit.** The port I/O string intrinsics take
   count as `i32`, but `RCX` is a 64-bit register. ISel must `ZERO_EXTEND`
   count to `i64` before `CopyToReg` to `X86::RCX`, otherwise it triggers
   `Cannot emit physreg copy instruction`.

3. **PGO/LTO builds expose UB.** A local debug build may appear to work
   (wrong operand count in `getMachineNode` happens not to crash), but
   PGO/LTO's different memory layout makes the UB segfault immediately. These
   issues must be caught via Unicorn binary verification — checking that the
   target instruction appears in assembly text is not enough. You must also
   verify:
   - Correct machine code bytes (`f3 6c` = rep insb, not `f3 a4` = rep movsb)
   - Correct register state (DX=port, RDI/RSI=buffer, RCX=count)

## Testing

### GTest suite (`tests/neverc/X86PrivilegedIntrinTests.cpp`)

23 tests that compile C source to assembly and check for expected instruction
mnemonics. Uses `compileToAsm()` helper with `--target=x86_64-pc-windows-msvc
-O2 -S`. Also verifies `#APP` (InlineAsm marker) is absent.

### Unicorn binary verification (`tests/verify-x86-intrin-binary.py`)

81 standalone tests that:

1. Compile to COFF `.o` via `ncc --target=x86_64-pc-windows-msvc -O2 -fno-lto`
2. Extract `.text` bytes via `lief`
3. Disassemble via `capstone` to locate the privileged instruction
4. Emulate up to the privileged instruction via `unicorn`
5. Verify register state matches Intel SDM expectations

Port I/O string ops tests additionally verify three registers (DX=port,
RDI/RSI=buffer, RCX=count) to ensure the ISel CopyToReg chain correctly sets
up the implicit operands.

Key flags: `-fno-lto` is required to get native object code (not bitcode).
Windows x64 ABI: args in RCX, RDX, R8, R9.

Requires: `pip install unicorn capstone lief`

## Converted Builtins (complete list)

| Category | Builtins |
|----------|----------|
| MSR | `__readmsr`, `__writemsr` |
| CR registers | `__readcr0/2/3/4/8`, `__writecr0/3/4/8` |
| DR registers | `__readdr`, `__writedr` |
| Port I/O | `__inbyte/word/dword`, `__outbyte/word/dword` |
| Port I/O strings | `__inbytestring`, `__inwordstring`, `__indwordstring`, `__outbytestring`, `__outwordstring`, `__outdwordstring` |
| Rep strings | `__movsb/w/d/q`, `__stosb/w/d/q` |
| Interrupt | `_disable` (cli), `_enable` (sti), `__int2c` |
| TLB | `__invlpg`, `_invpcid` |
| Descriptor tables | `__sidt`, `__lidt`, `_sgdt`, `_lgdt` |
| Cache | `__wbinvd` |
| VMX | `__vmx_off/vmlaunch/vmresume/vmwrite/vmread/vmclear/vmptrld/vmptrst/on` |
| Segment | `__segmentlimit` (lsl), GS/FS read/write/inc/add |
| CPUID | `__cpuidex` |
| EFLAGS | `__readeflags`, `__writeeflags` |
| XCR | `_xgetbv`, `_xsetbv` |
| TSX | `_xbegin`, `_xend`, `_xabort`, `_xtest` |

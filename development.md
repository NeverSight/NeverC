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
| [`llvm/include/llvm/IR/IntrinsicEnums.inc`](llvm/include/llvm/IR/IntrinsicEnums.inc) | Total intrinsic count (`num_intrinsics`) |
| [`llvm/include/llvm/IR/IntrinsicImpl.inc`](llvm/include/llvm/IR/IntrinsicImpl.inc) | Name table, IIT type encoding, attribute map |
| [`llvm/include/llvm/IR/IntrinsicsX86.h`](llvm/include/llvm/IR/IntrinsicsX86.h) | X86 intrinsic enum (sorted alphabetically) |
| [`llvm/lib/Target/X86/X86ISelLowering.cpp`](llvm/lib/Target/X86/X86ISelLowering.cpp) | SelectionDAG lowering for each intrinsic |
| [`llvm/lib/Target/X86/X86GenDAGISel.inc`](llvm/lib/Target/X86/X86GenDAGISel.inc) | ISel pattern-matching table (hardcoded IDs) |
| [`llvm/lib/Target/X86/X86RegisterInfo.cpp`](llvm/lib/Target/X86/X86RegisterInfo.cpp) | CR/DR register reservation (prevents DCE) |
| [`llvm/lib/Target/X86/X86InstrInfo.cpp`](llvm/lib/Target/X86/X86InstrInfo.cpp) | Physical register copy support |
| [`neverc/lib/Emit/Builtin/BuiltinEmitterX86.cpp`](neverc/lib/Emit/Builtin/BuiltinEmitterX86.cpp) | Frontend intrinsic emission |

## Portable scalar MSR and port I/O

`__builtin_ia32_wrmsr(unsigned, unsigned long long)` and the existing
`__builtin_ia32_rdmsr(unsigned)` use native LLVM operations without platform
headers. The six portable port builtins are `__builtin_ia32_inbyte`, `inword`,
`indword`, `outbyte`, `outword`, and `outdword` (each with that same prefix).
They take an `unsigned short` port; output operations take the value second.
The value widths are 8, 16 and 32 bits. Ordered accesses and unused reads
survive optimization, including constant ports above 255, which require DX.

The privileged intrinsic suite checks all eight operations at O0/O2 in ELF,
COFF and Mach-O, with no inline assembly or platform headers. The binary oracle
also applies the Microsoft-name register/return checks to all eight portable
names: 89 binary cases pass. These transport checks do not authorize or qualify
successful privileged accesses on physical hardware.

## Native MPX bounds state

NeverC exposes 14 C23 builtins for the architectural BND0--BND3 bank. Each
returns `void` and emits a native LLVM intrinsic, without platform headers or
inline assembly. Bank indices and address controls are constant `unsigned`;
addresses and values are `unsigned long long`.

| Builtin suffix after `__builtin_ia32_` | Arguments |
| --- | --- |
| `bnd_branch` | `kind` (0: conditional initialization, 1: preservation) |
| `bndmov_reg` | `bank, source_bank` |
| `bndcl_reg`, `bndcu_reg`, `bndcn_reg` | `bank, value` |
| `bndmov_load`, `bndmov_store`, `bndmov_store_lock` | `bank, offset, address_bits, segment_prefix` |
| `bndcl_address`, `bndcu_address`, `bndcn_address` | `bank, effective_address, address_bits, segment_prefix` |
| `bndmk` | `bank, lower, inclusive_upper, address_bits, segment_prefix` |
| `bndldx`, `bndstx` | `bank, translation_base, pointer_value, address_bits, segment_prefix` |

Bank indices are 0..3. Address bits select a 32- or 64-bit encoding. Segment
prefixes are 0, 0x26, 0x2e, 0x36, 0x3e, 0x64 or 0x65. MPX ignores 67H in long
mode, so a 32-bit encoding still uses the entire 64-bit base and index. BNDMK
uses an inclusive upper address; BNDCN compares against the raw complemented
upper-bound field. Effective-address checks do not read memory or add segment
bases. BNDLDX/STX use base+displacement for table translation and a separate,
unscaled pointer-comparison value. With no encoded base, supply zero, ignoring
the displacement as required by the instruction.

Bounds stay in the ambient CPU bank. Native instructions own enable checks,
table accesses, #BR and partial fault completion; the compiler retains repeated
checks and unknown memory effects. Compiler-created near transfers receive
BND/F2 so they preserve bounds even with BNDPRESERVE=0. Callees and inline
assembly retain their native side effects; arbitrary callees do not gain a
bounds-preserving ABI. XRay sleds fail explicitly because their runtime patch
format cannot accept added prefixes. A binary lifter represents the original
transfer's bank effect with `bnd_branch`: kind 0 emits a forced-width unprefixed
E9 to the next instruction, which initializes all bounds only if MPX is enabled
and BNDPRESERVE is clear. Kind 1 establishes preservation even in a function
with no explicit bounds instruction. Both survive optimization as ordered
effects; the original encoding and fault-order placement remain the lifter's
responsibility. Assembly roundtrips cannot shorten the E9 into a short JMP.

`sync-x86-mpx-registers.py` appends four reserved, nonallocatable physical
registers and a bounds class. `sync-x86-mpx-instructions.py` appends native
instructions and bank-owning pseudos. Existing register/opcode identities remain
stable. The standard native-intrinsic synchronizer owns signatures and immargs;
the generated-table CI job checks all three authorities and drift regressions.

[tests/verify-mpx-encoding.py](tests/verify-mpx-encoding.py) validates independent bytes, full input values,
callee-saved registers and both assembly dialects for 520 combinations at
O0/O2 across ELF, COFF and Mach-O. It accepts NeverC, full LLVM, or NeverC IR
with the full LLVM backend. `--mode=32 --llc=...` checks 268 i386 combinations
against the full LLVM fork; NeverC's x86 backend remains 64-bit only.
These controlled boundaries do not simulate successful MPX operations.

[tests/verify-mpx-state.py](tests/verify-mpx-state.py) supplies a freestanding CPL0 QEMU guest with its own
page tables, XCR0, BNDCFG and exception gates. It executes native enable/disable,
bounds-check, bounds-table hit/miss/fault, repeated-fault and CFG tests. Example:

```sh
python3 tests/verify-mpx-state.py --compiler build-neverc/bin/neverc \
  --qemu qemu-system-x86_64 --clang clang --qemu-mpx-limitations
```

Strict mode is the default. The explicit limitation option skips 180 cases and
executes 340 per optimization level on QEMU 10.2.2. The model incorrectly
truncates 67H memory/table addresses, rejects LOCK BNDMOV memory stores, and
uses decoded memory operands for register checks. Its nonzero BNDCFG directory
base extraction also differs from the SDM; the fixture uses a zero directory
base and does not qualify nonzero bases. Six additional branch-state cases
observe all four banks after original conditional initialization or preservation,
with MPX disabled, enabled/preserving, and enabled/initializing. The limitation
option runs five and skips one: QEMU's E9/direct-CALL translation omits BNDINIT.
Strict branch-state checking rejects the stale bounds. Byte and assembly tests
still require the exact unprefixed E9, not an emulator-specific replacement.
See QEMU's
[`translate.c`](https://github.com/qemu/qemu/blob/v10.2.2/target/i386/tcg/translate.c),
[`decode-new.c.inc`](https://github.com/qemu/qemu/blob/v10.2.2/target/i386/tcg/decode-new.c.inc),
[`mpx_helper.c`](https://github.com/qemu/qemu/blob/v10.2.2/target/i386/tcg/mpx_helper.c),
and [`emit.c.inc`](https://github.com/qemu/qemu/blob/v10.2.2/target/i386/tcg/emit.c.inc).
Skipped cases retain byte/transport coverage, not successful execution coverage.
Physical MPX hardware qualification remains outstanding.

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
   `utils/lint/sort-x86-intrinsics.py --write` after adding entries.
   The sorter moves the overload bitmap with the name, type and attribute
   entries. DAGISel remapping must read its baseline enum and pattern table
   from the same revision, including when that revision was a sync point.
   Both `OPC_CheckChild0Integer` (without chain) and
   `OPC_CheckChild1Integer` (with chain) carry intrinsic IDs. Omitting the
   former left PCLMUL/CRC32 patterns stale after earlier additions. The sorter
   rejects remaps that change VBR byte width because those also require
   regeneration of matcher branch offsets.

4. `num_intrinsics` in `IntrinsicEnums.inc` must equal the total count across
   all targets. The x86 count is tracked in `IntrinsicImpl.inc` (currently
   1580).

### How to update X86GenDAGISel.inc after enum changes

The `OPC_CheckChild0Integer` and `OPC_CheckChild1Integer` directives use VBR
encoding of `intrinsic_id * 2`.
A 2-byte VBR `A|128,B` decodes to `A + B*128`, and the intrinsic ID is
`decoded / 2`.

To remap after inserting N entries at various sorted positions:

1. Build old→new ID mapping by comparing the old and new sorted enum lists
2. For each intrinsic ID matcher in the file, decode the VBR value
3. If `decoded/2` matches an old x86 intrinsic ID, replace with `new_id * 2`
4. Re-encode as VBR

The sorter reads the baseline enum and DAGISel from the same revision,
computes the ID shifts by intrinsic name, and patches the affected matchers.

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

`__builtin_ia32_wrmsrns(unsigned int index, unsigned long long value)` emits
the operand-free nonserializing write through ECX and EDX:EAX. It shares the
native MSR write operand transport while retaining the distinct WRMSRNS opcode.
`__builtin_ia32_clts()` emits the processor-state instruction as a retained
side effect. Neither builtin emits inline assembly.

`python3 tests/verify-msr-completion.py --compiler build-neverc/bin/neverc`
checks the MSR index, both halves of EDX:EAX, read-result reconstruction, and
retention of reads whose result is unused at O0/O2, including WRMSRNS. It uses the same Python
dependencies as the VMX completion test. `--llc /path/to/fork/llc` tests both
LLVM code modes directly; `--backend-llc /path/to/fork/llc` instead compiles
NeverC's actual emitted IR with the full LLVM fork. These are controlled
compiler-transport checks, not privileged execution against hardware MSRs.

`__builtin_ia32_rdmsrlist(const void *indices, void *values, unsigned long long mask)`
and `__builtin_ia32_wrmsrlist(const void *indices, const void *values, unsigned long long mask)`
return `unsigned long long`. Their shared native IR signature is
`i64 (ptr, ptr, i64)`. They pass the two linear table addresses in RSI/RDI and
the full mask in RCX, then return the actual completion RCX. The instruction
owns all table accesses and fault/restart progress. The LLVM fork rejects these
APIs outside 64-bit mode. These fixed signatures are maintained by
[utils/lint/sync-x86-native-intrinsics.py](utils/lint/sync-x86-native-intrinsics.py); the matching implicit register and
memory effects are maintained by [utils/lint/sync-x86-system-effects.py](utils/lint/sync-x86-system-effects.py).

[tests/verify-msr-list-completion.py](tests/verify-msr-list-completion.py) supports the same `--compiler`, `--llc`,
and `--backend-llc` modes. It checks full table pointers, high mask bits, result
transport, live input-mask preservation, nonvolatile RSI/RDI restoration,
unmapped table boundaries, and surrounding memory ordering. It injects both
zero and a diagnostic nonzero RCX value to detect incorrect result transport;
nonzero injection does not model a normal architectural return. Privileged MSR
access and actual partial-progress exception/restart behavior need a suitable
execution environment.

### Port I/O, descriptor tables and processor queries

The LLVM fork accepts NeverC's native `inbyte/inword/indword`,
`outbyte/outword/outdword`, `cli/sti`, `invlpg`, `sgdt/sidt/lgdt/lidt`, and
`cpuidex` IR signatures. Port selectors remain i16, with i8/i16/i32 data.
The descriptor-table APIs retain the instruction-owned access: ten bytes in
64-bit mode, six bytes for the fork's default 32-bit form. INVLPG keeps its
address operand without introducing an ordinary load. CPUID writes its four
i32 results to the output array in EAX/EBX/ECX/EDX slot order.

[utils/lint/sync-x86-system-effects.py](utils/lint/sync-x86-system-effects.py) also maintains these native opcodes'
implicit registers and memory effects. CLI/STI update ambient IF/VIF state;
they preserve arithmetic flags and must not consume an undefined compiler
EFLAGS value. The old descriptions failed machine verification at function
entry even though ordinary assembly emission succeeded.

Run [tests/verify-system-completion.py](tests/verify-system-completion.py) using `--compiler`, `--llc`, or
`--backend-llc` as above. It checks live/dead port reads, full-width port
values, descriptor sizes and unchanged surrounding memory, unmapped access
boundaries, all CPUID outputs and restoration of callee-saved RBX/EBX.
The native completion runners enable LLVM machine verification in NeverC
as well as in the full fork. These checks qualify compiler transport; they
do not execute privileged I/O or change host interrupt/descriptor state.

### Bounded history, translation and GS-state operations

`__builtin_ia32_hreset(unsigned control)` emits HRESET with the complete EAX
input and an immediate zero; the architecture ignores imm8. It requires the
`hreset` target feature, supplied by a function attribute or `-mhreset`.
The standard `_hreset(int)` wrapper in `<x86gprintrin.h>` now calls this native
builtin. Its installed `hresetintrin.h` retains the LLVM source license.

`__builtin_ia32_invlpgb(unsigned long long address_control, unsigned ids,
unsigned count)` transports rAX, EDX and ECX in that order. The control/address
word remains an integer, including its low control bits; it is not dereferenced
by the compiler. `__builtin_ia32_tlbsync()` emits the actual synchronization
instruction. The shared IR names are `llvm.x86.invlpgb` and `llvm.x86.tlbsync`.
The full LLVM fork additionally provides `llvm.x86.invlpgb32(i32, i32, i32)`
for the 32-bit target, rejecting mode mismatches. Its existing INVLPGB machine
descriptions omitted the ECX input; both backends now retain all three inputs.
Address-size overrides remain separate from these default-mode APIs.

`__builtin_ia32_lkgs_reg(unsigned short selector)` and
`__builtin_ia32_lkgs_mem(const void *selector)` map to `llvm.x86.lkgs.reg(i16)`
and `llvm.x86.lkgs.mem(ptr)`. Both require 64-bit mode. The memory form owns
its m16 selector read, including unaligned access and native fault ordering.
Descriptor validation, the GS selector/descriptor-cache update and the
IA32_KERNEL_GS_BASE update remain inside LKGS; the compiler does not synthesize
segment state. The full fork owns its definitions in `X86InstrSystem.td`.
`sync-x86-lkgs-instructions.py` appends the two forms to NeverC's pruned tables,
reusing the explicit-schema table machinery for descriptors, operand shapes,
both printers and both assembler dialects. Existing opcode numbers stay fixed.

All four instructions remain ordered effects and preserve arithmetic flags.
[tests/verify-bounded-system-completion.py](tests/verify-bounded-system-completion.py) supports the usual compiler, full
LLVM and NeverC-to-fork modes. It checks actual input operands, full live input
values, repeated effects, memory ordering, saved registers and unmapped LKGS
selector boundaries at O0/O2. ELF/COFF/Mach-O output round-trips through both
assembly dialects. It executes surrounding scalar code while intercepting
privileged instructions, so it does not qualify successful processor-history
reset, descriptor loading, interprocessor invalidation or synchronization.

### Bounded virtualization and supervisor state

`__builtin_ia32_vmload` and `__builtin_ia32_vmsave` pass a full unsigned i64
physical VMCB address in RAX. `__builtin_ia32_invlpga` takes an unsigned i64
linear address and an unsigned i32 ASID in ECX. These integer operands never
become compiler memory accesses. Matching `llvm.x86.vmload`, `vmsave` and
`invlpga` APIs require 64-bit mode; the full fork also provides `vmload32`,
`vmsave32` and `invlpga32` for i386. These APIs select default address width.

`__builtin_ia32_{vmload,vmsave,invlpga}_addr32` instead selects the long-mode
address-size override, taking an unsigned i32 address. The matching LLVM
`llvm.x86.{vmload,vmsave,invlpga}.addr32` APIs reject non-long mode; the full
fork also supplies `.addr16` with i16 addresses for i386. INVLPGA's ASID
remains a complete i32 ECX value in every form. These interfaces preserve the
native `67` prefix through late MC lowering. Pseudos retain exact AX/EAX/ECX
uses and VMCB memory effects during machine optimization, then emit the real
instruction and explicit address-size prefix in both assembly dialects.
`sync-x86-svm-address-instructions.py` owns the three appended NeverC pseudos;
existing opcode identities remain unchanged.

`__builtin_ia32_{stgi,clgi,swapgs,clac,stac,hlt}` and their matching LLVM APIs
have no explicit operands. SWAPGS requires long mode. CLAC/STAC change AC,
preserving arithmetic flags; STGI/CLGI change GIF. HLT can resume after a
returning interrupt, so its machine description is no longer a CFG terminator
and the IR call has neither `noreturn` nor `willreturn`. All APIs retain
conservative IR memory effects and native privilege/feature validation.

`__builtin_ia32_vmfunc_eptp_switch(unsigned index)` selects only VMFUNC leaf 0
with EAX=0 and the full ECX index, through `llvm.x86.vmfunc.eptp.switch`.
It does not synthesize VMfail status or define register behavior for other
VMFUNC leaves. Entry/exit continuations, hypercall protocols and arbitrary
VMRUN machine-state transport remain separate work.

Microsoft `<intrin.h>` interfaces `__svm_{vmload,vmsave,invlpga,stgi,clgi}`
and `__halt` lower through the same native paths. The old assembly-only halt
wrapper is removed. [tests/verify-bounded-virtualization-completion.py](tests/verify-bounded-virtualization-completion.py)
checks exact bytes, full inputs, dead/repeated effects, live values, memory
ordering, saved registers and continuation at O0/O2, including all three
object formats and both assembly dialects. Linux x64 children compare real
CPL3 faults with independent byte oracles.

### Secure-loader launch

`__builtin_ia32_skinit(unsigned physical_address)` and Microsoft's
`__svm_skinit(int)` emit `llvm.x86.skinit(i32)` directly. The complete input
reaches EAX unchanged, including unaligned values. SKINIT performs its own
checks before clearing the low 16 bits, resets processor state and enters the
secure loader; it has no normal continuation in the old C ABI context.
Both builtins are nonreturning. The LLVM declaration is convergent and
noduplicate, with unknown memory effects and no willreturn, nosync, nofree or
nocallback promise. No inline assembly or external launch helper is emitted.
The contract follows [AMD APM Volume 3 revision 3.38, pages 472–473](https://docs.amd.com/v/u/en-US/24594_3.38_APM_Vol3_PUB).

The full LLVM fork first added this API in `6804d12d13a8`. The native signature
and system-effect synchronizers retain the pruned enum, attributes, matcher IDs
and terminating machine descriptor together. `X86SkinitTest` checks both
spellings, ordered stores, removed continuation, malformed inputs and O0/O2
ELF/COFF/Mach-O output. The [binary oracle](tests/neverc/X86/verify_skinit_completion.py)
also checks original EAX bits at a stopped instruction boundary: 1,080 checks
for NeverC's six objects and 1,080 for the full fork's twelve i386/x64 objects.
It never executes SKINIT or qualifies reset, attestation or successful launch.

[tests/verify-bounded-virtualization-qemu.py](tests/verify-bounded-virtualization-qemu.py) executes the generated code in
disposable i386/x64 CPL0 guests. It checks VMSAVE/VMLOAD MSR round trips,
INVLPGA fallthrough, AC changes, SWAPGS exchange, GIF masking of an actual
pending PIC interrupt, and HLT continuation after a timer interrupt. It shares
the descriptor-state guest build/boot runner, including the bitcode bridge.
TCG results do not qualify physical TLB invalidation or successful VMFUNC in
VMX non-root operation.

Both harnesses include explicit address-width overrides. The CPL0 fixture
places a second VMCB in guest RAM below 64 KiB, so i386's AX address refers to
valid physical storage. Exact-byte and live-input checks separately verify
prefix retention and narrowing without losing callers' original values.

### Software interrupt contracts

`__builtin_ia32_int(vector)` emits the shared `llvm.x86.int` intrinsic. The
vector must be a constant in 0..255. This raw interface is resumable and retains
ordered memory effects; it does not mark the call `noreturn` or `willreturn`.
The Microsoft `__int2c()` assertion interface retains its existing `noreturn`
contract, matching Clang's Windows compatibility behavior, while now emitting
`llvm.x86.int(i8 44)` instead of the private `llvm.x86.int2c` API. This lets its
bitcode pass through the full LLVM fork without an unresolved private call.
Previously produced NeverC intrinsic IDs remain available.

[tests/verify-interrupt-completion.py](tests/verify-interrupt-completion.py) checks live inputs, repeated interrupts,
ordered stores/loads and the continuation in i386 and x64 ELF/COFF/Mach-O.
The native Linux x64 fixture compares independently encoded INT 2C bytes
with the raw builtin through each compiler route, both with an unhandled fault
and with a handler that advances the fault PC and returns through sigreturn.
Controlled memory updates independently check optimizer ordering. The contract
assumes the handler preserves the compiler's register ABI; it does not transport
arbitrary handler machine state or qualify privileged interrupt gates. Tests
separately retain the Microsoft assertion's non-returning behavior.

### Bounded SMX register transport

`__builtin_ia32_getsec_bounded(a, b, c, d, registers_out)` transports full
RAX/RBX/RCX/RDX values, returns RAX and stores the other three results to
`unsigned long long` cells after the native instruction. Its constant first
argument selects CAPABILITIES (0), PARAMETERS (6), SMCTRL (7) or WAKEUP (8)
through its low EAX. The full LLVM fork exposes `llvm.x86.getsec.bounded`
and `llvm.x86.getsec.bounded32`, each returning four natural-width registers
from four inputs. This includes prior register bits retained on leaf-dependent
paths. No flags are returned; the allowed leaves preserve arithmetic flags,
and no incoming compiler flag state is part of this API.

The frontend and backend share `llvm/IR/X86SmxIntrinsics.h`. Reserved or
transferring leaves, dynamic selection and wrong code modes are rejected.
Code16 is not supported by this intrinsic. LLVM has no separate SMX target
feature gate here: native CPUID/CR4.SMXE, privilege, chipset and measured-state
checks remain processor behavior. Conservative instruction memory effects
also retain SMCTRL/WAKEUP's external state changes. This does not provide the
complete-state continuation ABI for ENTERACCS, EXITAC, SENTER or SEXIT.

`tests/verify-sgx-supervisor-completion.py --bounded-getsec` checks every allowed
leaf and register output, full-width inputs, dead/repeated calls, input liveness,
ordered memory, unmapped scalar inputs and native object/assembly round trips.
The real CPL3 oracle compares faults and, if SMXE permits a query, all four
returned registers against independent instruction bytes. Injected diagnostic
outputs qualify compiler transport rather than implementing SMX behavior.
The contract follows Intel's [SDM Volume 2D](https://cdrdv2.intel.com/v1/dl/getContent/671143),
chapter 7, including the unchanged-register cases in PARAMETERS.

### SGX supervisor, VMM and bounded user completion

`__NEVERC_X86_SGX_COMPLETION__ == 1` advertises the x64 compiler API even
when the translation unit has no SGX CPU feature enabled. Source generators
can select the native spellings and attach a per-function `target("sgx")`
attribute. This marker does not define `__SGX__` or certify CPU/OS availability.

`__builtin_ia32_encls(a, b, c, d, registers_out, flags_out)` and `enclv` take
four `unsigned long long` inputs for RAX/RBX/RCX/RDX, return the full native
RAX value, and store RBX/RCX/RDX to three `unsigned long long` output cells.
The `unsigned char` flag output is `CF | (ZF << 1)`. These output stores are
ordinary C accesses after the instruction. Input register values retain all
bits, even though only EAX selects a leaf. Callers must supply valid scalar
values for all four arguments; implicit enclave accesses use the architectural
DS/mode rules inside the instruction.

The shared IR APIs `llvm.x86.encls` and `llvm.x86.enclv` return
`{i64, i64, i64, i64, i8}` from four i64 arguments. The full fork also supplies
`encls32` and `enclv32` with four i32 inputs/results plus i8 flags. Mode
mismatches and a missing `sgx` target feature are diagnosed. Both backends
model all four implicit input/output registers, EFLAGS and conservative memory
effects. Scalar status is captured before arithmetic can overwrite flags;
dead results cannot remove or combine the native operations.

On leaves that preserve arithmetic flags, the flag byte reflects unmodelled
incoming machine flags and is not a defined status. A lifter must retain its
explicit incoming flag state for those leaves. These bounded APIs do not
provide ENCLU's entry/exit/resume continuations or full enclave state transport.
Native privilege, feature, EPC and VMX checks remain hardware behavior.

`__builtin_ia32_enclu_bounded` adds the same register/status contract for
constant fallthrough leaves: EREPORT (0), EGETKEY (1), EACCEPT (5), EMODPE
(6), EACCEPTCOPY (7), EVERIFYREPORT2 (8), EDECCSSA (9), EREPORT2 (10) and
EGETKEY256 (11). Its first argument is a constant full-width RAX value;
only its low 32 bits select the leaf. The frontend and backend share
`llvm/IR/X86SgxIntrinsics.h` for that validation. The full LLVM fork exposes
`llvm.x86.enclu.bounded` and `llvm.x86.enclu.bounded32` with the corresponding
64-bit and 32-bit signatures and an `immarg` first argument. The DAG lowering
materializes that constant into rAX before the instruction. Dynamic leaves,
EENTER (2), ERESUME (3), EEXIT (4) and unknown leaves are rejected because
this API does not own their continuation state. Code16 is also rejected.

The header's `_enclu_bounded_u32(leaf, data)` macro takes a constant leaf,
evaluates the three-element data-array expression once and returns low EAX.
Use it in a function with the `sgx` target feature. Unlike the direct builtin,
this wrapper intentionally takes a 32-bit leaf and does not expose CF/ZF.
`tests/verify-sgx-supervisor-completion.py --bounded-enclu` qualifies all nine
leaves through direct LLVM, NeverC and NeverC-to-full-LLVM routes. It checks
register transport (including high RAX bits), retained memory effects,
assembly round trips and real native fault boundaries outside an enclave.
Compiler regressions separately check constant validation. It does not qualify successful enclave execution or the
undefined/preserved incoming flags of individual leaves.

The installed `<sgxintrin.h>`, included through `<immintrin.h>` or
`<x86intrin.h>`, provides the standard `_encls_u32` and `_enclv_u32` wrappers
using these builtins. Their three-element `__SIZE_TYPE__` arrays retain the
standard input/output contract and their return is the low 32 bits of RAX.
The header retains the LLVM source license. [tests/verify-sgx-supervisor-completion.py](tests/verify-sgx-supervisor-completion.py)
checks independent/all register and flag uses, retained dead/repeated calls,
input liveness, memory ordering, RBX restoration, standard wrappers and
unmapped input boundaries at O0/O2. It covers ELF/COFF/Mach-O and both assembly
dialects. Isolated Linux x64 children additionally compare real CPL3 #UD faults
with independently encoded bytes. Controlled output injection qualifies
compiler transport, not successful privileged enclave operations.

### Native FNOP

`__builtin_ia32_fnop()` emits `llvm.x86.fnop()` and the original `d9 d0`
instruction. FNOP waits for pending x87 exceptions and updates the processor's
x87 instruction pointer; it is observable even though it performs no numeric
operation. The descriptor reads FPCW/FPSW, writes FPSW, may raise an FP exception
and retains conservative memory ordering. It does not invalidate x87 or MMX
payloads. The frontend accepts no arguments and returns void.

The MMX state test targets cover intrinsic attributes, descriptor effects and
O0/O2 IR/assembly on ELF, COFF and Mach-O. The full LLVM fork additionally
covers i386 and rejects malformed intrinsic declarations. Run
`tests/neverc/X86/verify_fnop_state.py --llc /path/to/llc --neverc /path/to/neverc
--clang /path/to/clang --output-dir /path/to/new-directory` on native Intel
Linux x64 to retain independent byte-oracle and generated-code observations.
Supplying both tools also checks NeverC text IR through the full LLVM fork.
Each compiler/optimization profile checks 48 CW, defined SW, TOP, tag, payload
and FIP observations plus two pending #MF faults in isolated child processes.
Undefined condition bits are excluded. This does not qualify other CPUs,
privileged #NM conditions, or FEMMS hardware execution.

### Native x87 control words

`__builtin_ia32_fnstcw(unsigned short *)` and
`__builtin_ia32_fldcw(const unsigned short *)` emit the fixed pointer interfaces
`llvm.x86.fnstcw` and `llvm.x86.fldcw`. Both return void and retain their actual
two-byte memory access, including unaligned addresses and memory faults.
FNSTCW does not wait for pending x87 exceptions. FLDCW checks pending exceptions
using the old control state; newly unmasked exceptions are delivered at the next
waiting instruction. Undefined condition-code bits are not promised to survive.

The IR interfaces have conservative memory effects. Their DAG memory operations
are volatile and aligned to one byte. The FLDCW descriptor reads FPCW/FPSW,
writes FPCW/FPSW and may raise an FP exception. FNSTCW reads FPCW, writes FPSW
and stores through its memory operand. The generated instruction table remains
owned by `sync-x86-system-effects.py`; its register-sequence selection must be
stable even when an appended sequence overlaps the previous table tail.

`X86X87ControlTest` checks header-free builtins, pointer qualification, and
O0/O2 native IR/assembly on ELF, COFF and Mach-O. `X86X87Control` checks
intrinsic and descriptor effects. Run
`tests/neverc/X86/verify_x87_control.py --llc /path/to/llc
--neverc /path/to/neverc --clang /path/to/clang
--output-dir /path/to/new-directory` on native Linux x64 for an independent
byte-oracle comparison. Each compiler/optimization profile retains 1,152 state
observations, four page-boundary accesses and 26 fault/nonfault cases, including
20 expected faults. The test observes stores before the fault, the faulting
opcode and saved control/status state. Children exit in their signal handler;
generated code is never resumed by guessing an instruction length. Supplying
both tools also checks NeverC text IR through the full LLVM fork. Other CPUs,
i386 execution, privileged #NM and alignment-check exceptions are outside this
harness's qualification.

The full LLVM O2 profile first runs `opt -passes=default<O2>`; `--opt` defaults
to the sibling of `--llc`. Compiler/script inputs must remain unchanged during
execution, and the record hashes every retained source, object, executable and
output. Six O0/O2 profiles on an Intel Core i9-13900H Linux x64 host pass 6,912
state observations, 120 expected faults and 24 page-boundary accesses. Their
recorded outputs agree across direct LLVM, NeverC and the text-IR bridge.

### Native FEMMS and legacy MMX IR

`__builtin_ia32_femms()` already has a native frontend and embedded-backend
interface. It emits `llvm.x86.mmx.femms` and the original `0f 0e` instruction.
The descriptor invalidates all eight MMX and eight x87 register payloads;
conservative side effects preserve repeated calls and surrounding memory
operations. It must not be replaced with EMMS, whose payload contract differs.

`X86MMXStateTest` checks O0/O2 IR and both assembly syntaxes for ELF, COFF and
Mach-O. `X86MMXState` checks the complete implicit clobber set and intrinsic
effects. [tests/verify-mmx-state.py](tests/verify-mmx-state.py) independently
decodes native object files and completes each FEMMS boundary with adversarial
register contents. Its 12 objects cover direct NeverC output and NeverC IR
through the full LLVM fork, including live MMX values, realigned frames,
dynamic allocation, memory ordering and ABI preservation (432 completions).
This does not qualify physical FEMMS execution or x87 exception delivery.

EMMS and FEMMS are also machine scheduling boundaries. Their physical
clobbers alone did not constrain virtual MMX registers: O2 could move MMX
arithmetic and integer extraction past the final reset and return with
nonempty x87 tags. All six O2 objects in the direct/bridge format matrix
failed that postcondition before this fix. The oracle now checks empty tags
on return, and the frontend regression checks both final reset instructions
with realigned/dynamic frames. Physical EMMS cleanup was reproduced by
replacing only the unavailable FEMMS boundaries with EMMS; the returned
abridged tag changed from 255 before the fix to 0 afterward. That experiment
does not execute FEMMS or establish its hardware fault behavior.

The full fork now restores `llvm.x86.mmx.femms`, accepts the legacy `3dnow`
and `3dnowa` annotations, and upgrades textual `x86_mmx` types to `<1 x i64>`,
matching its existing bitcode upgrade. Use a build containing those changes
for this bridge check; the production LLVM release pin is still pending the
remaining instruction work.

### Native 3DNow values

The existing 25 `__builtin_ia32_*` spellings cover all 24 3DNow/3DNowA
arithmetic, comparison, conversion and shuffle instructions; `pswapdsi` and
`pswapdsf` select the same instruction. Function-level `target("3dnowa")`
enables these interfaces. Their `llvm.x86.3dnow.*` and `llvm.x86.3dnowa.*`
names now have matching definitions and register/memory selection patterns
in the full fork. Its MMX type legalization maps the upgraded `<1 x i64>`
IR carrier back to the native MMX register class. Use a fork build containing
these definitions for the bridge; no new production SDK is pinned yet.

These are packed value interfaces. They preserve the processor's original
arithmetic, including reciprocal/refinement behavior, and do not promise
ordinary IEEE floating-point replacements or an ambient x87 context ABI.
End MMX use with EMMS/FEMMS before executing x87 code.

The installed `<mm3dnow.h>` and umbrella `<x86intrin.h>` expose all 25 `_m_*`
value interfaces plus `_m_femms()`, with function-level `3dnow`/`3dnowa`
requirements. The header is imported from LLVM `llvmorg-18.1.8`, retaining
its Apache/LLVM exception license and exact source revision. Its standard
`_m_pfrsqrtit1` spelling maps to `__builtin_ia32_pfrsqit1`.

[ThreeDNowFrontendTests.cpp](tests/neverc/X86/ThreeDNowFrontendTests.cpp)
checks every builtin and both header entry points through IR and both assembly
syntaxes at O0/O2 for ELF, COFF and Mach-O.
[tests/verify-3dnow-values.py](tests/verify-3dnow-values.py)
compares 50 compiled register/memory wrappers with independently encoded
instructions inside a disposable QEMU TCG guest. Each optimization level
checks 1,600 results across eight input pairs and four x87 rounding controls,
plus control/status/tags and input canaries. Native processor approximations
are compared within the same CPU model; this is not a mathematical oracle
for all processors or a physical hardware/fault qualification.

Run it with `--compiler /path/to/neverc` or `--llc /path/to/llc`. The latter
also supports `--mode 32`; NeverC uses its supported x64 target. Add
`--backend-llc /path/to/llc` to send NeverC text IR through the full fork,
or additionally `--bitcode` to exercise the bitcode reader. `--keep DIRECTORY`
retains inputs, generated objects, guest images and qualification records.
`--headers` selects the standard interfaces instead of direct builtin calls.
GNU binutils, a host Clang and `qemu-system-x86_64` are required. Optional
`--context-object PATH` arguments additionally execute NeverD's owned FEMMS
probe objects, whose `isa_probe` performs MOVQ/FEMMS/FLD1/FSTP and stores
binary64 one at argument + 8. They check state transport, empty final tags
and memory canaries through the existing context emitter.

### APX native atomic transactions

On x86-64, `__NEVERC_X86_APX_ATOMIC__` is 1. The builtins
`__builtin_ia32_{aadd,aand,aor,axor}{32,64}_address` take an unsigned 64-bit
offset, an unsigned value of the named width, a constant address width and a
constant segment prefix. They return void. The two
`__builtin_ia32_cmpccxadd{32,64}_address` builtins take offset, comparator,
addend, constant condition, address width and segment prefix, and return the
old memory value as an unsigned integer of the named width.

Address width is 32 or 64; segment is 0 for the DS default or one of the
override bytes `0x26`, `0x2e`, `0x36`, `0x3e`, `0x64`, `0x65`. Conditions use
the architectural 0..15 encoding. Invalid or dynamic controls are diagnosed.
Narrow addressing wraps before native segment-base addition. The APIs retain
EVEX encoding, native atomic access and faults, including CMPccXADD write-back
when its condition is false. They do not check CPU availability or promise
alignment or successful completion.

These builtins emit `llvm.x86.<operation><width>.address` directly. Unknown
memory and synchronization effects preserve repeated calls and dead results;
CMPccXADD defines flags and RAO-INT preserves them. `X86APXAtomicNative.h`
shares the machine descriptor with the full fork. The generated intrinsic
synchronizer owns signatures and immediate positions, while
`sync-x86-apx-atomic.py` appends the pruned EVEX opcodes and native pseudos
without renumbering existing machine opcodes.

[tests/verify-apx-atomic-completion.py](tests/verify-apx-atomic-completion.py) qualifies O0/O2 in ELF, COFF and Mach-O
through NeverC, the full fork (`--llc`), or NeverC IR followed by the full fork
(`--backend-llc`). It checks all conditions and segment/address controls,
live/dead/repeated calls, aliased ordinary memory and C ABI preservation with
independently decoded, controlled completions. ATT and Intel assembly round
trips preserve every effective instruction's bytes and address; alignment
NOP encodings can differ. Controlled completions do not establish physical
atomicity, coherence or hardware fault behavior.

The additional `__NEVERC_X86_LEGACY_ATOMIC__` capability is 1 on x86-64.
The ten `__builtin_ia32_{aadd,aand,aor,axor,cmpccxadd}{32,64}_legacy_address`
builtins have the same operand and control contracts as the APX forms, but
emit `llvm.x86.<operation><width>.legacy.address`. They retain legacy RAO
or VEX CMPccXADD encoding even when APX register allocation is enabled.
The machine pseudos restrict their data operands to the low sixteen GPRs;
CMPccXADD's comparator is tied to its result. The ordinary RAO/VEX opcodes
already exist in the embedded backend, so no duplicate encoding tables are
introduced. Existing opcode identities remain stable.

The embedded intrinsic metadata and lowering also share the full fork's four
`llvm.x86.{aadd,aand,aor,axor}32.legacy.address32` interfaces for i386 IR.
These use an i32 offset and constant address width 16 or 32. NeverC's C
frontend remains x86-64-only; these i386 IR declarations are not frontend
builtins. Code mode, data width and address width are separate contracts.

Pass `--legacy` to the completion oracle for the legacy/VEX matrix. Each
compiler route covers 13,680 functions and 164,160 controlled completions
across O0/O2 and ELF/COFF/Mach-O, plus twelve assembly round trips. This
includes stack-reference controls with every permitted CS/DS/ES/SS override.
The independent parser decodes the RBP-plus-index SIB and its displacement,
refuses malformed or incompatible carriers, and checks the effective address,
callee-saved registers and exact return-stack balance. The
legacy encoding check is independent of the producer's instruction decoder;
the existing APX matrix remains available without the option.
The wrapper oracle resolves direct jumps produced by identical-function
folding, requires the expected atomic count for every entry, and still checks
every entry's executed operations and ABI. Indirect, interior, cyclic and
state-changing alias stubs fail the independent decoder checks.

The older pointer-taking RAO-INT/CMPccXADD, bit-test and conditional-arithmetic
intrinsics no longer infer address alignment from their integer bit widths.
Their backend memory descriptors retain byte alignment and volatile read/write
effects; native alignment and permission checks remain inside the instruction.
`neverc-x86-backend-tests` checks 48 signatures against the actual x86 target
lowering interface. Its source lives in `tests/neverc/X86/`, and
`check-neverc` builds this target when X86 is enabled. NeverC's target registry
is x86-64-only; the full LLVM fork additionally tests 32-bit code generation.

### Bounded leaf state with incoming arithmetic flags

`__NEVERC_X86_BOUNDED_LEAF_STATE__ == 1` advertises two additional x64 APIs:
`__builtin_ia32_enclu_bounded_state` and
`__builtin_ia32_getsec_bounded_state`. Both return void and take constant
`unsigned long long ax`, three `unsigned long long` register inputs,
`unsigned short flags`, `unsigned long long *registers` and
`unsigned short *result_flags`. They write all four full register results to
the four-element array. Leaf admission matches the existing bounded APIs.
The capability macro does not enable CPU features: ENCLU requires `sgx,sahf`
and GETSEC requires `sahf`, independently of SGX.

`llvm/IR/X86NativeArithmeticFlags.h` owns the packed status shared with REP:
CF/PF/AF/ZF/SF in bits 8/10/12/14/15 and OF in bit zero, with mask `0xd501`.
Other input bits are ignored and other result bits are zero. One machine
pseudo restores these six flags, executes the native leaf, and captures its
actual flags before compiler arithmetic. AX/DI exchanges preserve the complete
AX while LAHF/SAHF use AH; DI transports the flag word. This also avoids
clobbering i386's reserved ESI realignment base in the full fork. DI, EFLAGS
and unknown memory effects are declared. The MC window excludes
compiler spills, stackmap shadow padding and automatic assembler padding.
The full LLVM fork provides matching `.state` and `.state32` interfaces;
NeverC's frontend continues to target x64.

`sync-x86-bounded-leaf-state.py` retains the original two pseudo descriptors;
new lowering uses the register-window descriptors described below. All previous
opcode identities remain unchanged. Run
`tests/verify-sgx-supervisor-completion.py --bounded-enclu --explicit-state`
or `--bounded-getsec --explicit-state` for register and six-flag transport.
The controlled boundary checks all 64 arithmetic-flag combinations, reserved
input bits, independent results, dead/repeated calls and the callee-saved ABI.
Its separate Linux x64 CPL3 checks compare complete four-register/six-flag
fault contexts or normal query results with independent instruction bytes and
POPFQ/PUSHFQ flag transport. They cover all 64 input flag combinations, reserved
status bits and three full-width register patterns on both optimization levels,
including NeverC IR compiled through the full LLVM fork. Faults before the
original instruction boundary fail the check. Controlled completions do not
qualify successful enclave/SMX operation,
AEX, VM exits or nonlocal continuations. NeverD still needs an incoming CFG
proof of a bounded leaf before consuming these interfaces.

The recorded matrix passes 66,872 encoding/controlled-completion checks on
each NeverC route and 133,744 on the full LLVM fork's two code modes. Each
route compares 9,984 physical CPL3 boundaries. All physical observations on
this host were faults; successful enclave/SMX completion was not observed.
With `--keep`, `native-observations.json` records the fault and normal counts
for each leaf and optimization level. A deliberately corrupted incoming OF
bit is rejected by the independent POPFQ/PUSHFQ reference comparison.

### Descriptor and machine-state access

The following builtins emit native instructions through matching fixed LLVM
IR signatures; replace the builtin's underscores with dots after `llvm.x86.`:

| Builtin suffix | C signature | Result/effect |
| --- | --- | --- |
| `{verr,verw}_reg` | `unsigned char (unsigned short)` | Actual ZF, 1 for accessible/readable or writable |
| `{verr,verw}_mem` | `unsigned char (const void *)` | Same completion, with an instruction-owned m16 selector |
| `{sldt,str,smsw}_reg{16,32,64}` | `unsigned short/unsigned/unsigned long long (void)` | Native register result at the named operand width |
| `{sldt,str,smsw}_mem` | `void (void *)` | Instruction-owned m16 output, regardless of code mode |
| `{lldt,ltr,lmsw}_reg` | `void (unsigned short)` | Native selector/status-word input |
| `{lldt,ltr,lmsw}_mem` | `void (const void *)` | Instruction-owned m16 input |
| `invd` | `void (void)` | Native cache invalidation without writeback |

Each suffix is prefixed with `__builtin_ia32_`. The full LLVM fork supports
both code modes and rejects 64-bit register results on i386. Memory APIs use
AS0 and the default address width, without imposing C alignment. Wider SMSW
register forms are never synthesized by extending a 16-bit read: native upper
bits depend on mode and processor. The other instructions' privilege, UMIP,
descriptor and fault checks likewise remain native. LTR's descriptor busy-bit
update is a read/write effect even with a register selector. LLDT, LKGS and
VERR/VERW retain their implicit descriptor reads. All APIs remain ordered even
when their results are unused; no API promises a software descriptor model.

[tests/verify-descriptor-state-completion.py](tests/verify-descriptor-state-completion.py) checks operand encodings, widths,
ZF transport, repeated/dead effects, memory ordering, unmapped boundaries and
saved registers at O0/O2, including ELF/COFF/Mach-O and both assembly dialects.
Safe user-mode queries are compared with an independent assembly oracle in
isolated processes, with explicit UMIP/host-availability skips. The separate
[tests/verify-descriptor-state-qemu.py](tests/verify-descriptor-state-qemu.py) runs disposable CPL0 TCG guests against
GDT/LDT/TSS and CR0 state. It checks LTR busy-bit writes, both LLDT/LTR/LMSW
operand forms, native verification, m16 output boundaries and LMSW's inability
to clear PE. INVD's successful guest fallthrough does not qualify physical
cache discard. Both scripts accept the usual `--compiler`, `--llc` and
`--backend-llc` routes; QEMU additionally needs clang, ld.lld and GNU objcopy.
The CPL0 bridge uses NeverC's native bitcode output (`-c -emit-llvm`), letting
the newer LLVM reader upgrade legacy constant expressions without changing
the optimized module. The retained `.ll` file is diagnostic output; newer
LLVM text parsers reject some older constexpr syntax, such as a shift of a
global address used in descriptor construction. Prefer bitcode for this
cross-version boundary. Other differences between LLVM versions still need
their own qualification.

### Native memory default-segment provenance

`__NEVERC_X86_NATIVE_MEMORY_STACK_REFERENCE__ == 1` enables bit `0x100` in
native memory segment controls. OR it with a prefix byte to request a real
stack-default operand with a 64-bit offset. For example,
`__builtin_ia32_lar_mem32_address(offset, 64, 0x100, prior, &zf)` retains
stack-reference `#SS` faults for noncanonical addresses. An explicit `0x36`
SS prefix without this bit keeps data-default fault provenance in long mode.
CS/DS/ES/SS overrides can be combined with the bit; FS/GS, address32 and
unknown control bits are rejected.

This applies to explicit descriptor/VMX addresses, RAO/CMPccXADD, native
compress/expand and four-source memory operations, and LDTILECFG/STTILECFG.
AMX tile row-memory and MPX controls do not yet accept the bit: their index
or state semantics need separate lowering. Existing raw prefix values keep
their meaning. This API describes the native access; it does not authorize
execution of privileged instructions or promise an emulated completion.

The full LLVM fork publishes `LLVM_X86_NATIVE_MEMORY_STACK_REFERENCE` in
`llvm/IR/X86NativeMemoryAddress.h` and the shared `NativeMemoryStackReference`
constant. Its backend reserves the frame pointer, computes `offset - RBP`,
and uses `[RBP + index]` without replacing RBP or probing memory. CFI/SEH and
dynamic stack realignment remain under the normal frame-lowering owner.
Data-default addresses use allocator-owned registers excluding SP/BP; a
carrier pseudo preserves that constraint through coalescing and post-RA copy
propagation until MC lowering. Full LLVM also uses SI/DI for i386 address16,
so neither the PIC GOT register nor a reserved realignment base is overwritten.
NeverC remains x64-only. The append-only table authority is
[utils/lint/sync-x86-native-address-carrier.py](utils/lint/sync-x86-native-address-carrier.py); no intrinsic IDs change.
The address-carrier update established 18,573 instructions: it also caught up
32 previously implemented REP, bounded-state and atomic pseudos that were
missing from the checked-in schema. Existing instruction records, register
and feature identities remain unchanged; AArch64's schema is unchanged.

`X86NativeAddressTest` checks native code generation on ELF/COFF/Mach-O,
invalid controls, real LAR results against a register-form assembly oracle,
noncanonical `#SS/#GP` trap numbers, and dynamic stack canaries at O0/O2.
The native trap test requires Linux x64 and does not qualify AMX hardware.
The address completion scripts validate encodings, wrapped offsets, effects
and saved registers across both compilers and the NeverC-to-fork route.

### Native implicit registers and realigned frames

SGX, bounded GETSEC/ENCLU and CPUID must not replace the compiler's reserved
RBX realignment base. Their x64 lowering carries the native RBX input/result in
a tied virtual register outside RBX and the other implicit operands. A pseudo
survives through allocation and copy propagation; final MC emission swaps the
carrier into RBX, executes the operation and swaps the result back. The entire
window preserves the compiler's RBX value and permits no intervening spills or
padding. No compiler-owned frame register is declared as an instruction result.

[utils/lint/sync-x86-native-register-windows.py](utils/lint/sync-x86-native-register-windows.py) appends seven machine records.
The x86 schema contains 18,580 instructions; all earlier 18,573 records, 391
registers and 190 features remain unchanged, as does the AArch64 schema.
Intrinsic signatures and capability macros are unchanged. Accumulator exchanges
use the canonical short encoding, including the REP SCAS flag window, so object
emission and both assembly dialects round-trip to identical bytes.

`X86NativeRegisterTest` runs CPUID against an independent header-assembly
reference with dynamic stack canaries under SysV and Microsoft calling
conventions. The SGX/SMX completion oracle includes realigned dynamic frames
and verifies the callee-saved registers, all native results and six arithmetic
flags across ELF, COFF and Mach-O. These controlled completions qualify compiler
transport; successful enclave/SMX execution and nonlocal transitions still need
their own execution environment and semantic contract.

The register-window qualification passes 156,640 SGX/SMX encoding and controlled
completion checks on full LLVM and 78,936 on each NeverC route. Separate CPL3
checks cover 9,984 native boundaries for LLVM and 10,044 for each NeverC route.
The REP regression passes 45,152 memory/flag cases, 112 hardware fault restarts
and 192 controlled port completions per x64 route; the full fork additionally
passes 8,928 i386 memory cases and 2,304 controlled port cases in emulation.

The full fork also preserves the i386 ESI realignment base for explicit
MOVS/LODS/CMPS/OUTS state and raw MOVS/OUTS intrinsics. Their tied, early-clobber
carrier excludes the other native operands; final MC emission exchanges ESI
only inside the indivisible operation. Raw intrinsics retain ambient DF and
arithmetic flags. Explicit-state intrinsics restore DF=0 on normal completion.
NeverC remains x64-only, so it does not acquire the six i386 machine records.
Both backends use the canonical accumulator ADD encoding for REP comparison
flag restoration as well as for bounded SGX/SMX leaves.

[tests/verify-rep-frame.py](tests/verify-rep-frame.py), run with
`--llc /path/to/full-fork/llc --keep /path/to/results`, checks 180 i386 objects
and 2,160 controlled completions across ELF/COFF/Mach-O, O0/O2, address16/32,
element sizes and both directions. Unmapped native operands expose any
compiler spill or canary access through a borrowed frame base. The checks cover
native inputs/results, comparison flags, stack canaries and callee-saved
registers, with byte-identical object/assembly round trips in both dialects.
They do not execute port I/O or qualify physical REP fault restart.

### Explicit descriptor-state addresses

Thirteen header-free builtins add `offset, address_bits, segment_prefix`
operands to descriptor-state memory instructions:
`__builtin_ia32_{verr,verw,sldt,str,smsw,lldt,ltr,lmsw}_mem_address` and
`__builtin_ia32_{sgdt,sidt,lgdt,lidt,invlpg}_address`. Offsets are
`unsigned long long`; the two unsigned controls are constants. VERR/VERW
return `unsigned char` ZF and the other interfaces return `void`. All retain
instruction-owned accesses and unknown memory effects, including dead checks.
The same width/prefix validation as the descriptor-query APIs applies.

Full LLVM supplies the corresponding `.address` and i386 `.address32` APIs,
plus `llvm.x86.{sgdt16,sidt16,lgdt16,lidt16}.address32` for the legacy 16-bit
descriptor-table operand size. These are distinct from a 16-bit address.
NeverC remains x64-only. Existing pointer and register builtins are unchanged.
The instruction generator appends thirteen address-size pseudos while
preserving every existing opcode identity.

[tests/verify-descriptor-state-address-completion.py](tests/verify-descriptor-state-address-completion.py) checks complete native
bytes, all seven segment controls and both address widths, O0/O2, three object
formats, ATT/Intel assembly round trips, wrapping high offsets, ZF, live inputs,
repeated/dead calls, memory ordering and saved registers. Its controlled
instruction boundaries do not model descriptor permissions or native faults.
`tests/verify-descriptor-state-qemu.py --address-bits 32` additionally runs
CPL0 selector, CR0 and descriptor-table transfers on TCG; full LLVM includes
i386 16-bit table loads/stores against independent original bytes. The
`--address-bits 64` run covers natural long-mode addressing. Physical hardware
qualification is separate.

The full fork passes 3,144 object/encoding checks; NeverC and its bitcode-to-fork
route each pass 1,404. Explicit-address CPL0 runs pass 210 checks for full LLVM
with `--address-bits 32`, 102 for its 64-bit-only run, and 102 for each NeverC
route at either width. The compiler suite passes 65 tests, the independent
binary suite 89, generator/sort regressions 36, and target/MC plugin tests 221.
Both schemas regenerate successfully: x86 adds thirteen instructions to 18,538,
while all prior instructions, registers and features remain unchanged.

### Descriptor queries (LAR / LSL)

`__builtin_ia32_lar_{reg,mem}{16,32,64}` and the corresponding `lsl` builtins
take a selector, a prior destination value, and an `unsigned char *` status
output. The return type is `unsigned short`, `unsigned int`, or
`unsigned long long`, matching the operand width. Register selectors are
`unsigned short`; memory selectors use `const void *` so the C interface does
not impose alignment on the architectural m16 read. That read belongs to the
instruction, including native descriptor checks and faults. The status output
is a separate ordinary C store after the query.

The shared native IR names are `llvm.x86.{lar,lsl}.{reg,mem}{16,32,64}` with
`{iN, i8} (i16-or-ptr, iN)` signatures. ZF is 1 on success and 0 on failure.
Success returns the native instruction result; failure returns the supplied
prior value. In particular, LAR's architecturally undefined result bits are
not replaced with invented descriptor data. The result width is independent
of the code mode: the full LLVM fork supports 16/32-bit queries in both modes
and rejects 64-bit queries outside 64-bit mode. Memory pointers currently use
the default address space and address size.

The existing Microsoft `__segmentlimit` remains a 32-bit LSL API whose result is
unspecified on failure. Its `llvm.x86.lsl(i32)` signature now works through
the full LLVM fork too. The pruned backend's former chainless lowering could
discard unused reads; both backends retain dead and repeated queries in order.
Use the new result/status API when failure preservation matters. Non-Microsoft
targets need `-fms-extensions` to expose the legacy spelling.

[tests/verify-descriptor-query-completion.py](tests/verify-descriptor-query-completion.py) accepts `--compiler`, `--llc`,
and `--backend-llc`. It checks each width and operand form, independent value
and ZF uses, dead/repeated queries, failure preservation, ordinary memory
ordering, unmapped selector boundaries and the C ABI at O0/O2. ELF, COFF and
Mach-O objects also round-trip through both assembly dialects. Controlled
completions test compiler transport, not descriptor-table emulation. On Linux
x86-64 the NeverC and bridge routes additionally compare real user-mode
queries for null and current-CS selectors against an independent assembly
oracle. Privileged descriptor changes and fault delivery are outside that
hardware check.

### CR/DR register ops

`__builtin_ia32_readcr/readdr(unsigned int index)` return a full unsigned
64-bit value; `__builtin_ia32_writecr/writedr(index, value)` write one. The
index must be a constant: CR0/2/3/4/8 or DR0..7. The existing Microsoft
`__readcr*`, `__writecr*`, `__readdr` and `__writedr` spellings use the same
IR/lowering. DR4/DR5 keep their own encodings and native alias/fault behavior.
NeverC diagnoses invalid or dynamic indices before code generation, and IR
verification also enforces `immarg` on these four signatures.

Reads use chained native MOV nodes. The former `CopyFromReg` lowering lost
unused reads and merged repeated reads at O2. Writes use `WRITE_SYSREG64`,
expanded to a real MOV before machine optimization. An ordinary register COPY
does not model the access's ordered side effects or its undefined arithmetic
flags. The system register bank remains reserved in `X86RegisterInfo.cpp`.
Both read and write MOV descriptions define EFLAGS, as required by Intel's
[SDM Volume 2B](https://cdrdv2.intel.com/v1/dl/getContent/671241).

`sync-x86-system-pseudos.py` owns the appended pseudo and its descriptor, name,
operand and feature tables; it preserves all existing opcode numbers.
`sync-x86-system-effects.py` owns the native MOV effects, and
`sync-x86-native-intrinsics.py` owns the immediate signature attributes.
The LLVM fork exposes matching 64-bit APIs and separate `readcr32/readdr32`
and `writecr32/writedr32` APIs. Wrong-mode calls and CR8 in 32-bit mode fail
explicitly.

[tests/verify-system-register-completion.py](tests/verify-system-register-completion.py) accepts `--compiler`, `--llc`,
or `--backend-llc`, and `--spelling builtin|microsoft`. It intercepts every
register access and checks all valid indices, full-width values, live/dead and
repeated reads, repeated writes, write/read sequences, ordinary memory
ordering, undefined-flag clobbers and callee-saved registers at O0/O2. These
are controlled compiler completions, not native privileged execution tests.

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

The portable names below are builtins on every x86-64 target, without
`intrin.h`, a Windows SDK or Microsoft language extensions. They share the
native IR lowering and memory effects of the corresponding `__vmx_*` APIs:

```c
unsigned char __builtin_ia32_vmread(unsigned long long field,
                                   unsigned long long *out);
unsigned char __builtin_ia32_vmwrite(unsigned long long field,
                                    unsigned long long value);
unsigned char __builtin_ia32_vmclear(const void *operand);
unsigned char __builtin_ia32_vmptrld(const void *operand);
void __builtin_ia32_vmptrst(void *operand);
unsigned char __builtin_ia32_vmxon(const void *operand);
void __builtin_ia32_vmxoff(void);
unsigned char __builtin_ia32_vmlaunch(void);
unsigned char __builtin_ia32_vmresume(void);
```

Pointer operands use the default address space. Use the explicit-address
interfaces below when an architectural segment override is required.

`__vmx_vmread` lowers to the memory form of VMREAD. The instruction owns the
conditional write; do not emit a register VMREAD followed by an unconditional
LLVM store. On VMfail the destination must remain unchanged, and the compiler
must not introduce an additional memory access. Both VMREAD and VMWRITE define
EFLAGS, and VMWRITE's field and value are inputs, not register definitions.
The full LLVM fork owns these properties in `X86InstrVMX.td`; this pruned tree's
corresponding descriptors are maintained by
`python3 utils/lint/sync-x86-vmx-effects.py --write` and checked in generated
table CI. The generator also owns the EFLAGS definitions of VMX management
instructions, the m64 reads of VMCLEAR/VMPTRLD/VMXON, the m64 write of VMPTRST,
and the EFLAGS definitions and m128 reads of INVEPT/INVVPID.
This does not reorder opcode or intrinsic IDs.

`__builtin_ia32_invept` and `__builtin_ia32_invvpid` take an
`unsigned long long` type selector and a `const void *` descriptor, and return
the same `unsigned char` completion status. The full 64-bit selector reaches
the instruction, and the descriptor access remains instruction-owned. Their
native IR signatures are `i8 @llvm.x86.invept(i64, ptr)` and
`i8 @llvm.x86.invvpid(i64, ptr)`, matching the full LLVM fork. These APIs use
the default pointer address space; they do not encode segment overrides or
forced alternative instruction encodings. Run
`python3 utils/lint/sync-x86-native-intrinsics.py --write` to install their
declarations while preserving the identities of existing intrinsics across
the enum, name, type, attribute, overload and instruction-selection tables.
Generated-table CI checks these declarations and the metadata sorter's tests.

Run `python3 tests/verify-vmcs-completion.py --compiler build-neverc/bin/neverc`
with `capstone`, `lief`, and `unicorn` installed to check both live and dead
status results at O0/O2. The test intercepts the actual VMCS instruction, checks
its operands, supplies each architectural completion, and executes the remaining
machine code. Failed reads include unchanged and unmapped destinations. This
qualifies compiler transport and native intrinsic emission; successful privileged
execution still requires a VMX environment. NeverC's pruned X86 backend currently
targets x86-64. Run the same test with `--llc /path/to/fork/llc` to cover the LLVM
fork's x86-64 and i386 intrinsic lowering, including its separate 32-bit forms.
The frontend test uses freestanding headers, so it needs no Windows SDK.
Add `--portable-builtins` to check the header-free names, also with
`--backend-llc` for the full LLVM fork. The frontend regression suite separately
compiles all nine portable APIs at O0/O2 for ELF, COFF and Mach-O, checking both
native IR calls and machine instructions.
Use `--family access`, `--family management`, or `--family invalidation` for a
focused run. Invalidation coverage checks the complete type selector, the
unchanged m128 descriptor, all three completion statuses, and the unmapped
descriptor boundary before supplying completion. Management
coverage keeps the explicit memory operand at 64 bits in both code modes and
checks that an unmapped operand is not accessed before the VMX instruction.
VMLAUNCH/VMRESUME are tested only on their falling-through VMfail paths; guest
entry or late entry failure transfers control through the VMCS and is not a
normal intrinsic return.
Add `--backend-llc /path/to/fork/llc` to the frontend run to compile NeverC's
actual emitted LLVM IR with the full fork and test the resulting machine code.
This checks the shared intrinsic names and signatures across both compilers.

The register and memory-source forms also have explicit native interfaces:

```c
unsigned long long __builtin_ia32_vmread_reg(unsigned long long field,
                                            unsigned long long prior,
                                            unsigned char *status);
unsigned char __builtin_ia32_vmwrite_mem(unsigned long long field,
                                        const void *source);
```

`llvm.x86.vmread.reg(i64 field, i64 prior)` returns `{i64, i8}`: the field
value on success or `prior` on either VMfail, followed by the usual status.
The C builtin stores that status to the caller's valid writable byte pointer;
this is an ordinary C output, not a VMREAD memory destination. Lowering emits
register VMREAD, captures CF/ZF before arithmetic, and selects the prior value
on failure. Both results may be discarded without removing the access.
`llvm.x86.vmwrite.mem(i64 field, ptr source)` returns i8 status and emits the
memory-source instruction; it does not preload the pointed-to word. The full
fork has matching `.reg32` and `.mem32` code-mode APIs with i32 field/value
operands. Wrong-mode APIs fail explicitly. Pointer APIs use address space 0.

`sync-x86-native-intrinsics.py` also owns the long IIT signature for VMREAD's
two results. New long signatures append before the sentinel, preserving all
existing offsets; generator tests protect reuse, idempotence and the exact
result/argument types. Intrinsic insertion still remaps every indexed table
and the DAG matcher IDs together.

[tests/verify-vmcs-forms-completion.py](tests/verify-vmcs-forms-completion.py) checks these forms at O0/O2, including
each result independently, discarded results, repeated operations with
independent statuses, full-width values, memory barriers, saved registers and
unmapped memory-source boundaries. `--llc` and `--backend-llc` select direct
full-fork and NeverC-to-fork verification as above. Controlled completions test
compiler transport; they do not establish hardware VMCS execution or faults.

Explicit address APIs additionally preserve address-size and segment overrides:

```c
unsigned char __builtin_ia32_vmread_address(unsigned long long field,
    unsigned long long offset, unsigned address_bits, unsigned segment_prefix);
unsigned char __builtin_ia32_vmwrite_mem_address(unsigned long long field,
    unsigned long long offset, unsigned address_bits, unsigned segment_prefix);
unsigned char __builtin_ia32_invept_address(unsigned long long type,
    unsigned long long offset, unsigned address_bits, unsigned segment_prefix);
unsigned char __builtin_ia32_invvpid_address(unsigned long long type,
    unsigned long long offset, unsigned address_bits, unsigned segment_prefix);
unsigned char __builtin_ia32_vmclear_address(unsigned long long offset,
    unsigned address_bits, unsigned segment_prefix);
unsigned char __builtin_ia32_vmptrld_address(unsigned long long offset,
    unsigned address_bits, unsigned segment_prefix);
void __builtin_ia32_vmptrst_address(unsigned long long offset,
    unsigned address_bits, unsigned segment_prefix);
unsigned char __builtin_ia32_vmxon_address(unsigned long long offset,
    unsigned address_bits, unsigned segment_prefix);
```

The last two arguments must be compile-time constants. NeverC accepts address
width 32 or 64, and segment prefix 0 (native DS default), `0x26` (ES), `0x2e`
(CS), `0x36` (SS), `0x3e` (DS), `0x64` (FS) or `0x65` (GS). These are actual
prefix bytes, not LLVM address-space numbers. Native mode and segment rules
apply, including ignored CS/DS/ES/SS bases in long mode. The integer offset is
truncated before segment-base addition; lowering fixes the base to BX/EBX/RBX
so the zero-prefix form cannot accidentally select SS through BP/SP allocation.
The instruction owns memory access, privilege checks, VMfail and fault order.
VMREAD still writes only on success. VMPTRST writes m64 in both code modes;
VMCLEAR/VMPTRLD/VMXON read m64, and INVEPT/INVVPID read m128. Other than VMPTRST,
the APIs return the same 0/1/2 completion status above.

Native IR names use `.address`, for example `llvm.x86.vmread.address` and
`llvm.x86.vmwrite.mem.address`. The full LLVM fork also provides `.address32`
forms with natural i32 selector/offset arguments and address width 16 or 32.
Wrong-code-mode and code16 use fail explicitly. These APIs emit legacy VMX
encodings; forcing EVEX/APX remains a separate encoding contract.
`X86NativeMemoryAddress.h` owns the width/prefix validation in both LLVM trees,
and the native-intrinsic synchronizer owns the signatures and per-argument
`immarg` attributes. Narrow addressing uses dedicated machine pseudos with
normal pointer register classes and native memory/EFLAGS effects. MC lowering
selects EBX (or BX in the full fork's i386 route) for the actual address.
`sync-x86-system-address-instructions.py` appends these eight NeverC opcodes
without renumbering existing IDs. Repeated or discarded calls retain effects.

Run [tests/verify-vmx-address-completion.py](tests/verify-vmx-address-completion.py) with the same `--compiler`, `--llc`
and `--backend-llc` routes above. It checks all address-width/prefix choices,
three object formats, assembly round trips, all VMX completions, full-width
selectors, preserved live inputs/callee-saved registers, repeated and discarded
results, memory ordering and unmapped operand boundaries at O0/O2. Direct LLVM
O2 cases run the IR optimizer as well as code generation. On Linux x86-64 the
C routes compare real CPL3 faults against independent raw-byte oracles.
Controlled segment-base addition verifies transport only; descriptor validation
and successful VMX operation still require a suitable execution environment.
The current matrix passes 8,416 direct full-LLVM checks and 4,208 checks on
each C route, with 896 real CPL3 fault boundaries per C route. All 58 focused
compiler tests, 81 binary checks, 29 generator tests and 12 plugin checks pass;
the overload bitmap has no mismatches. The x86 schema retains all 18,495 prior
opcode records unchanged and appends eight address-size pseudos (18,503 total).

### Explicit descriptor-memory addresses

`__builtin_ia32_lkgs_address(offset, address_bits, segment_prefix)` keeps the
m16 selector access inside LKGS. The six
`__builtin_ia32_{lar,lsl}_mem{16,32,64}_address` builtins take those same first
three arguments, followed by a prior destination and an `unsigned char *`
for the actual ZF. They return the operand-sized value, selecting the supplied
prior on failure. The ZF pointer is an ordinary C output after completion.
Offsets are `unsigned long long`; width and segment controls are constants
with the same validation and native ordering as the VMX address APIs above.

The matching native IR names are `llvm.x86.lkgs.address` and
`llvm.x86.{lar,lsl}.mem{16,32,64}.address`. The full LLVM fork additionally
provides `.address32` query forms for 16/32-bit results in i386 code, using an
i32 offset and address width 16 or 32. Code16 and mismatched code modes fail
explicitly. The system-address generator appends seven NeverC pseudos while
retaining the earlier opcode identities; the full fork also owns the i386
address-size pseudos.

[tests/verify-descriptor-address-completion.py](tests/verify-descriptor-address-completion.py) checks all segment/width
combinations, native operand bytes, register and memory ownership, independent
result/ZF uses, failed-read priors, repeated/dead calls, live inputs and saved
registers. It covers O0/O2 and all three object formats with the same direct
LLVM, NeverC and NeverC-to-fork routes as the other completion harnesses.
Linux C routes compare real default/GS queries against raw-byte oracles,
including nonzero GS bases and narrow offsets with different high bits.
LKGS CPL3 faults are compared separately; these do not qualify successful
privileged descriptor loading.

Generated C containing flag snapshots can use header-free `__readeflags` and
`__writeeflags` on all three x64 object formats. NeverC's C23 parser also
accepts the already-advertised `__builtin_bit_cast(type, expression)`: it
validates equal object sizes, evaluates the source once and reinterprets its
representation through the existing scalar/vector/aggregate cast machinery.
Invalid, incomplete, variable-sized, atomic or array destination types fail
clearly. Tests cover constant initializers, signed values, floating-point bit
patterns, aggregates, vectors and wide integers. Array sources retain their
object representation instead of decaying to pointers; their address is
evaluated once. Volatile arrays use an explicit volatile aggregate copy.

Qualification passes 4,704 full-LLVM encoding/completion checks and 2,928
checks on each C route. Each C route also passes 96 real descriptor queries
and 112 LKGS CPL3 fault-boundary comparisons. All 61 focused frontend tests,
81 binary checks and 29 generator tests pass. The old descriptor-query matrix
passes 1,174 controlled checks and 25 real queries. The target schema preserves
all 387 registers and 18,503 previous opcode records, appending seven opcodes
for a total of 18,510; both target schemas pass regeneration checks.

### TSX (xbegin, xabort, xend, xtest)

- **xbegin**: The XBEGIN pseudo's custom inserter expands the transaction
  entry and fallback blocks. Lowering returns the i32 transaction status and
  effect chain; `_xbegin` exposes that status as `unsigned int`. The custom
  inserter produces `_XBEGIN_STARTED` on success and the hardware abort status
  on the fallback path.

- **xabort**: Takes a single i8 immediate operand.

### MCInstrDesc NumDefs

If an instruction's `NumDefs` field in `X86GenInstrInfo.inc` is wrong, it
causes segfaults during instruction emission. Example: VMWRITE64rr had
`NumDefs=1` (wrong — vmwrite has no register output). Fixed to `NumDefs=0`.

## Native REP String Operations

The Microsoft `__movs*`, `__stos*`, `__in*string` and `__out*string` builtins
and the 14 `__builtin_ia32_*` counterparts emit native LLVM intrinsics and
one REP instruction. The dword move spelling is
`__builtin_ia32_movsd_rep` / `llvm.x86.movsd.rep`, to distinguish it from
scalar floating-point MOVSD. MOVS accepts a const source pointer; its builtin
type must qualify the source element, not the preceding destination pointer.

Memory-string counts are 64-bit in NeverC. String port I/O uses an unsigned
32-bit count, zero-extended into RCX. These operations consume ambient DF;
they do not introduce CLD/STD or change the platform ABI's DF requirements.
The hardware instruction owns zero-count behavior, element order, overlapping
accesses and fault/restart behavior. The API does not implement memcpy or
memset, and does not expose partially completed machine state as a C return.

`sync-x86-system-effects.py` maintains the pointer/count definitions, DF use,
accumulator width and memory effects in the pruned instruction table. DF is a
reserved architectural register, so the machine verifier can represent its
ambient input. Port operations additionally have unmodeled I/O side effects.
The full LLVM fork supplies the matching 64-bit APIs plus 32-bit-code-mode
memory-string APIs with a `32` suffix; a mismatched code mode is rejected.

[tests/verify-string-completion.py](tests/verify-string-completion.py) runs MOVS/STOS in Unicorn against an
independent sequential byte oracle. It covers O0/O2, every element width,
zero/one/seven elements, both DF values, unaligned overlap, zero-count unmapped
pointers, register preservation, and ordinary memory before/after the call.
Controlled completions cover port I/O, full-width pointers/counts and garbage
upper bits above the i32 port count. DF is changed at the instruction boundary
and restored before C resumes. These checks do not execute host port I/O or
qualify hardware fault/restart behavior.

Run the script normally for NeverC, with `--spelling microsoft` for the
Microsoft names, with `--llc /path/to/llc` for full-fork 32/64-bit IR, or with
`--backend-llc /path/to/llc` for NeverC IR through the full fork. The current
matrix has 544 cases per 64-bit route and 1,000 for the full fork.

### Explicit REP state

`__NEVERC_X86_REP_STATE__ == 1` advertises seven additional builtins:
`__builtin_ia32_rep_{movs,stos,lods,cmps,scas,ins,outs}_state`. They expose
full 64-bit input/output registers, an explicit `unsigned char` DF input,
constant element bytes and address bits, a constant source segment for
MOVS/LODS/CMPS/OUTS, and a constant REPE/REPNE selector for comparisons.
They lower to `llvm.x86.rep.*.state`, without inline assembly. Their result
is `void`; the last argument is an `unsigned long long *` state array,
followed by an additional `unsigned short *` flags output for comparisons.

| Operation | First three inputs | State array order |
| --- | --- | --- |
| MOVS | RSI, RDI, RCX | RSI, RDI, RCX |
| STOS | RDI, RCX, RAX | RDI, RCX |
| LODS | RSI, RCX, RAX | RSI, RCX, RAX |
| CMPS | RSI, RDI, RCX | RSI, RDI, RCX |
| SCAS | RDI, RCX, RAX | RDI, RCX |
| INS | RDI, RCX, port | RDI, RCX |
| OUTS | RSI, RCX, port | RSI, RCX |

Each initial register is `unsigned long long`; the port is `unsigned short`.
After these three inputs comes DF (zero forward, nonzero backward), then
incoming `unsigned short` flags for comparisons only, then the constant
controls: element bytes, address bits, source segment if applicable, and
repeat selector if applicable. Element bytes are 1/2/4/8 (INS/OUTS: 1/2/4);
address bits are 32/64; source segment is 0 or the architectural prefix byte;
repeat is 0 for REPE or 1 for REPNE. All controls must be compile-time constants.

Comparison flags use SF/ZF/AF/PF/CF in the LAHF high byte and OF in bit zero,
masked by `0xd501`. CMPS/SCAS require `target("sahf")`; the capability macro
does not enable CPU features. Input flags are restored before the instruction,
including for zero count and architectural fault restart. The actual hardware
instruction owns accesses and partial-register updates; do not pre-truncate
register inputs for address overrides. Normal completion restores ABI DF=0.
The DF window is one backend pseudo, with no compiler-inserted spills or calls.

These APIs expose completed registers, not a complete guest exception ABI:
non-string registers can be compiler scratch, and no result-array stores take
place until the operation returns. An OS fault handler may resume the native
instruction. Port I/O still requires the architectural privilege/permission.
NeverC supports 64-bit code only. The full LLVM fork also supplies `state32`
interfaces for 32-bit code and 16/32-bit addresses.

`sync-x86-rep-state.py` owns the pseudo operands, implicit GPR/DF definitions
and unknown memory effects; `sync-x86-native-intrinsics.py` owns signatures
and immarg positions. Both are checked in generated-table CI.

[tests/verify-rep-state.py](tests/verify-rep-state.py) compares the emitted operations with independently
encoded hardware REP instructions and a sequential memory/flag oracle. On
Linux x86-64 with SAHF, each O0/O2 route covers 45,152 memory/flag cases, including
all six incoming comparison flags, source segments, direction, overlap, early
termination and address overrides. It also runs 112 real protected-page fault
restarts. Another 192 port tests inspect the CPL3 fault context and inject a
controlled completion; they do not execute physical port I/O. The raw reference
is important: for example, zero-count addr32 MOVS and LODS need not have the same
high-register write behavior.

Run with `--llc /path/to/llc` for full-fork IR, without it for NeverC, or with
`--backend-llc /path/to/llc` for NeverC IR through the full fork. Add
`--llc /path/to/llc --emulate-32` to check 8,928 memory cases and 2,304 controlled
port cases in 32-bit Unicorn, including 16-bit addresses and preserved high
register halves. That 32-bit result is emulator coverage, not hardware fault
restart qualification. Dependencies are `capstone`, `lief`, and `unicorn` for
the emulator route, plus a host C compiler for native execution.
All routes also reassemble both syntax dialects and compare function bodies
with direct object emission. The comparison matches the compile driver's
branch-relaxation policy and excludes inter-function alignment padding.

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

### Native AMX compute and MOVRS

The physical tile interface now includes `__builtin_ia32_tdpbf8ps`,
`tdpbhf8ps`, `tdphbf8ps`, `tdphf8ps`, `tmmultf32ps`, `tileloaddrs64` and
`tileloaddrst164` (all with the same `__builtin_ia32_` prefix). Their native
LLVM signatures match the full fork. Compute operations take three distinct
constant tile numbers in `[0, 7]`. Loads take a constant tile number, a const
linear pointer and a full-width byte stride. The compiler preserves the
instruction-owned memory accesses and architectural tile state; it does not
preload memory or implement the instruction with inline assembly.

`<immintrin.h>` also exposes the standard physical `_tile_*` entry points
through `amxintrin.h`, including configuration, loads/stores, compute and row
operations. These wrappers call the native builtins; they introduce no inline
assembly or managed-tile conversion. For example, `_tile_loaddrs(0, base,
stride)`, `_tile_dpbf8ps(0, 1, 2)` and `_tile_cvtrowd2ps(0, row)` use the same
contracts as their corresponding builtins. The header ships in the compiler's
resource directory and the installed x86 resource-header component.

The feature names are `amx-fp8`, `amx-movrs` and `amx-tf32`, each implying
`amx-tile`. They work in function target attributes and through `-mamx-fp8`,
`-mamx-movrs` and `-mamx-tf32`, with matching `-mno-` options. The last command-line
enable/disable option controls both builtin availability and feature macros.
TF32 retains the fork's historical instruction support, rather than
claiming that withdrawn instruction is present in current processors. The
physical tile bank must not be mixed in one function with compiler-managed
`x86_amx` values. Both compilers reject this before instruction selection,
including release builds; otherwise an allocator-generated tile configuration
can invalidate the physical operation. Out-of-range physical tile indices also
receive a backend diagnostic with assertions disabled.

[utils/lint/sync-x86-amx-instructions.py](utils/lint/sync-x86-amx-instructions.py) owns the pruned AMX descriptors,
operand metadata, native encodings, both assembly writers/matchers, compression
pairs and appended feature bits. Existing opcode and feature IDs stay stable.
Assembly writers have different mnemonic-offset bit widths, which must not
overwrite their operand-printing commands. EGPR target functions can compress
eligible tile loads to VEX when their allocated address registers fit; explicit
EVEX assembly retains its requested encoding. These pointer intrinsics do not
provide an API for forcing an encoding or choosing segment/address width.

[tests/verify-amx-native-completion.py](tests/verify-amx-native-completion.py) checks all 336 distinct tile triples for
each compute operation, all load destinations, O0/O2, ELF/COFF/Mach-O and both
assembly dialects. It also checks forced EVEX assembly with extended GPRs and
controlled completions with high pointers/strides, repeated operations, tile
bank inputs/outputs, ordinary memory ordering and saved C registers. The full
fork, NeverC and NeverC-to-fork routes each cover 10,794 checks. Controlled
completions inject architectural results; they do not qualify hardware AMX
arithmetic, configuration validity, cache hints, faults or restart behavior.
Use `--llc` or `--backend-llc` to select those routes, and `--keep` for artifacts.

### Native AMX row results

The physical tile interface also provides twelve AMX-AVX512 builtins. Each
name below has the `__builtin_ia32_` prefix. A trailing `i` selects an immediate
row; the other form consumes all 32 bits of an unsigned integer row operand.

| Register-row / immediate-row names | Result |
|---|---|
| `tcvtrowd2ps` / `tcvtrowd2psi` | 16 floats |
| `tcvtrowps2bf16h` / `tcvtrowps2bf16hi` | 32 bfloat16 values |
| `tcvtrowps2bf16l` / `tcvtrowps2bf16li` | 32 bfloat16 values |
| `tcvtrowps2phh` / `tcvtrowps2phhi` | 32 float16 values |
| `tcvtrowps2phl` / `tcvtrowps2phli` | 32 float16 values |
| `tilemovrow` / `tilemovrowi` | 16 integers |

Arguments are a constant tile index in `[0, 7]` and a row operand. NeverC
rejects nonconstant or out-of-range immediate rows instead of truncating them;
all encoded imm8 values in `[0, 255]` remain available. The architectural
meaning of row bits belongs to the instruction, including values outside a
configured tile. The compiler preserves dynamic row bits and all 512 result
bits. These are side-effecting accesses to physical tile state, including when
the result is discarded. Mixing physical rows with managed `x86_amx` values is
rejected by the same ownership check used for physical compute instructions.

Both `amx-avx512` and `avx10.2` are required, through a function target attribute
or `-mamx-avx512 -mavx10.2`. Matching `-mno-` switches disable the feature.
The target macros are `__AMX_AVX512__`, `__AVX10_2__` and
`__AVX10_2_512__`. In this backend AVX10.2 implies the existing AVX10.1 512-bit
feature set; this addition qualifies the row APIs, not every AVX10.2 opcode.
The full LLVM fork accepts the older `avx10.1-256` IR feature spelling so
NeverC's implied feature list crosses the compiler boundary without warnings.

[tests/verify-amx-row-completion.py](tests/verify-amx-row-completion.py) covers all physical tiles, every encoded
imm8, all 32 ZMM and row GPR numbers, O0/O2, ELF/COFF/Mach-O and both assembly
dialects. Controlled completions check repeated reads, full-width dynamic
indices, four vector element types, ordinary memory ordering and saved C
registers. A 32-result pressure case also checks spill/reload flow and Win64
nonvolatile XMM preservation. Unicorn executes scalar instructions; the harness handles vector
moves and injects distinct 512-bit row results. This qualifies compiler
operand/result flow, not AMX arithmetic, hardware faults or restart behavior.
The full fork, NeverC and NeverC-to-fork routes each run 20,570 checks. Select
those routes with `--llc` or `--backend-llc`, and retain artifacts with `--keep`.

Segment/address-width and forced-encoding APIs, plus complete state-transition
contracts, remain separate work. Native object generation alone does not
establish those contracts.

## Testing

### Explicit physical AMX addresses and row registers

Thirteen additional builtins expose the fork's exact native controls:
`__builtin_ia32_{ldtilecfg,sttilecfg}_address(offset, bits, segment, encoding)`,
`__builtin_ia32_{tileloadd64,tileloaddt164,tileloaddrs64,tileloaddrst164,tilestored64}_address(tile, offset, stride, bits, segment, encoding)`,
and `__builtin_ia32_{tcvtrowd2ps,tcvtrowps2bf16h,tcvtrowps2bf16l,tcvtrowps2phh,tcvtrowps2phl,tilemovrow}_reg(tile, row, extended)`.
The resource header supplies matching `_tile_*_address` and `_tile_*_reg`
macros. The row result vector types match the existing typed row interfaces.

Offsets and strides are unsigned 64-bit inputs; address bits are constant
32 or 64 and truncate both before hardware segment semantics. Segment is zero
or an actual override byte (`0x26/2e/36/3e/64/65`). Encoding zero forces VEX;
one forces EVEX. Row `extended` zero forces a legacy GPR and one forces R16D.
Tiles, address controls and register-class controls are constant and validated;
all 32 dynamic row bits are preserved. EVEX/extended-register forms require
`egpr`, memory needs `amx-tile` (plus `amx-movrs` for MOVRS loads), and rows
need `amx-avx512,avx10.2`. APIs keep unknown memory and ambient-bank effects,
including dead results and native faults/restart. They reject managed tiles
in the same function and do not synthesize configurations or ordinary tile
memory accesses.

`__NEVERC_X86_AMX_CONTROLS__ == 1` advertises this compiler API independently
of active CPU features. This lets source generators select native builtins
and then attach the exact required function target attributes. It does not
set `__AMX_TILE__` or other CPU availability macros. `__has_builtin` alone is
insufficient at baseline because target builtins require active CPU features.

`sync-x86-native-intrinsics.py` owns signatures and constant operands;
`sync-x86-amx-native-controls.py` appends two encoding-preserving pseudos.
MC lowering preserves forced VEX/EVEX in object bytes and both assembly
dialects. The physical row pseudo explicitly permits extended GPRs at machine
verification. The x86 schema grows from 18,538 to 18,540 opcodes; all prior
opcode entries, 391 registers and 190 features remain identical, as does the
AArch64 schema.

[tests/verify-amx-explicit-controls.py](tests/verify-amx-explicit-controls.py) passes 11,136 checks on each of the full
LLVM, NeverC and NeverC-to-LLVM routes: all tiles/widths/segments/encoding
classes at O0/O2 in ELF/COFF/MachO, ATT/Intel round trips, 32-bit wrap, full
strides, repeated effects, unknown-memory ordering, complete 512-bit row
results and Win64 nonvolatile GPR/XMM preservation under pressure. The harness
executes scalar code and interprets exact REX2 MOV/vector-move setup while
injecting AMX completions. It does not qualify hardware arithmetic, native
exceptions or restart. See the fork's `X86AMXNativeControls.rst` for the IR API.

The end-to-end NeverD object check also covers constant zero selectors: its
O2 copy folding materializes R16D through `MOV32r0`. NeverC now permits EGPRs
for this pseudo, matching the full fork's REX2-capable XOR expansion. The
constant-row regression covers zero, one and high-bit values for all six row
operations in three object formats at O0/O2. This batch passes 68 privileged
builtin tests, 33 generator regressions and 221 target/plugin tests.

### GTest suite ([`tests/neverc/X86PrivilegedIntrinTests.cpp`](tests/neverc/X86PrivilegedIntrinTests.cpp))

68 tests cover native instruction emission, semantic regressions, builtin
type/selector diagnostics and the absence of inline assembly. The
`compileToAsm()` helper enables the machine verifier for the Windows x64
target; tests exercise O0/O2 where optimization affects the contract.

### Unicorn binary verification ([`tests/verify-x86-intrin-binary.py`](tests/verify-x86-intrin-binary.py))

81 standalone tests. Requires: `pip install unicorn capstone lief`

**Pipeline:**

1. Compile to COFF `.o` via `ncc --target=x86_64-pc-windows-msvc -O2 -fno-lto`
2. Extract `.text` bytes via `lief`
3. Disassemble via `capstone` to locate the target instruction
4. Emulate via `unicorn` and verify register/memory state

**Two emulation modes:**

- **Priv-insn mode:** `emu_start(func_entry, priv_insn_addr)` — executes all
  preceding instructions and stops *before* the privileged instruction. Then
  snapshots all registers and runs the checker. This validates that the ISel
  CopyToReg chain correctly set up implicit operands (e.g., DX=port, RDI=buf,
  RCX=count) before the instruction would execute.

- **Full-emulation mode:** Runs the entire function to completion, then checks
  RAX (return value) and/or memory contents. Used for GS/FS segment ops where
  no privileged trap occurs — the instructions execute normally in Unicorn and
  we verify end-to-end semantics.

**Verification tiers (each tier catches a distinct class of bugs):**

| Tier | What it checks | What it catches |
|------|---------------|-----------------|
| Instruction existence | Capstone mnemonic match | Missing intrinsic lowering, wrong instruction entirely |
| Machine code bytes | Capstone disassembly of raw `.text` bytes | Wrong TSFlags / opcode encoding (e.g., `f3 a4` movsb vs `f3 6c` insb) |
| Register state | Unicorn register snapshot at priv insn | Wrong CopyToReg target, missing ZERO_EXTEND, swapped operands |
| Full emulation | RAX return value / memory after execution | End-to-end semantic correctness (GS/FS reads/writes) |

**Coverage:** 60/81 tests have register-level or value-level verification.
The remaining 21 are no-arg instructions (cli, sti, wbinvd, vmxoff, etc.),
pure reads that trap before producing output (readcr*), or TSX ops with no
input registers — these have no meaningful register state to check.

**Known limitations and design tradeoffs:**

1. **`-fno-lto` is required** to get native object code (LTO emits bitcode).
   This means the test validates the non-LTO codegen path. However, ISel
   lowering is per-function and deterministic — LTO's cross-module
   optimization does not change intrinsic lowering patterns. The PGO/LTO
   segfault we encountered was caused by UB in the ISel code itself (wrong
   operand count in `getMachineNode`), which manifests identically in both
   paths; the difference was that PGO/LTO's memory layout made the UB crash
   instead of silently producing wrong output.

2. **`find_insn` matches the first Capstone mnemonic.** For unique mnemonics
   like `rep insb`, `wrmsr`, or `invlpg` this is unambiguous. For the common
   mnemonic `mov` (used by CR/DR ops), it depends on `-O2` generating minimal
   code (e.g., `mov cr3, rcx; ret` with no preceding `mov`). This is reliable
   today because each test wraps a single intrinsic call in a trivial function,
   and the backend at `-O2` does not insert unnecessary register shuffles for
   one-instruction bodies. If this assumption ever breaks, the fix is to switch
   to op-string matching (e.g., check `op_str` contains `cr3`), not to
   rewrite the emulation strategy.

3. **Unicorn emulates user-mode (ring 3).** Privileged instructions (mov cr,
   in/out, cli, etc.) fault when executed, which is why we stop *before* them.
   We cannot verify the instruction's own side effects (e.g., CR3 actually
   changes). What we *can* verify is that the compiler set up the correct
   register inputs — if the registers are right, the instruction will do the
   right thing on real hardware.

4. **Cross-compilation on macOS ARM64.** The test compiles for
   `x86_64-pc-windows-msvc` and emulates x86-64 via Unicorn. This validates
   the cross-compilation backend. It does not validate execution on real
   Windows x64 hardware, but for privileged instructions that require ring 0,
   there is no user-mode test possible anyway.

Windows x64 ABI: args in RCX, RDX, R8, R9.

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

## Native compact-memory interfaces

`__NEVERC_X86_COMPRESS_EXPAND__=1` advertises 36 x86-64 builtins:
`__builtin_ia32_vp{compress,expand}{b,w,d,q}{128,256,512}_native` and
`__builtin_ia32_v{compress,expand}{ps,pd}{128,256,512}_native`.
A 128/256/512-bit operand is an unsigned-int vector of 4/8/16 elements,
regardless of the instruction's element kind. These are raw-bit carriers.

Stores take `(void *offset, unsigned long long mask, vector source,
const unsigned segment)`. Loads return a vector and take `(const void *offset,
unsigned long long mask, vector prior, const unsigned zeroing,
const unsigned segment)`. Zeroing is 0 or 1. Segment is zero or an override
byte `0x26/0x2e/0x36/0x3e/0x64/0x65`; the pointer is a segment-relative
offset. The backend uses the DS-default RBX carrier. An SS prefix in long
mode does not preserve the original RSP/RBP address's #SS fault provenance.

Functions need `avx512f`, plus `avx512vl` for 128/256-bit widths.
Byte/word forms also need `avx512bw,avx512vbmi2`. The C frontend enables
512-bit EVEX through `avx512f`; LLVM IR must retain `+evex512` for 512-bit
operations. For example:

```c
typedef unsigned int U32x4 __attribute__((vector_size(16)));
__attribute__((target("avx512f,avx512vl")))
U32x4 expand(const void *offset, unsigned long long mask, U32x4 prior) {
  return __builtin_ia32_vpexpandd128_native(offset, mask, prior, 0, 0);
}
```

Six fixed-signature LLVM native interfaces carry these operations; constant
kind selectors preserve PS/PD versus D/Q opcodes. Native calls conservatively
retain memory effects, including zero-mask calls and unused load results.
They do not use inline assembly or generic masked loads/stores. The full LLVM
fork also supports i386; NeverC's builtin registry remains x86-64 only.
`X86CompressExpandTest.*` checks frontend controls and O0/O2 output in
ELF/COFF/Mach-O and both assembly dialects. Hardware AVX-512/fault execution
requires separate qualification.

## Native four-register arithmetic interfaces

`__NEVERC_X86_FOUR_SOURCE__=1` advertises the six x86-64 builtins
`__builtin_ia32_{v4fmaddps,v4fnmaddps,v4fmaddss,v4fnmaddss,vp4dpwssd,vp4dpwssds}_native`.
Each returns a raw unsigned-int vector and takes `(const void *offset,
vector accumulator, vector source0, vector source1, vector source2,
vector source3, unsigned short mask, const unsigned zeroing,
const unsigned segment)`. Scalar SS vectors contain four elements; the
remaining vectors contain 16. Zeroing and segment use the checked constant
controls described above. The scalar result retains accumulator bits 127:32.

The frontend requires `avx512f` for these register carriers; execution requires
the actual CPU's AVX512_4FMAPS or AVX512_4VNNIW capability and enabled extended
state. These builtins neither dispatch at runtime nor imply that every
AVX512F processor supports the instructions.

All four source values enter fixed-signature LLVM intrinsics. The backend
materializes the aligned XMM4..7 or ZMM4..7 block and records the unencoded
register uses so optimization cannot discard its other members. Native
instructions retain each arithmetic, rounding and saturation step, mask
suppression, segment addition and memory completion. No source-level assembly
or ordinary arithmetic replaces them. Generated intrinsic IDs, signatures and
DAG references remain owned by `sync-x86-native-intrinsics.py`.

`X86FourSourceTest.*` checks constant diagnostics and O0/O2 native output across
ELF/COFF/Mach-O and both assembly dialects. Physical Xeon Phi execution and
hardware exception behavior remain outside this host qualification.

## Native ADX carry chains

`<immintrin.h>` and `<x86intrin.h>` expose `_adcx_u32`, `_adcx_u64`,
`_adox_u32` and `_adox_u64`. Their `__builtin_ia32_*` forms require the `adx`
function target. Each takes `(unsigned char carry, word a, word b, word *sum)`,
stores the low word, and returns an unsigned carry byte. Every nonzero input
byte supplies one. ADOX's output is unsigned carry, even though the instruction
uses OF; it is not signed overflow.

The matching NeverSight LLVM intrinsics are `llvm.x86.adcx.32/.64` and
`llvm.x86.adox.32/.64`, with the fixed signature
`{i8, iN} (i8 carry, iN a, iN b)`. They have no memory effects. The native
lowering seeds the selected flag, emits ADCX or ADOX, and captures its carry
in one scheduling group. It requires ADX, supports 32-bit operands in i386
and x86-64 and 64-bit operands in x86-64, and uses no inline assembly or
external helpers. Carry chains are explicit SSA values; callers do not own
ambient EFLAGS across C expressions or calls. Scheduling simultaneous live
hardware CF/OF chains without reseeding is a separate optimization.

`_addcarryx_u32/u64` now use the native ADCX API. `_addcarry_u32/u64` and the
existing `__builtin_ia32_addcarryx_*` compatibility spelling retain the generic
addcarry contract and work without ADX. This preserves the baseline header
while allowing explicit selection of both native carry instructions. The
native intrinsic synchronizer keeps the pruned LLVM enum, signatures,
attributes and instruction-selection IDs consistent with the full fork.

`X86ADXTest.*` covers headers and function targets at O0/O2 for ELF, COFF and
Mach-O, native instruction selection, missing-feature diagnostics, all 256
input carry bytes, aliases, single argument evaluation and interleaved CF/OF
chains against independent bit addition. Native execution requires an ADX CPU;
NeverC itself still supports only x86-64/AArch64 targets. Independent execution
through both NeverC and the full LLVM fork passed 1,187,840 value/chain cases
at O0/O2, with six ELF/COFF/Mach-O compilation profiles. The full fork's
standalone value APIs additionally passed 740,352 x86-64 cases and 65,536
native i386 cases; its source change is
[`5b7938a7dea3`](https://github.com/NeverSight/llvm-project/commit/5b7938a7dea34d00a705a44c6dad43321e11f3e9).
These APIs are present in the fork source; this change does not publish a new
NeverD LLVM SDK release.

## Standard transaction headers

`<immintrin.h>` and `<x86intrin.h>` expose `_xbegin`, `_xend`, `_xabort`
and `_xtest`, together with the standard transaction-status constants. The
installed `rtmintrin.h` and `xtestintrin.h` retain the LLVM source license.
When a Microsoft spelling is already a frontend builtin, the header declares
that interface; otherwise its wrapper uses the existing `__builtin_ia32_*`
operation. Both forms emit native LLVM intrinsics, including XABORT's constant
byte, without inline assembly or external helper calls.

`X86TransactionTest.TransactionHeadersExposeNativeInterfaces` compiles
both umbrella headers at O0/O2 for ELF, COFF and Mach-O, checks native IR and
assembly, and includes `<intrin.h>` in Microsoft mode to cover declaration
compatibility. Callers use the `rtm` function target attribute. These are
compiler interfaces; the tests do not claim physical transactional execution
or recovery of an arbitrary binary's original abort continuation.

The Microsoft `_xabort` builtin also requires a constant when used without an
umbrella header. A variable argument previously reached instruction selection
and could silently become a fixed abort byte. The frontend now diagnoses it
before IR emission. The native metadata synchronizer also checks XABORT's
existing `immarg` contract; a backend regression rejects variable IR operands
while retaining unknown memory effects and the instruction's possible normal
completion outside a transaction.

[tests/verify-transaction-encoding.py](tests/verify-transaction-encoding.py)
checks all 256 abort bytes at O0/O2 in
ELF, COFF and Mach-O. It checks each native IR call, exact `C6 F8 imm8` object
encoding, absence of helper calls/assembly, and the optimized store/abort/load
order. Select `--compiler build-neverc/bin/neverc`, or the full fork with
`--llc /path/to/llc`; both require `--objdump /path/to/llvm-objdump`. The full
fork also supports `--mode 32`. `--backend-llc /path/to/llc` sends NeverC's IR
through the full backend, and `--keep DIRECTORY` retains the inputs, outputs
and JSON results. The qualified matrix has 40 profiles and 10,240 functions:
14 NeverC, 14 NeverC-to-full-LLVM, and six full-LLVM profiles per code mode.
It does not execute physical RTM regions.

Transaction frontend tests live in
[TransactionFrontendTests.cpp](tests/neverc/X86/TransactionFrontendTests.cpp);
the shared x86 assembly fixture is `X86/NativeBuiltinFixture.h`. Native IR
operand/effect tests stay in `X86/TransactionTests.cpp`.

### TBM header interfaces

The installed [tbmintrin.h](neverc/lib/Headers/tbmintrin.h), also exposed by
`<x86intrin.h>`, provides both widths of `__bextri`, `__blcfill`, `__blci`,
`__blcic`, `__blcmsk`, `__blcs`, `__blsfill`, `__blsic`, `__t1mskc` and
`__tzmsk`. Callers can use `__attribute__((target("tbm")))` on the function
without a translation-unit-wide target flag, including in Microsoft mode.
The header retains its LLVM license and records its import revision.

BEXTR uses the existing native LLVM intrinsic and requires a constant control
operand. The nine arithmetic operations use LLVM's existing instruction
selection patterns: O2 emits native TBM; O0 may retain ordinary arithmetic.
Neither route needs inline assembly or an external helper. These C interfaces
return values and do not expose machine flags. NeverD owns the original
instruction's CF/ZF/SF/OF and undefined AF/PF semantics separately.

[TBMFrontendTests.cpp](tests/neverc/X86/TBMFrontendTests.cpp) compiles all
20 interfaces at O0/O2 for ELF, COFF and Mach-O, checks native O2 assembly,
checks BEXTR IR, and rejects variable immediate controls. It does not execute
physical TBM instructions.

### BMI and BMI2 header function targets

`<x86intrin.h>` and `<immintrin.h>` expose their BMI/BMI2 interfaces in
NeverC's Microsoft mode without requiring translation-unit feature macros.
The wrappers retain their `target("bmi")` or `target("bmi2")` attributes;
callers can enable the family on individual functions. The older header
conditions hid these interfaces even from such callers on Windows.

[BMIFrontendTests.cpp](tests/neverc/X86/BMIFrontendTests.cpp) compiles 22
interfaces at O0/O2 for ELF, COFF and Mach-O, including all three BEXTR
spellings with dynamic controls. BEXTR, BZHI, PDEP and PEXT retain native LLVM
intrinsics. ANDN and BLS operations use ordinary arithmetic patterns and
select their native instructions at O2. MULX retains both product halves
without a runtime helper; its 32-bit form may use an ordinary 64-bit multiply.
The suite rejects inline assembly and does not require BMI hardware.

The eight `_rorx_u32/u64`, `_sarx_i32/i64`, `_shlx_u32/u64` and `_shrx_u32/u64`
interfaces now have BMI2 builtins and definitions in `<immintrin.h>` (also
reachable through `<x86intrin.h>` and Windows `<intrin.h>`). Their previous
Windows-only declarations emitted unresolved external calls. RORX takes a
constant imm8 in 0..255 and uses LLVM funnel shifts; the variable shifts mask
the count to five or six bits before emitting integer shifts. Negative
arithmetic-shift inputs and counts at or above the width have defined machine
semantics. Arguments are evaluated once and function-level BMI2 is required.

The frontend tests verify all eight native instructions at O0/O2 in ELF, COFF
and Mach-O and diagnose missing BMI2, dynamic RORX controls and out-of-range
immediates. LLVM may canonicalize a right rotation to an equivalent left
funnel shift. An independent `llc` O0 FastISel profile can select equivalent
ordinary shifts; the full SelectionDAG selector generates BMI2. Neither path
requires inline assembly or a new LLVM target intrinsic.
Native regression tests compare both widths and every imm8 rotation against
bit-loop references, including high shift-count bits, negative arithmetic
inputs and argument evaluation counts. Cross-format qualification covers
x86-64 with 32/64-bit operands; the existing i686 frontend target remains
unsupported. Separate NeverC and fork-LLVM O0/O2 objects passed 2,072,384 value
comparisons across these eight interfaces plus MULX, PDEP and PEXT.

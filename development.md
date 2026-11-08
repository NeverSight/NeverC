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

## Native system-return transfers

The four C23 builtins below return `void` and terminate the current function.
They emit native LLVM intrinsics and CPU instructions, with no inline assembly
or runtime helper. The suffix is the operand width; NeverC exposes these APIs
on x86-64.

| Builtin after `__builtin_ia32_` | Arguments |
| --- | --- |
| `sysret32` | `unsigned pc, unsigned flags, unsigned long long stack` |
| `sysret64` | `unsigned long long pc, flags, stack` |
| `sysexit32` | `unsigned pc, unsigned stack` |
| `sysexit64` | `unsigned long long pc, stack` |

SYSRET transports PC/flags through ECX/R11D or RCX/R11 and copies the supplied
stack to RSP immediately before the native instruction. Its final stack switch
stays in one machine pseudo until MC output: machine scheduling must never move
it before a compiler-frame access. SYSEXIT uses EDX/ECX or RDX/RCX and installs
the new stack itself. Narrow arguments promise only their low 32 input bits.
SYSEXIT uses the flags present at its instruction boundary; evaluating C
arguments and the preceding compiler code can change those flags.

The current compiler frame is abandoned, with no ABI epilogue or ordinary
return. Other registers remain compiler-selected. These interfaces do not
restore a complete saved context or promise asynchronous unwinding across the
stack switch. MSRs, CPL, CPU features and CET remain ambient state, and hardware
owns admission checks, segment installation, flag filtering and faults. Legacy
GPR high halves after a compatibility transition are architecturally undefined.
The intrinsics are convergent, noduplicate and noreturn, with unknown memory
effects; source after a builtin is unreachable.

`SystemReturnFrontendTests.cpp` checks native identity, nonreturning control,
dynamic frames and malformed arguments at O0/O2 on ELF, COFF and Mach-O.
`sync-x86-system-returns.py` owns the pruned stack-carrier records; its checks
also reject accidental ordinary-return metadata and retain existing opcode IDs.
The full LLVM fork additionally supports SYSEXIT32 in protected 16/32-bit
code and rejects SYSRET/SYSEXIT64 outside 64-bit code. Code16 retains the
32-bit compiler stack ABI; it is not real-mode support.

Run the independent Linux x64 privileged oracle with:

```bash
python3 tests/neverc/X86/verify_system_returns.py \
  --llvm-bin /path/to/llvm/build/bin --neverc build-neverc/bin/neverc \
  --output /path/to/artifacts
```

It executes direct LLVM, NeverC, and NeverC IR through LLVM at O0/O2. Each
profile tests 16 plain/fixed/dynamic-frame/register-pressure functions, five
selector bases, 68 flag inputs, four stack values and four size/address combinations. At the
actual instruction boundary it checks input transport and then compares all
GPRs, flags, PC, hidden CS/SS caches, remaining system registers and every mapped
memory byte with an independent original-byte vCPU. A deliberately corrupted
stack move must fail the operand check. Canonical high PC aliases and stacks,
unaligned stacks and narrow argument high halves are included. KVM access is
required; unavailable hardware is not a pass. This fixture disables CET and
does not qualify faults, asynchronous events or other CPU models. Add `--i386`
to run the two LLVM profiles in native protected 32-bit mode: four SYSEXIT32
functions and 21,760 complete state pairs per profile, with an independently
corrupted ECX move rejected. Add `--code16` instead to run the same LLVM
profiles from a 16-bit code segment, retaining a 32-bit stack segment and ABI.
The disassembler and negative control use the selected code width. Neither
option enables an i386/code16 frontend in NeverC; its builtins retain their
x86-64 target. The contract follows [Intel SDM SYSRET/SYSEXIT and section
3.4.1.1](https://cdrdv2-public.intel.com/835781/325462-sdm-vol-1-2abcd-3abcd-4.pdf).

## Native far calls and terminal jumps

Ten C23 builtins now expose the LLVM fork's native far-transfer interfaces.
`__builtin_ia32_lcall16/32/64(const void *target)` returns an unsigned short,
unsigned int or unsigned long long from AX/EAX/RAX. The target is a packed
native-width offset followed by a u16 selector. The two `lcall16/32_imm`
forms instead take constant offset and selector arguments and retain the 9A
encoding. The memory forms retain FF /3; all pointer reads, descriptor checks
and faults remain inside the native instruction.

These are zero-argument returning far entries. The destination must preserve
the platform C callee-saved registers, return with RETF, and restore the exact
caller CS/SS/SP and ordinary C ABI environment. Call gates must copy zero
parameters. Their frame width comes from the gate rather than the intrinsic
suffix. The actual native return frame must be able to represent the
generated continuation offset, including narrow direct calls in code64. The
backend uses a real call sequence, a platform call-preserved mask and Win64
outgoing home space, and forbids leaf red-zone allocation. It does not silently
promise preservation of arbitrary original-image registers.

`__builtin_ia32_ljmp16/32/64(const void *target, const void *stack)` is
nonreturning. `ljmp16/32_imm(offset, selector, stack)` supplies constant target
fields. A terminal carrier sets ESP/RSP immediately before the native far
jump, after all compiler-frame accesses; its target-address operand excludes
the stack register. No ordinary epilogue follows. The compiler does not
promise values for other registers or an asynchronous unwind contract.

Immediate forms require a legacy target, while 64-bit operands require a
long-mode target. Memory 16/32-bit operands are available in both. Linux i386
source uses `-fllvm-backend=/path/to/full-fork/llc`; x64 uses the embedded
backend. No inline assembly or runtime helper implements these APIs.
`X86NativeFarTransfers.def` owns backend identities and drives intrinsic
schema synchronization. `X86NativeFarTransfer.h` ports the full fork's
memory-form lowering and explicitly rejects legacy-only records in the pruned backend;
`sync-x86-far-transfers.py` owns the appended terminal records without
renumbering old opcodes. Frontend tests cover modes, types, immediate arguments,
attributes, O0/O2 and ELF/COFF/Mach-O output.

`tests/neverc/X86/verify_far_transfers.py --llvm-bin LLVM_BIN --neverc NEVERC
--mode 16|32|64 --output DIR` compares native instruction boundaries and complete
RAM with independently encoded far transfers. It also executes the returning
compiler continuation after a far entry clobbers caller-saved GPR/vector banks
and mutates memory, checking live values, return results and exact stack and
callee-saved register restoration. The fixture covers direct transfers and
16/32-bit protected call gates with zero parameters. Code16 and
`--mode 64 --x32` execute the full LLVM profiles. Stack-local targets verify
that the final SP assignment preserves the target address.
The comparison includes GPRs, segment/system state, FPU/XMM state, events and
RAM. It does not claim task switching, CET, nonzero gate parameters, fault
recovery, asynchronous unwinding or arbitrary original-image machine-context
restoration. See the LLVM fork's
`llvm/docs/X86NativeFarTransfers.rst` and the
[Intel SDM CALL/JMP contracts](https://cdrdv2-public.intel.com/835781/325462-sdm-vol-1-2abcd-3abcd-4.pdf).

## Native frame and SMM transfers

The C23 builtins `__builtin_ia32_iret16/32/64`, `retf16/32/64`, `erets`,
`eretu` and `uiret` (all with the same prefix) take a `const void *frame` and
return `void`. The three `retf16/32/64_imm` variants take an additional constant
`unsigned short adjustment`; they retain the CA immediate encoding even for
zero. The plain forms use CB. `__builtin_ia32_sysenter()` and
`__builtin_ia32_rsm()` take no arguments. All are nonreturning native intrinsics,
with conservative memory effects; source after the builtin is unreachable.
UIRET additionally requires `-muintr` or the corresponding function target
feature. NeverC supports the x64 code mode, including narrow operand sizes.

Frame pointers replace RSP immediately before the CPU instruction. A terminal
machine pseudo keeps that move after every compiler-frame access. The CPU reads
the frame, including its native layout and any FRED error-code slot, and owns
privilege, segment, flag, NMI, CET and fault behavior. SYSENTER reads ambient
entry MSRs; RSM restores the hardware SMM save state. Other live registers are
compiler-selected, and these APIs do not restore a complete original-image
context or promise asynchronous unwind behavior. An IRET context that can later
resume after the instruction, such as a protected-mode NT task return, is
outside this nonreturning interface.

`X86NativeTransfers.def` owns the frontend identities/signatures shared by
builtin registration and emission. The backend frame registry is separate from
the frontend, and drives the pruned intrinsic schema. `sync-x86-frame-returns.py`
owns the appended frame carrier and FRED native records without renumbering
existing opcodes. Frame-return frontend tests cover ELF/COFF/Mach-O at O0/O2,
dynamic frames, argument errors, nonconstant adjustments and UIRET features.

`verify_frame_returns.py` runs the direct LLVM, NeverC, and NeverC-IR-through-LLVM
paths against independent original-byte KVM executions. Its default tests
IRET/RETF operand sizes and SYSENTER, with plain/fixed/dynamic/pressure compiler
frames, CPL0/CPL3 transitions, aligned/unaligned and canonical high frame aliases,
flag samples and immediate cleanup. `--i386` selects the two full LLVM code32
profiles; `--code16` selects those profiles with a 16-bit code segment and the
same 32-bit stack ABI. Both run 16/32-bit IRET/RETF and SYSENTER. Code16 checks
the opposite operand-size-prefix polarity and corrupts the full ESP carrier in
its negative control. This is a full LLVM capability; NeverC's frontend target
remains x86-64. `--smm` tests RSM with separate normal/SMM memory spaces and SMI-created
save images, then establishes code64 execution inside SMM for the compiler
wrapper. This is KVM-mediated SMM qualification, not bare-metal firmware evidence.
The comparison covers all represented GPRs, PC, flags, segment/system state,
event state and every mapped RAM byte. XSTATE, CET, NT task returns, fault and
asynchronous delivery, other CPU models, and successful FRED/UIRET transitions
remain outside this native fixture's scope.

## Native FLAGS image widths

`__builtin_ia32_readeflags_u32()` and `__builtin_ia32_writeeflags_u32(unsigned)`
expose the existing full LLVM i386 FLAGS intrinsics. They reject x64 targets,
where a dword PUSHF/POPF encoding does not exist. The word APIs remain valid in
both modes, and the explicit u64 APIs remain x64-only. Generic `__readeflags`
and `__writeeflags` select u32/u64 from the code target, then convert to/from
their existing source ABI types. They no longer produce a u64 intrinsic call
with an i32 argument or store an i64 result into an i386 i32 temporary.

All snapshots and writes retain their ordered effects, including unused reads.
`X86FlagsFrontendTest` checks types, headers, ordering, rejected modes/arguments
and native word stack width at O0/O2. The intrinsic synchronizer updates the
pruned tables and selector IDs together; i386 machine emission requires the
full LLVM backend selected with `-fllvm-backend=LLC`.

`tests/verify-flags16-completion.py --compiler NEVERC --bits 32 --image-bits 32
--backend-llc LLC --keep DIRECTORY` compares both optimized objects with original
PUSHFD/POPFD bytes on Linux. `--spelling generic` checks the generic APIs;
`--bits 64 --image-bits 64` checks x64, and the default image width remains 16.
The witness tests every TF-clear low word and all old/new AC/ID combinations,
checks stack guards, restores host flags before returning to C, and rejects
wrong-width writes and fabricated read snapshots. Arithmetic flags across an
ordinary compiler ABI, privileged accesses, faults and asynchronous events
are separate contracts. `--source` retains the unchanged generated-C write
qualification used by NeverD.

## i386 intrinsic headers

The explicit `__builtin_ia32_readcr32(index)`, `writecr32(index, value)`,
`readdr32(index)` and `writedr32(index, value)` APIs use `unsigned` data and
emit the corresponding native `llvm.x86.*32` intrinsics. They require an i386
target and the full LLVM machine backend. CR indices are 0/2/3/4; DR indices
are 0..7, including the environment-dependent DR4/DR5 aliases. Indices must be
constant. Microsoft `__readcr0/2/3/4`, `__writecr0/2/3/4`, `__readdr` and
`__writedr` select the instruction width from the code target; their i386
header types match the 32-bit ABI. CR8 is diagnosed outside x64. Existing
portable 64-bit API signatures are unchanged.

The i386 source target is Linux. Microsoft spellings are also checked there
with `-fms-extensions -D_MSC_VER=1930`; this is not a Windows i386 ABI route.
`intrin.h` owns its `size_t` include, and NeverC's unconditionally exposed AVX
headers keep their MMX/SSE type prerequisites visible without enabling those
CPU features for generated code.

`tests/verify-system-register-completion.py --compiler NEVERC --backend-llc
LLC --spelling builtin` and `--spelling microsoft` exercise both code modes,
all register indices, live/dead/repeated reads and writes, memory ordering,
undefined-flag clobbers and ABI preservation at O0/O2. These are controlled
instruction completions in Unicorn; hardware paging/privilege behavior is a
separate qualification. `X86PrivilegedIntrinTest.I386SystemRegister*` checks
frontend types, native IR and rejected modes/indices. The native-intrinsic
schema and sorter maintain the new IDs, signatures, immediate attributes and
DAG selector references together.

The standard MMX/SSE and umbrella intrinsic headers admit both x86 pointer
widths. AMX remains guarded by long mode; its builtins are not declared by the
i386 target. `__SSC_MARK` saves the full EBX or RBX according to that target,
including PIC code. Linux i386 source still emits IR for the full LLVM fork;
these headers do not restore NeverC's embedded machine backend.

`__builtin_ia32_hreset` is available in both modes. The shared compiler macro
`__NEVERSIGHT_X86_HRESET__=1` declares this API independently of global CPU
options. It does not assert a processor feature. Callers still need
`target("hreset")` or `-mhreset`; this distinction lets NeverD's generated
functions select the native call without requiring a global target option.
The LLVM fork's Clang exposes the same builtin, capability macro and native
`_hreset` header wrapper.

`tests/neverc/X86/verify_i386_intrinsic_headers.py --neverc NEVERC --llvm-bin
LLVM_BIN --output OUT` executes O0/O2 static/PIC code in both widths. It checks
MMX/SSE/AVX2 values, carry/borrow, CRC32C, x87 after EMMS and full EBX/RBX
preservation. HRESET and AVX-512 are emitted without execution. The fixture
requires Linux multilib and a CPU with SSE4.2/AVX2/ADX.

`verify_hreset_source.py --exports EXPORTS --frontend COMPILER --llvm-bin
LLVM_BIN --output OUT` consumes actual NeverD `X86HresetSource` exports.
Patched Clang/NeverC must select native calls; stock Clang uses `--fallback`.
The verifier decodes HRESET, replaces it with a trap, observes EAX and resumes
to check the compiled return. Wrong-control mutations preserve the call but
must fail. These observations do not qualify physical prediction-history
effects or original-machine fault/event routing.

## Native protected ARPL

The LLVM fork provides `llvm.x86.arpl.reg(i16, i16)` returning the
adjusted selector and actual ZF, and `llvm.x86.arpl.mem(ptr, i16)` returning
ZF while the native instruction owns the word read and conditional write.
`llvm.x86.arpl.mem.address32(i32, i16, i32, i32)` additionally carries constant
16/32-bit address width and segment prefix. Ordinary pointers use DS through a
register class excluding BP/SP; explicit offsets wrap before segment addition.
All forms remain ordered and survive unused results. Long mode is rejected.

Clang and NeverC expose `__builtin_ia32_arpl_reg(unsigned short, unsigned short,
unsigned char *)`, returning the adjusted word and storing ZF after the native
operation; `__builtin_ia32_arpl_mem(void *, unsigned short)` returns ZF directly.
`__builtin_ia32_arpl_mem_address32(unsigned, unsigned short, constant width,
constant prefix)` returns ZF with explicit addressing. NeverC's Linux i386
frontend emits these native calls with `-S -emit-llvm`; object generation still
uses the full LLVM fork. This does not restore its pruned embedded i386 backend.
Clang additionally supports code16 with the 32-bit pointer/stack ABI.

Run `tests/neverc/X86/verify_protected_arpl.py --llvm-bin LLVM_BIN --mode 16
--output ARTIFACTS` for O0 and optimized O2 KVM profiles; `--mode 32` checks
protected code32. `--frontend PATH` compiles actual builtin C through patched
Clang or NeverC (NeverC currently admits mode 32). Each profile compares 19,968
pairs against independently encoded ARPL, including every RPL pair, high
selector bits, aliased inputs, used/unused results, CPL0/CPL3, arithmetic flags,
aligned/unaligned/cross-page and read-only/missing operands. Pointer DS and
explicit 16/32-bit FS addresses cover read-only, limited, null, expand-down and
nonflat segment caches. Original #GP/#PF/#AC handlers retain the original
fault frames and vector. All GPRs, flags, system/event state and RAM participate;
ABI-compatible successes also verify the compiled result and return frame.
Corrupting a declared source before intact ARPL or inverting its returned ZF
must fail independently.

`verify_arpl_source.py` consumes actual NeverD `X86ArplSource` exports, compiles
both C routes at O0/O2, and compares original state through the same KVM owner.
It admits a patched `--frontend` or stock Clang with `--fallback`. ELF, COFF and
MachO source exports are recompiled for SysV32; ordinary DS and nonflat FS/GS
are exercised, with an intact-instruction returned-ZF negative control.

`KvmProtectedFaultFixture.h` shares completed-OUT fault observation and inactive
exception-record handling with the protected far-pointer oracle. These tests
do not qualify real/VM86 mode, asynchronous events, device memory or an entire
i386 source dispatcher. The instruction contract follows
[Intel SDM ARPL](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html).

## Native far-pointer segment loads

`__builtin_ia32_lds16/32`, `__builtin_ia32_les16/32`,
`__builtin_ia32_lfs16/32/64`, `__builtin_ia32_lgs16/32/64` and
`__builtin_ia32_lss16/32/64` each take one `const void *`. The result is an
`unsigned short`, `unsigned int` or `unsigned long long`, respectively. The
memory image is packed: the offset occupies 2, 4 or 8 bytes, followed immediately
by a two-byte selector. It requires no padding or stronger alignment. Each
builtin emits its matching `llvm.x86.*` intrinsic and one native far-pointer
load. Hardware owns selector validation and segment installation before the
offset becomes the successful result. An unused result does not remove the
operation. Descriptor-table reads and accessed-bit writes remain unknown memory
effects, separate from the pointer image's exact read extent.

The selected segment and its hidden descriptor remain ambient CPU state. These
APIs do not save or restore TLS, switch the compiler stack or provide a complete
machine-state continuation. Callers must preserve the compiler ABI's segment
invariants. LSS does not establish MOV SS / POP SS interrupt/debug inhibition
([Intel SDM Volume 3A, section 7.8.3](https://cdrdv2-public.intel.com/835754/253668-sdm-vol-3a.pdf));
the earlier claim that it inhibited the next compiler-selected instruction was
incorrect. Machine scheduling cannot move instructions across the far-load
boundary. Native arithmetic flags are unchanged.
The pointer follows compiler-selected addressing. The API does not retain an
original instruction's source segment, address-size prefix or default-stack
fault provenance.

NeverC's Linux i386 frontend exposes all ten word/dword forms, emitting IR for
the full LLVM fork. The four LDS/LES calls diagnose long-mode use at the frontend;
all 64-bit result forms require long mode. The full fork's Clang exposes the same
thirteen builtins and maps them directly to the existing effectful intrinsics.
Its protected code16 target uses the 32-bit stack/pointer ABI. These APIs do not
restore NeverC's pruned embedded i386 backend or add a NeverC code16 frontend.
Invalid code modes fail rather than substituting another instruction.
`__has_builtin` reports registration; it does not override the call's code-mode
checks, just as for the existing legacy BOUND and ARPL interfaces.

`verify_protected_far_pointer.py --llvm-bin LLVM_BIN --mode 32 --frontend
NEVERC --output ARTIFACTS` compiles actual C for the protected-mode KVM oracle.
Use `--frontend CLANG` with `--mode 16` or `32` for the full fork's frontend;
omitting `--frontend` retains the direct-IR baseline. Every profile checks used
and unused results, ordinary-memory ordering, complete represented CPU state,
descriptor effects and original #GP/#NP/#SS/#PF frames. Its wrong-pointer
negative control retains the native instruction. `verify_far_pointer.py` also
accepts `--frontend CLANG` to check direct native Clang and Clang IR through the
full LLVM backend on x64, alongside the existing NeverC and LLVM profiles.

`FarPointerFrontendTests.cpp` checks native identity, unused effects, exact C
types, malformed operands and both assembly dialects at O0/O2 on ELF/COFF/Mach-O.
The backend and generated-table tests check segment definitions and unknown
descriptor effects. [verify_far_pointer.py](tests/neverc/X86/verify_far_pointer.py) compares direct LLVM,
NeverC and NeverC IR through LLVM against independent original bytes on Linux
x64. Its invocation window restores FS/GS bases, selectors and SS before host C
continues. It tests GDT/LDT selectors, readable and execute-only code descriptors,
read-only and absent data descriptors, null selectors, unaligned pointer images
and protected page boundaries. Faulting children single-step to the actual
allocated instruction and verify that all GPRs and arithmetic flags/DF remain
unchanged when it faults; page faults also check CR2. FSGSBASE OS support and `modify_ldt` are required;
unavailable prerequisites do not count as passes. The driver retains exact
commands, tool hashes, objects, observation streams and explicit exclusions.

Each of the six compiler/optimization profiles checks 4,320 paired state
observations, 18 exact page-boundary completions and 186 faults. The 1,440
observations with null FS/GS selectors 1..3 use `ptrace` to read the saved
register bank at the instruction's immediate single-step stop; they also check
GPR preservation, partial-register writes, arithmetic flags/DF and image
canaries. Those children are terminated at the stop, so they do not qualify
post-interrupt continuation. A separate original-byte reproduction showed all
three selectors becoming zero after Linux SIGTRAP return, while direct and
ordinary syscall observations retained them. Linux's
[x64 signal-frame implementation](https://github.com/torvalds/linux/blob/master/arch/x86/kernel/signal_64.c)
also writes zero into the frame's FS/GS fields, so reading them from a signal
handler would not measure the instruction result. No selector is masked,
removed from the matrix or retried on mismatch. Parent-child tracing permission
is an additional prerequisite for these observations.

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

`__NEVERC_X86_MPX_STACK_MEMORY__=1` advertises stack-default addresses for the
three BNDMOV memory builtins. OR `0x100` into a non-FS/GS segment control to
retain the SS default using the unmodified frame pointer and a computed index.
This accepts both 32- and 64-bit encodings, since MPX ignores 67H. Native bounds
memory is never probed by ordinary compiler loads. MIB table operations,
BNDMK and effective-address checks reject this control because their operand
roles differ. One shared MPX address validator owns frontend and backend
admission. Intrinsic IDs, signatures and machine opcode IDs are unchanged.

All MPX operands use allocated legacy-encodable registers. Fixed RBX copies
previously overwrote a reserved dynamic-stack base and redirected local
accesses to the bounds operand. The existing address carrier retains its
constraint through allocation, including register checks and MIB indices;
frame lowering keeps ownership of the realignment and PIC bases.

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
`--frame` additionally checks aligned fixed locals, dynamic allocations and
both stack canaries in every function. `--stack-memory` checks 120 BNDMOV
combinations; it composes with `--frame`. Per-function instruction bytes and
relative offsets must match both assembly dialects, independently of section
ordering or inter-function alignment.

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
The state verifier also accepts `--stack-memory --frame`: 40 BNDMOV controls
execute and 80 are explicitly skipped per optimization level under the same
model limitations. Enabled loads/stores and disabled operand suppression are
checked; native stack-segment fault delivery still needs separate qualification.

NeverD's separate `intel-mpx-v11` explicit-state C output is also executable
through NeverC and both full LLVM codegen pass managers. The integration oracle
loads its actual source and bound manifest, assembles the manifest's original
BND bytes independently, and compares all bounds registers, BNDSTATUS and RAM
under QEMU, including disabled operation and pending #BR. Run it with the files
exported by NeverD's `MpxOwnsAllBoundsAndMemoryForms` regression:

```sh
python3 tests/neverc/X86/verify_mpx_continuation.py \
  --source /path/to/mpx-high.c --manifest /path/to/mpx-high.json \
  --compiler build-neverc/bin/neverc --keep /path/to/qualification
```

Repeat for `mpx-llvm.c/.json`; add `--backend-llc /path/to/llc`, and optionally
`--new-pm`, for the full backend routes. Both optimization levels execute
1,344 original/source comparisons each. The six route combinations passed
16,128 comparisons. Each record explicitly excludes 100 of the exported 156
forms: 96 affected by the QEMU issues above and four absent-base table cases
outside this native fixture. NeverD's sanitizer oracle covers all 156 forms,
including nonzero directory bases. These counts do not certify physical MPX,
transaction rollback, memory faults, or arbitrary ordinary function ABIs.
The native fixture keeps BNDPRESERVE enabled while executing compiler-generated
code; original MPX input enablement is independently selected for each call.

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
   all targets. The x86 count is tracked in `IntrinsicImpl.inc`.

5. Changes to `TargetInfos` also change the reviewed immutable-object digest in
   [`utils/plugin-api/global-state-allowlist.json`](utils/plugin-api/global-state-allowlist.json). Review the complete table and
   its element type, then update its `definition_sha256` using
   `object_definition_sha256` from
   [`utils/plugin-api/check-global-state.py`](utils/plugin-api/check-global-state.py).
   Run the plugin global-state audit alongside the intrinsic synchronizers;
   signature checks alone do not validate this review manifest.

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

### Returning VMRUN with explicit register and xstate ownership

`<svmcontextintrin.h>` provides the 144-byte `struct __svm_vmrun_context` and
`__svm_vmrun_context64` / `__svm_vmrun_context32`. Both call the native
`__builtin_ia32_vmrun_context(void *, unsigned immediate_width)` and emit
`llvm.x86.vmrun.context(ptr, i32)`. Both require x64 code; 32 selects an addr32
physical VMCB address, not an i386 calling convention.

The context contains four u64 fields (VMCB physical address, guest XSAVE linear
address, host XSAVE scratch linear address, XCR0 mask), followed by fourteen
u64 guest input/output fields: RBX, RCX, RDX, RSI, RDI, RBP and R8..R15.
RAX/RSP/RIP/RFLAGS and exit information remain in the VMCB. The mask covers
all enabled XCR0 components; the guest leaves XCR0 unchanged. Both buffers
must be 64-byte aligned, CPUID-sized standard XSAVE images with initialized
reserved bytes. The context, buffers and host compiler stack remain accessible
and disjoint from guest-writable RAM and hardware-written VMCB/VM_HSAVE_PA
pages, including after loading guest xstate.

The compiler owns the helper frame and restores host GPR ABI and user xstate
after VMEXIT. The VMM owns SVM setup, GIF, intercepts, supervisor state and any
required VMLOAD/VMSAVE. The guest must preserve the host's software-managed
system ABI. Processor faults and arbitrary mode/system-state changes are not
converted into normal C returns. This returning contract cannot be represented
by a scalar `void __svm_vmrun(address)` call that loses guest register outputs.

`X86SVMContextTest` checks the installed header, layout, returning IR effects,
invalid widths and O0/O2 ELF/COFF/Mach-O code generation. The native-signature
and SVM-context synchronizers own the pruned tables, including both printers.
The system-effect synchronizer also retains raw VMRUN's unknown reads/writes.
The shared `X86SVMContext` implementation is synchronized from NeverSight
LLVM commit `101513238669`, retaining its Apache-2.0 WITH LLVM-exception license.
[tests/neverc/X86/verify_vmrun_context.py](tests/neverc/X86/verify_vmrun_context.py) runs 48 contexts per optimization
profile in QEMU TCG: eight seeds, both address widths, VMMCALL/HLT/invalid-VMCB
exits, fourteen GPRs, x87/XMM/YMM, host state and dynamic caller frames. It
supports direct NeverC, LLVM, NeverC bitcode to LLVM and actual NeverD LLVMC
source via `--neverd-source`. Every guest also executes the original-byte IDA
counterexample; the guest changes RCX from 0x1234 to 0x5678 before VMEXIT.
One additional generated-context call executes that same guest and checks
RCX=0x5678; the NeverD routes also check the actual exported function's return.
`--neverd-continuation FILE` also compiles actual NeverD `amd-svm-v10` exported
source, through direct NeverC or `--backend-llc` for the full LLVM fork. It adds
the complete original-byte MOV/VMRUN/MOV/RET path: VMEXIT must stop with status
13, preserve hardware-restored RAX/RSP, clear RF, publish actual CR2 and all
fourteen GPR/SIMD outputs, and resume to return 0x5678. The fixture separately
executes original addr32 VMRUN with high RAX bits set to verify only its VMCB
address truncates. Each source profile executes 50 generated-context calls
plus the independent original-byte checks. Add `--new-pm` with `--backend-llc`
or `--llc` to qualify the full fork's new codegen pass manager. HighC and
LLVM C, direct NeverC and both full-fork pass managers, at O0/O2 give 600
calls across twelve profiles.
Sixteen invalid VMCB cases per profile also compare CR2 against independent
original addr64/addr32 VMRUN instructions. Distinct explicit CR2 inputs must
survive early exits; the compiler host's ambient CR2 cannot supply them.
The harness retains commands, hashes and exit logs. Physical AMD execution,
supervisor xstate and arbitrary ordinary-ABI lifting remain unqualified.

The complete generated driver also exposed two backend defects independent
of SVM. The full fork repairs unrelated loop-exit PHI inputs after LSR edge
splitting and makes tied-operand rewriting mandatory for `optnone` functions.
NeverC's embedded legacy backend already runs the required tied-operand pass;
its LSR now shares the full fork's delayed LCSSA repair after all fixups and
before dead-instruction cleanup.

### Explicit KVM hypercall ABI

`<kvmhypercallintrin.h>` exposes `__kvm_vmcall` and `__kvm_vmmcall`, backed by
`__builtin_ia32_vmcall_kvm` and `__builtin_ia32_vmmcall_kvm`. Each takes five
`unsigned long long` values and returns one: RAX is the call number/result,
and RBX/RCX/RDX/RSI hold four arguments. These are native instructions without
inline assembly or external helpers. Calls have unknown memory effects, are
convergent and cannot be duplicated even when their result is unused.

The caller explicitly binds the returning
[KVM ABI](https://docs.kernel.org/virt/kvm/x86/hypercalls.html), with all other
registers, extended state and the compiler execution environment preserved.
Services with extra outputs, nonlocal transfers or unrepresented caller-state
dependencies require a different interface. The opcode does not identify a
hypervisor and this API does not implement Hyper-V's different convention.

The shared `X86KVMHypercall.h` lowering comes from NeverSight LLVM commit
`7eb7c0ffbe9e`, retaining Apache-2.0 WITH LLVM-exception licensing. The full
fork provides code32 and code64; NeverC retains only its existing x64 codegen
domain and rejects other modes. `sync-x86-native-intrinsics.py` owns the four
IR declarations, while `sync-x86-kvm-hypercalls.py` owns machine descriptors
and both printers without renumbering existing instruction IDs. Their tests
verify exact register order and detect lost inputs or memory effects.

`X86KVMHypercallTest` exercises the public header, retained unused calls and
memory accesses, invalid inputs, and O0/O2 ELF/COFF/Mach-O emission.
`tests/neverc/X86/verify_kvm_hypercalls.py --keep DIRECTORY` additionally runs
independent original bytes and generated calls in a native KVM guest. An
isolated userspace service observes all five physical inputs, returns a value
that depends on each and writes shared RAM. Each O0/O2 profile compares 256
pairs and executes 512 hypercalls. This qualifies ABI transport, not all KVM
services. Missing KVM fails explicitly instead of counting as passed.

Use `--backend-llc /path/to/llc` for NeverC IR through the full fork; add
`--new-pm` for its other codegen pipeline. `--llvm-only --bits 32` or
`--llvm-only --bits 64` independently tests the full fork without NeverC.
`--source FILE --manifest FILE` compiles actual NeverD continuation C after
checking the manifest's source hash and explicit KVM binding. The run retains
commands, tool and fixture hashes, generated objects and runtime logs.

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

### Native x87 status, reset and wait

The header-free builtins `__builtin_ia32_fnstsw()` (unsigned short),
`__builtin_ia32_fnstsw_mem(unsigned short *)`, `__builtin_ia32_fnclex()`,
`__builtin_ia32_fninit()` and `__builtin_ia32_fwait()` emit the fixed native
interfaces `llvm.x86.fnstsw`, `llvm.x86.fnstsw.mem`, `llvm.x86.fnclex`,
`llvm.x86.fninit` and `llvm.x86.fwait`. The last four return void. The memory
status read uses a single two-byte DD /7 store even at unaligned addresses;
it does not become FNSTSW AX followed by an ordinary C store.

Status reads, clear and initialize are nonwaiting. FWAIT checks pending
unmasked exceptions. Both FNSTSW forms read and clobber FPSW, reflecting
undefined condition-code results. FNCLEX reads/clobbers FPSW; FWAIT additionally
reads FPCW and may raise an FP exception. FNINIT clobbers FPCW/FPSW and every
allocator FP, architectural ST and MMX register, with a scheduling boundary
that prevents virtual-register operations from crossing the reset. Compiler
values live across it must survive through spills/reloads. This allocation
contract does not mean FNINIT erases physical register payload bits.

`X86X87ControlTest` and `X86X87Control` also check these five interfaces.
[verify_x87_status.py](tests/neverc/X86/verify_x87_status.py) accepts the same compiler options as the
control-word harness. It compares independent DF E0, DD /7, DB E2, DB E3 and
9B byte oracles, checks two-byte canaries and page boundaries, inspects fault
PC/opcode/marker/saved state in children, and checks live x87/MMX values across
FNINIT, including a realigned stack frame. The shared
[native_qualification.py](tests/neverc/X86/native_qualification.py) owns compiler profiles and
hash-based evidence recording for both harnesses. Ordinary unconstrained FP
IR keeps its usual environment assumptions; these APIs do not establish an
atomic owned-context transition or complete machine-state equivalence.

Qualification against LLVM `5740ed1d13baa01ca5f82d82ebc927e656620b39` on the
recorded Intel Core i9-13900H Linux x64 host passes all six direct LLVM,
NeverC and text-IR bridge O0/O2 profiles: 34,560 state observations,
60 expected faults, 12 page-boundary accesses and 1,536 live-value cases.
All six outputs agree. The full x86 frontend and backend selections pass
113 and six tests respectively. The shared-driver refactor also repeats all
six control-word profiles with the same 6,912 state observations, 120 expected
faults and 24 boundary accesses; generated control sources remain unchanged.

### Native x87 environment and register images

Eight header-free `__builtin_ia32_fnstenv16/32`, `__builtin_ia32_fldenv16/32`,
`__builtin_ia32_fnsave16/32` and `__builtin_ia32_frstor16/32` builtins select
14/28-byte environments and 94/108-byte register images. Each returns void;
saves accept `void *`, restores accept `const void *`. Corresponding fixed
LLVM names use the same operation and format suffix. The suffix selects the
operand format independently of address width.

Saves are nonwaiting. FNSTENV masks exceptions after saving; FNSAVE saves the
register payloads and initializes the FPU. Restores check incoming pending
exceptions before reading, and defer newly loaded unmasked exceptions to the
next waiting operation. The backend carries exact volatile memory sizes and
byte alignment, keeps all operations as scheduling boundaries and invalidates
compiler FP/ST/MMX values for operations that replace TOP/tags or payloads.
This preserves compiler-owned values through spills/reloads; it does not make
separate state calls an atomic context switch.

The full fork owns the four new 16-bit machine encodings. NeverC's
[sync-x86-x87-environment.py](utils/lint/sync-x86-x87-environment.py) keeps their opcode numbers,
66 prefixes, assembler and printer records synchronized, using the existing
system-effect owner for register and memory effects. Native signature
synchronization remains in the existing intrinsic generator.

[X87EnvironmentFrontendTests.cpp](tests/neverc/X86/X87EnvironmentFrontendTests.cpp) checks both formats
at O0/O2 on ELF, COFF and Mach-O, including pointer qualification and arity.
[verify_x87_environment.py](tests/neverc/X86/verify_x87_environment.py) uses the shared qualification
driver with [x87_environment_harness.c](tests/neverc/X86/x87_environment_harness.c): raw-byte reference
instructions, independently reconstructed full tags, all TOP values, mixed
80-bit payload classes, save/reset/restore behavior, exact protected-page
boundaries, pending and newly loaded exceptions, and live x87/MMX values.
Physical qualification covers the recorded Linux x64 CPU; i386 runtime,
privileged faults and historical processors remain outside that evidence.

The 2026-10-28 qualification uses full LLVM
[`f35c3ce214cd`](https://github.com/NeverSight/llvm-project/commit/f35c3ce214cd0bb12849ea46fdb826864444786b).
Direct LLVM, NeverC and NeverC IR through full LLVM pass at O0/O2: six profiles,
55,296 state observations, 480 expected faults in 624 fault/nonfault cases,
96 protected-page boundaries and 3,072 live-value cases. Their observation
streams match byte-for-byte. All 115 X86 frontend tests, seven backend tests,
60 generator tests and 16 X86 synchronization checks pass. The qualification
manifest records compiler, source and artifact hashes and verifies stable inputs.

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
AMX tile row-memory additionally requires the stack-memory capability below;
MPX controls still reject the bit. Existing raw prefix values keep
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

`X86NativeRegisterTest` runs CPUID against an independent raw-assembly
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

Full LLVM supplies the corresponding `.address` and protected `.address32` APIs,
plus `llvm.x86.{sgdt16,sidt16,lgdt16,lidt16}.address32` for the legacy 16-bit
descriptor-table operand size. These are distinct from a 16-bit address.
The `.address32` forms accept code16 and code32 with the 32-bit stack/pointer
ABI. NeverC's x86 frontend remains x64-only. Existing pointer and register
builtins are unchanged.
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

`tests/neverc/X86/verify_protected_descriptor_state.py --llvm-bin DIR
--mode 16 --output DIR` executes all 17 full-LLVM address32 APIs against
independent original bytes in KVM; `--mode 32` selects the code32 regression.
Its 266 functions cover both address sizes, all segment prefixes, both table
operand widths and live/dead verification results at O0/O2. CPL0/CPL3 cases
include UMIP, CR0 input changes, null/invalid/nonpresent selectors, LTR busy
writes, source and descriptor paging, read-only pages and alignment faults.
The oracle compares represented GPRs/flags, segment/system state, active
events and RAM, then checks successful compiler returns and saved registers.
Offset-transport and returned-ZF corruption must each be rejected.

Code resides in a nonflat CS with separate relative RIP and linear debug
addresses. INVLPG's bytes, operand transport, exception behavior and retained
effects are checked; this is not an experiment observing stale physical TLB
entries. These tests do not supply a NeverC i386 frontend, real/VM86, arbitrary
segment caches, asynchronous events or device memory semantics.

The full fork passes 3,144 object/encoding checks; NeverC and its bitcode-to-fork
route each pass 1,404. Explicit-address CPL0 runs pass 210 checks for full LLVM
with `--address-bits 32`, 102 for its 64-bit-only run, and 102 for each NeverC
route at either width. The compiler suite passes 65 tests, the independent
binary suite 89, generator/sort regressions 36, and target/MC plugin tests 221.
Both schemas regenerate successfully: x86 adds thirteen instructions to 18,538,
while all prior instructions, registers and features remain unchanged.

### Shared i386 descriptor and machine-state builtins

The 35 builtins for VERR, VERW, SLDT, STR, SMSW, LLDT, LTR, LMSW, CLTS,
CLAC/STAC, HLT, VMXOFF, STGI/CLGI, INVD, and the 16/32-bit LAR/LSL results are
registered in the common x86 builtin table. Their LLVM lowering and signatures
are unchanged. Linux
i386 C23 source can emit IR or use `-fllvm-backend=/path/to/full/llc` for native
objects. The seven 64-bit result forms remain available only in x86-64;
explicit-address APIs keep their separately documented mode contracts.

`tests/neverc/X86/verify_i386_descriptor_builtins.py --neverc PATH --llvm-bin DIR
--output DIR` checks all 35 registrations, IR verification and two retained
native operations per function at O0/O2 in i386 and x64. It rejects accidental
registration of 64-bit result forms on i386 and retains all seven native wide
forms on x64. SWAPGS remains unavailable on i386. The query objects additionally
run as real Linux i386/x64 processes, comparing LAR/LSL results and ZF and
VERR/VERW status against independent original instructions for null, valid and
invisible selectors, differing prior values, unaligned words and words ending
at an inaccessible page. Privileged state operations have code-generation
coverage here; they do not run successfully in user mode. Runtime privilege,
feature availability and VMX/SVM state remain owned by the processor. These
registrations reuse the LLVM fork's existing 32-bit and 64-bit instructions;
no inline assembly or runtime helper implements them.

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

### Native segment selector MOV

Four header-free builtins retain one architectural MOV for each selector
access, without source-level assembly:

```c
unsigned short __builtin_ia32_readseg(unsigned segment);
void __builtin_ia32_readseg_mem(unsigned segment, void *destination);
void __builtin_ia32_writeseg(unsigned segment, unsigned short selector);
void __builtin_ia32_writeseg_mem(unsigned segment, const void *source);
```

The register number must be a constant architectural encoding: ES=0, CS=1,
SS=2, DS=3, FS=4, GS=5. Writing CS is rejected. The matching LLVM interfaces
are `i16 @llvm.x86.readseg(i32 immarg)`,
`void @llvm.x86.readseg.mem(i32 immarg, ptr)`,
`void @llvm.x86.writeseg(i32 immarg, i16)` and
`void @llvm.x86.writeseg.mem(i32 immarg, ptr)`. The full fork supports 16-, 32-
and 64-bit code. Protected code16 uses the 32-bit stack/pointer ABI;
NeverC's pruned backend supports x64 and does not expose an i386 frontend.

Memory forms access exactly two bytes inside the MOV. Segment writes also own
descriptor validation, hidden-cache installation and conditional descriptor
accessed-bit writes. These calls retain unknown memory effects, including when
a read result is unused. Lowering materializes a physical segment definition
before machine optimization and retains both explicit and implicit memory
references. The MOV does not define EFLAGS. Machine scheduling keeps the
segment boundary intact.

These APIs operate on ambient CPU state. Callers must preserve the enclosing
ABI's segment, TLS and stack invariants. MOV SS inhibits events at the next
machine-instruction boundary; the compiler chooses that instruction. A pair of
C calls does not establish an atomic stack switch or an original-PC restart
contract. The memory interfaces use ordinary pointers and do not encode a
segment override or an address-size override.

`sync-x86-native-intrinsics.py`, `sync-x86-system-effects.py` and
`sync-x86-segment-moves.py` own the pruned generated declarations, effects and
two appended write pseudos. The last generator preserves existing opcode IDs;
its tests check the physical definitions and absence of EFLAGS clobbers.

[verify_segment_moves.py](tests/neverc/X86/verify_segment_moves.py) compares all 22 register/memory
forms with independent original bytes on Linux x64. It saves and restores
ambient selectors and FS/GS bases around each invocation. The November 2
P-core and E-core runs each passed six LLVM/NeverC/combined O0/O2 profiles:
27,648 state pairs, 264 boundary observations and 1,488 fault observations
(744 original/generated pairs) in total. Cases include LDT descriptors, exact
two-byte end-of-page operands, invalid selectors, missing pages, read-only
destinations and low-word truncation. Fault checks retain the original marker,
selectors and bases and authenticate the faulting MOV form. These tests do not
qualify i386 execution, other processor models, CPL0..2 descriptor faults,
alignment checking, asynchronous events, or complete original GPR/flags restart
state. Null selectors 1..3 are excluded because Linux normalizes them on signal
return. The frontend/backend regressions separately check native IR, repeated
calls, diagnostics and ELF/COFF/Mach-O object emission.

[verify_protected_segment_moves.py](tests/neverc/X86/verify_protected_segment_moves.py)
adds native protected execution through `--llvm-bin LLVM_BIN --mode 16
--output ARTIFACTS`; `--mode 32` checks the existing 32-bit path. Each O0/O2
profile compares 4,824 complete state pairs from 28 functions, including used
and unused reads. The CPL0/CPL3 matrix covers all segment registers, null and
invalid selectors, read-only/nonpresent/code/nonflat/word-stack descriptors,
aligned/unaligned/cross-page/missing/read-only operands, and descriptor-page
faults. Original #NP/#SS/#GP/#PF/#AC handlers expose the vector; all GPRs,
flags, system/segment state, active events and RAM are compared. Successful
ABI-compatible cases also return through the compiler frame. Negative controls
corrupt the declared pointer or selector before an intact MOV and must fail.

Each generated MOV is followed immediately by an explicit LFENCE. The oracle
authenticates both opcodes and compares the same two-instruction span, ending
MOV-SS inhibition before the debugger stop. This fixture does not observe
events inside that window, qualify real/VM86 execution or supply a coupled
stack-switch API. It shares completed-OUT fault handling with the protected
ARPL and far-pointer fixtures.

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
provides `.address32` query forms for 16/32-bit results in protected code16
and code32, using the 32-bit pointer/stack ABI, an i32 offset and address width
16 or 32. Mismatched code modes still fail explicitly. The system-address
generator appends seven NeverC pseudos while
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

`tests/neverc/X86/verify_protected_descriptor_queries.py --llvm-bin DIR
--mode 16 --output DIR` executes the full LLVM query APIs in KVM; use
`--mode 32` for the protected code32 regression. Each O0/O2 image contains
224 functions covering both queries, result/address widths, every segment
prefix, and independent live/dead value/ZF results. The original-byte oracle
compares represented GPRs/flags, segment/system state, active events and RAM
at the native instruction boundary. Successful and rejected queries also
check the compiled return, prior selection and callee-saved registers.
Cases cover CPL0/CPL3, descriptor types/privileges/limits, nonflat CS/FS/GS,
unaligned/cross-page/read-only/missing operands and descriptor-page faults.
Wrong offset transport and inverted returned ZF must fail independently.

The KVM breakpoint helper distinguishes CS-relative RIP from the linear debug
address. This keeps code16 branches within their 64 KiB offset range while
placing the image above the fixture tables. Original fault handlers retain
their flat code segments; both restart PCs are authenticated before comparing
the relocated fault frames. These LLVM tests do not enable a NeverC i386
frontend, qualify real/VM86 or asynchronous events, or grant a general ABI for
arbitrary segment caches.

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

`__NEVERC_X86_AMX_STACK_MEMORY__ == 1` additionally admits `0x100` in the five
tile-memory builtins' segment controls. It requires address64 and permits
zero/CS/DS/ES/SS; FS/GS and address32 still reject it. The complete stride stays
independent of the base. The compiler outlines an internal `preserve_allcc`
leaf whose BP holds the base and whose normal prologue/epilogue and DWARF/SEH
preserve the caller's frame. The helper's return preserves ambient MPX bounds.
It requires no user assembly or external runtime; all existing IDs stay fixed.
The host call and saved-register frame are additional compiler transport.
Physical tile state and the instruction's partial progress remain native.

`verify-amx-explicit-controls.py --stack-memory` checks all tiles, five
operations, five controls, VEX/EVEX, O0/O2 and ELF/COFF/MachO. Its controlled
completions retain full-width base/stride, unknown-memory ordering and saved
GPRs. `verify-amx-stack-unwind.py` instruments only decoded AMX slots with UD2
and exercises real signal delivery, DWARF unwinding and resumption through
dynamic frames. It does not qualify physical AMX execution or tile restart.

Complete NeverD LLVM-to-C modules also exercise declarations and calls before
the target-attributed definition. Definition emission now refreshes CPU,
tuning and feature attributes from that declaration before generating its
body. This prevents an earlier baseline prototype from dropping AMX features;
feature inheritance and explicit feature disabling use the same authority.

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


### Explicit Hyper-V page ABI

The header `hypervcontextintrin.h` supplies a 224-byte context and `__hyperv_call`.
The header-free builtin `__builtin_ia32_hyperv_call(entry, gprs, xmm, stride)`
takes three pointers and a constant vector stride (16, 32 or 64). The GPR
bank uses x86 encoding order. It emits `llvm.x86.hyperv.call`, with unknown
memory effects, convergence and no duplication. RCX/RDX/R8 and XMM0..5 are
inputs; RAX/RCX/RDX/R8 and XMM0..5 are outputs. Other storage is untouched.

The native emitter calls the supplied registered hypercall page. The compiler
owns call-frame alignment and excludes live locals from the red zone.
Caller-state replacement, faults and invalid native environments remain
outside this returning interface. See the
[LLVM contract](https://github.com/NeverSight/llvm-project/blob/main/llvm/docs/X86HyperVContext.rst)
and [TLFS](https://learn.microsoft.com/en-us/virtualization/hyper-v-on-windows/tlfs/hypercall-interface).

The shared `X86HyperVContext.h` and `X86HyperVContextEmitter.cpp` come from
NeverSight LLVM commit `b5130e83dbd7`, retaining its Apache-2.0 WITH
LLVM-exception license. `sync-x86-hyperv-context.py` owns the append-only
machine descriptor; `sync-x86-native-intrinsics.py` owns the fixed intrinsic
signature.

`X86HyperVContextTest` checks native IR and ELF/COFF/Mach-O assembly.
`tests/neverc/X86/verify_hyperv_context.py --compiler build-neverc/bin/neverc --keep DIRECTORY`
checks unaligned banks, all strides, SysV/Win64 callers and live frames.
`tests/neverc/X86/verify_hyperv_native.py --keep DIRECTORY` executes an independent
original page-call wrapper and candidate inside KVM. Its synthetic userspace
service exercises normal, fast and rep transport; it is not evidence for
Microsoft Hyper-V service implementations. Missing KVM fails explicitly.
Use `--backend-llc /path/to/llc` and `--new-pm` for the full LLVM backend;
`--llvm-only` skips the frontend. `--source FILE --manifest FILE` qualifies
actual NeverD C after checking the source hash and explicit page binding.

## Native SNP and SEV-ES completion

`<snpcontextintrin.h>` supplies `struct __snp_context` (RAX/RCX/RDX/RFLAGS,
32 bytes), `__snp_psmash`, `__snp_pvalidate`, `__snp_pvalidate_addr32`,
`__snp_rmpadjust`, `__snp_rmpupdate`, `__snp_rmpquery`, `__snp_rmpread`,
`__snp_rmpopt`, `__snp_rmpchkd`, and `__sev_vmgexit`.

Eight raw `__builtin_ia32_<instruction>_context(gprs, flags, control)` builtins
lower directly to `llvm.x86.<instruction>.context(ptr, ptr, i32 immarg)`.
The register bank has three unaligned u64 slots, and the flags pointer names
a separate unaligned u64. `llvm/IR/X86SnpState.h` owns inputs, outputs and flag
masks. The API preserves nonoutputs, returns RMPQUERY's complete bank and
RMPCHKD's partial-progress registers, and merges actual modified flags.
RMPCHKD's undefined arithmetic bits are native values, not invented zeros.
Only PVALIDATE accepts control bit 0 (address32); bits 15:8 optionally hold a
native segment-prefix byte. Context addresses retain default full-width
linear addressing. The memory objects, compiler frame and instruction-owned
memory/RMP/mapping changes must be disjoint and accessible.

The x64 native backend emits instructions directly with unknown memory
effects. Register loads, the operation, output stores and LAHF/SETO flag
capture form one compiler pseudo; no inline assembly is emitted. The native
CPU owns SNP, paging, RMP, permission and restart behavior. Only normal
completion publishes outputs; contexts are not fault or exception images.
`__builtin_ia32_vmgexit_sev(parameter)` supplies the optional RAX parameter
under an explicit returning SEV-ES/GHCB service contract. Private CPU state
and compiler execution state must survive. Non-SEV-ES VMMCALL fallback and
caller-state replacement need different contracts.

`sync-x86-native-intrinsics.py`, `sync-x86-system-effects.py` and
`sync-x86-snp-context.py` own the generated tables. Existing opcode IDs stay
stable. The SNP generator appends RMPREAD/RMPOPT/RMPCHKD/VMGEXIT and the native
completion pseudo, with independent uses/defs, encoding and drift tests.
The installed header is listed in [neverc/lib/Headers/CMakeLists.txt](neverc/lib/Headers/CMakeLists.txt).

Run `X86SNPContextTest.*` for C23 frontend, ELF/COFF/Mach-O and immediate/mode
checks. `tests/neverc/X86/verify_snp_context.py --keep OUTPUT` compares native
builtins against separate raw instruction/PUSHFQ references. Use
`--backend-llc PATH` and optionally `--new-pm` for the full LLVM backend, or
`--llvm-only` to bypass the frontend. `--source FILE --manifest FILE` checks
actual NeverD exports against their source digest and explicit bindings.
The Linux x64 signal service tests encoding, unaligned transport, complete
register/flag outputs, live values and fault-before-publication. It does not
qualify physical AMD RMP algorithms or actual SEV-ES services.

## Native immediate-abort RTM pairs

`__builtin_ia32_rtm_abort_pair(code)` and `<immintrin.h>`'s
`__rtm_abort_pair(code)` return the actual abort status of a single XBEGIN /
XABORT pair. The constant code must be in 0..255. Entry requires RTM support
outside an existing RTM/HLE region; an early abort may have a different status
from an explicit abort. One machine pseudo owns both instructions and the
local fallback, so no spill, call, padding or C return splits the checkpoint.
This does not execute a general transaction body or restore an external one.

The generic x64 `__builtin_ia32_cpuidex(int *, unsigned, unsigned)` spelling
uses the existing native CPUID interface, including RBX preservation. Callers
can check RTM availability without inline assembly or Microsoft language mode.
The pair uses `llvm.x86.rtm.abort.pair`, with an immediate argument and
conservative memory, convergence and no-duplication effects. Its lowering is
shared with the LLVM fork. `sync-x86-rtm-abort-pair.py` appends its opcode and
preserves prior IDs; `X86TransactionTest` checks diagnostics, both optimization
levels, native IR, exact contiguous object bytes and all three x64 formats.

[verify_rtm_abort_pair.py](tests/neverc/X86/verify_rtm_abort_pair.py) compares all 256 immediate codes
with an independent raw-byte reference. It exercises integer/SIMD values live
across the pair, memory order and exact status at O0/O2 through NeverC or the
LLVM fork's legacy/new backend pass managers. Pass `--compiler`, `--sde` and
`--keep`; add `--backend-llc` and optionally `--new-pm` for the LLVM fork.
SDE 10.13.1 requires `-tsx` only in full mode: that option overrides abort mode.
Forced abort checks omit it and require exact statuses 0, 2, 6 and 8. This mode
also clears CPUID.RTM, so only the fixture's explicit `sde-abort-N` entry
bypasses discovery under SDE. Production providers retain their CPUID guard.
Emulator results do not establish physical RTM behavior or general rollback.

## Native HLE operation contexts

`<hlecontextintrin.h>` exposes `__builtin_ia32_hle_context(memory, gprs, flags,
control, bytes)` and the typed `__hle_native` / `__hle_step` forms. Both controls
must be constants. `llvm/IR/X86HleState.h` owns the 19 operation identities,
legal widths, register inputs/outputs and flag masks; its implementation is
shared with the LLVM fork. The pointers address native flat memory and disjoint
unaligned RAX/RCX/RDX/RBX and RFLAGS objects. Long-mode LAHF/SAHF and the selected
underlying instruction must be available. Native faults do not publish a new
context.

The native form leaves ownership of the entire HLE region to the C caller.
The standalone step requires entry outside RTM and external HLE. It executes a
native PAUSE after the prefixed memory instruction and before publication. As
specified by Intel SDM Volume 1 §17.3.9.1, PAUSE aborts an active transaction;
HLE retries its acquire without elision. The step thus returns a real native
nontransactional completion, including on CPUs that natively ignore legal HLE
hints. It does not replace the instruction with an ordinary C atomic or invent
an abort result.

`sync-x86-hle-context.py` appends the pseudo without renumbering prior opcodes.
`x86_context_tables.py` owns the common context descriptor construction used by
both SNP and HLE. The native-intrinsic generator supplies the two immediate
arguments and conservative effects. `X86HLEContextTest.*` checks the frontend,
object formats and control diagnostics. `verify_hle_context.py` compares 136
legal width/hint combinations against separate raw assembly, including unaligned
contexts, signed bit indexes, native register/defined-flag and memory outputs,
live integer/FP values and fault-before-publication. Undefined flags remain
architectural choices; native fallback qualification does not establish physical
HLE commits or generic RTM rollback support.

The Microsoft HLE compatibility surface also uses this native interface:
exchange, exchange-add, compare-exchange (including pointer forms), and the
three release stores. All 17 `_Interlocked*HLE*`/`_Store*HLERelease` wrappers
are installed through `<hleinterlockedintrin.h>` and the normal Microsoft
intrinsic includes. They retain the caller-owned region contract without
inserting a PAUSE, and emit no inline assembly. Windows long remains 32 bits;
64-bit and pointer forms remain 64 bits.

## Native CPUID header dispatch

`cpuid.h` now routes `__cpuid` and `__cpuid_count` through the generic native
`__builtin_ia32_cpuidex` interface, including max-leaf and feature helpers.
Leaf, subleaf and output lvalues are evaluated once. The compatibility assembly
uses an early-clobber temporary because its first XCHG precedes CPUID's input
consumption. Without it, an EBX-only result could share EAX, change the leaf and
corrupt RBX when other outputs were dead. The LLVM fork's Clang header carries
the same constraint correction.

`CpuidHeaderKeepsDeadOutputsAndSingleEvaluation` checks native IR without
assembly, each independently used output, live leaf/subleaf values, leaf-only
queries and argument/lvalue evaluation at O0/O2. It also executes the assembly
fallback. The realigned SysV/MS frame test retains an independent raw CPUID
reference, so changing the header does not merge the implementation and oracle.

## Native SMM software-state restoration

`<smmcontextintrin.h>` provides a 40-byte `__smm_rsm_context` and terminal
`__smm_rsm_context64` wrapper. The underlying builtin
`__builtin_ia32_rsm_context(const void *)` emits
`void llvm.x86.rsm.context(ptr)`, requiring code64 and XSAVE. Its five u64 slots
carry the standard XSAVE image address, restore mask, CR2, CR8 and
IA32_KERNEL_GS_BASE. The native environment owns active SMM, live XCR0, valid
component images, accessible disjoint storage and all other software-managed
state. The processor's actual SMI save image owns the hardware-restored banks.

One terminal backend pseudo writes the three system registers, then emits
adjacent XRSTOR64/RSM after register allocation and scheduling. No compiler
instruction or context-memory access follows XRSTOR. The caller's compiler
frame is abandoned; there is no ordinary C return. The intrinsic retains
convergent, noduplicate, noreturn, nounwind and conservative memory effects.
`sync-x86-smm-context.py` appends its opcode without renumbering earlier IDs;
the native-intrinsic generator owns its signature and effects. The emitter
is shared with the LLVM fork, including zero extension of x32 pointers.

`X86SMMContextTest.*` checks the installed header, native IR, terminal sequence,
three x64 object formats and invalid signatures/features. Run
`tests/neverc/X86/verify_frame_returns.py --smm-context --neverc COMPILER
--llvm-bin LLVM_BIN --output OUTPUT` for native KVM-mediated SMM comparisons
through NeverC, LLVM and NeverC IR through LLVM at O0/O2. Each profile compares
3,072 complete state pairs with original XRSTOR/RSM bytes, including x87,
SSE/AVX, nonzero register high halves, CR2/CR8/KernelGSBase, four compiler-frame
shapes, both memory spaces and event state. Replacing RSM or omitting XRSTOR
must fail. Initial XSAVE components are compared by architectural values;
XINUSE may legally be set even for an initial component. The projection mode
also retains the independent original image before NeverD imports its state.
These tests do not qualify bare-metal firmware, SMI selection/entry emulation,
SGX/CET/VMX interactions, faults or asynchronous interruption.

## Terminal native SGX state and original instruction sites

`<sgxcontextintrin.h>` exposes the 144-byte `__sgx_transfer_context` with sixteen
complete GPRs in encoding order, RFLAGS and a standard XSAVE image pointer.
`__sgx_transfer` emits `llvm.x86.enclu.transfer.context(ptr)` for EENTER,
ERESUME or EEXIT (low EAX 2/3/4). Other leaves trap before xstate import or
target-stack installation. The full input RAX, including its upper half,
is retained. A final terminal pseudo imports x87/SSE/AVX, flags and all GPRs,
then emits ENCLU/UD2 without an epilogue. The existing C frame is abandoned.

`__sgx_transfer_at(context, constant_pc)` emits
`llvm.x86.enclu.transfer.at.context(ptr, i64 immarg)`. It performs the same
state import followed by a RIP-relative indirect jump through a code-local
address literal. The literal is naturally aligned so CPL3 alignment checking
with CR0.AM=1 and restored AC=1 cannot fault on the jump's own memory read.
This consumes no restored GPR and preserves flags. The
original immutable executable ENCLU bytes and native continuation/AEP must
remain mapped. EENTER therefore exposes the original next PC in RCX; it is
not silently rebound to a different generated address. Subsequent enclave
execution, SSA restoration and AEX use the actual processor and original
native runtime. The direct interface's generated return PC deliberately traps
and instead requires a protocol that never returns to that address.

Both interfaces require native code64 SGX, XCR0=7, valid aligned images,
disjoint accessible storage, no concurrent mutation, CPL3-realizable POPF
flags and disabled CET. Input transport is not atomic and supplies no
original-PC fault recovery midway through its instructions. FS/GS caches,
EPC/TCS/SSA and other untransported state belong to the native environment.
These NeverC SGX interfaces remain x64-only; the full LLVM fork also provides code32
and x32 lowering, including their original-site branch encodings.

`sync-x86-sgx-transfer-context.py` owns the pseudos' complete GPR/FS/GS/flags
definitions, incoming compiler RSP use, conservative memory effects and
terminal barriers. The intrinsic generator owns signatures, noreturn effects
and the constant-site operand. `X86SGXTransferContextTest.*` exercises the
installed header, both native interfaces, all three x64 object formats and
invalid signatures/features/nonconstant sites. Run
`tests/neverc/X86/verify_sgx_transfer_context.py --llvm-bin LLVM_BIN --neverc
COMPILER --output OUTPUT` and repeat with `--original-site` for KVM input
transport comparison across compiler backends, optimization levels and modes.
The oracle stops immediately before ENCLU and executes **zero enclave
instructions**. It compares every represented GPR, flags, unchanged memory
and x87/SSE/AVX against independent original XRSTOR bytes; missing-XRSTOR and
wrong-original-site mutations must fail. CR0.AM stays enabled throughout,
including inputs with AC set. It does not certify successful
enclave entry, exit, resume or AEX on a machine without SGX.

The November 1 qualification passes 36 compiler/mode/optimization profiles
across the two interfaces: 331,776 input-state observations and 864 rejected
invalid leaves. This includes code32/code64/x32 through full LLVM, code64
through NeverC and both LLVM backend pass managers. All 13 adjacent frontend
tests and 26 generator tests pass. The shared XSAVE fixture also passes six
SMM compiler profiles with 18,432 complete native state pairs. These counts
retain the instruction-boundary scope above: zero enclave instructions ran.

## Protected far-pointer compiler qualification

`tests/neverc/X86/verify_protected_far_pointer.py --llvm-bin LLVM_BIN
--mode 16 --output OUTPUT` (or `--mode 32`) compares all ten LDS/LES/LSS/LFS/LGS
word/dword intrinsics with independent original bytes in KVM. Used and unused
results run at O0 and optimized IR/O2. The matrix covers null and non-null
selectors, RPL/DPL/type/present checks, flat/nonflat caches, word stacks,
unaligned/cross-page/read-only pointer images, descriptor reads and hardware
#GP/#NP/#SS/#PF entry. Native offset publication, GPRs, flags, caches, CR2,
active event state and RAM are compared at the instruction or fault handler.
Only successful cases preserving the compiler ABI's segment invariants continue
to verify result projection, stack balance and callee-saved registers.

Original fault handlers identify their vector through OUT; the runner completes
KVM's pending I/O before observing or replacing state. Inactive exception records
are excluded only after checking that neither a pending nor injected exception
exists. KVM retains old payload fields after delivery; this is not guest-visible
fault state. The remaining event state and authenticated restart frames still
compare exactly. See the [KVM API](https://www.kernel.org/doc/html/latest/virt/kvm/api.html#kvm-get-vcpu-events)
and its [I/O completion contract](https://www.kernel.org/doc/html/latest/virt/kvm/api.html#the-kvm-run-structure).
The oracle rejects a wrong pointer loaded before an unchanged far-load opcode.
It does not qualify a NeverC i386 frontend, real/VM86, CPL3 alignment checking,
MOV-SS inhibition or asynchronous events.

## Native full-dividend division

`__builtin_ia32_udivrem8/16/32/64` and `__builtin_ia32_sdivrem8/16/32/64`
accept `low, high, divisor, remainder_pointer`, return the quotient, and write
the remainder after the instruction completes. All integer arguments/results
are unsigned bit-pattern carriers at the indicated width (`unsigned char`,
`unsigned short`, `unsigned int`, `unsigned long long`). The signed operation
interprets the entire double-width dividend and divisor as two's complement.
Each builtin emits the matching pair-valued LLVM intrinsic and one DIV/IDIV.
The existing `_udiv128(high, low, divisor, remainder_pointer)` uses the same
64-bit operation, retaining its Microsoft parameter order.

Zero divisors and unrepresentable quotients produce actual #DE, even for
unused results. These are not ordinary C `/` or `%` operations with undefined
exceptional inputs. Native calls have conservative memory effects, nounwind,
convergent and noduplicate attributes. Compiler ABI state remains compiler-owned;
flags are undefined and are not outputs. The pointer store is a subsequent C
memory effect: a fault on that store does not turn the operation into memory DIV.

`X86NativeDivision.h` contains the same backend lowering in the full LLVM fork
and NeverC's trimmed backend. `sync-x86-native-intrinsics.py` owns all eight
pair signatures and updates intrinsic identities and DAG references together.
`NativeDivisionFrontendTests.cpp` checks native IR, unused effects, source types
and Microsoft overflow on ELF/COFF/Mach-O at O0/O2. Run
`tests/neverc/X86/verify_native_division.py --llvm-bin LLVM_BIN --neverc NEVERC
--output OUTPUT` for original-instruction comparisons through direct LLVM,
NeverC and NeverC IR through both LLVM backends. The declared i386 matrix uses
full LLVM; the NeverC division APIs remain in its x64 builtin registry. A 32-bit libc development
installation is required to run the native i386 signal oracle.

The full LLVM fork also lowers the 8/16/32-bit pairs in protected code16,
using its 32-bit stack ABI and mode-correct operand-size prefixes. The 64-bit
pairs remain long-mode-only. This does not provide code16 source support or
i386/code16 machine code in NeverC's trimmed backend.

Run `tests/neverc/X86/verify_protected_division.py --llvm-bin LLVM_BIN
--mode 16 --output OUTPUT` (or `--mode 32`) on Linux with KVM for native
protected-mode qualification. It checks argument transport at the instruction,
then compares GPRs, flags, system registers/caches, event state and RAM with an
independently encoded DIV/IDIV on another vCPU. The oracle authenticates both
restart PCs before normalizing their intentional difference in #DE frames.
Successful calls also verify quotient/remainder stores, callee-saved registers
and stack balance; unused results retain division and its faults. Erased DIV
and a wrong divisor loaded before an intact DIV are rejected controls.
The matrix covers CPL0, byte/word/dword operands, signed/unsigned full dividends,
all four result-use combinations, O0 and optimized IR/O2. It does not qualify
operand-memory faults, asynchronous events, other privilege levels or physical
processors beyond those recorded in the qualification.

## Native memory bit strings

`__builtin_ia32_{bt,bts,btr,btc}_mem{16,32,64}(base, signed_index)` returns
an `unsigned char` carry bit. The index is `short`, `int` or `long long`;
BT accepts a `const void *`, and the updating forms accept `void *`.
Each emits the corresponding `llvm.x86.*.memN` intrinsic and one native
memory instruction. The full signed register index can select a word before
or after the base; even constant indices preserve that behavior. Complete
word/dword/qword access and faults remain native for unused return values.

The Microsoft `_bittest*` and `_bittestand{set,reset,complement}*` interfaces
now use these native LLVM calls instead of inline assembly. Their interlocked
variants use the existing `llvm.x86.atomic.*.rm` intrinsics and retain LOCK.
The signed index makes a fixed memory range at the base inaccurate; native
machine metadata therefore conservatively aliases unknown memory. The new
unlocked APIs make no concurrent atomicity claim.

`X86NativeBitTest.h` owns lowering in both backends; the intrinsic synchronizer
owns the twelve fixed signatures and their sorted IDs/DAG references.
`NativeBitTestFrontendTests.cpp` covers ELF/COFF/Mach-O O0/O2, retained unused
calls, native compatibility routes and single evaluation of arguments.
`verify_native_bit_tests.py --llvm-bin LLVM_BIN --neverc NEVERC --output DIR`
compares return carry, complete fixture RAM, signed/extreme indices, alignment,
real memory faults and marker order with independent original instructions.
It checks LLVM's legacy/new pass managers, NeverC, and NeverC IR through both
LLVM paths, and rejects a control that keeps CF but erases one BTS store.
The i386 runtime matrix uses full LLVM; these NeverC APIs remain in its x64 builtin registry.
The full LLVM code16 route is checked by compiler regressions, not claimed as
protected KVM execution of compiler-generated bit-string functions.

`_BitScanForward` and `_BitScanReverse` continue to use native `cttz/ctlz`
code generation, with their C API's zero-source output preservation. They do
not provide a complete architectural flag-state API.

## Exact-width FLAGS interfaces

`__builtin_ia32_readeflags_u16()` returns `unsigned short` and
`__builtin_ia32_writeeflags_u16(unsigned short)` writes exactly that word.
Both lower to native `llvm.x86.flags.{read,write}.u16`, with no inline assembly.
The word write preserves AC/ID; extending it to `__writeeflags` would change
those upper flags. `__readeflags`/`__writeeflags` select their natural i386/x64
instruction width as described in [Native FLAGS image widths](#native-flags-image-widths).
Native privilege restrictions and faults still apply.

`RDFLAGS16`/`WRFLAGS16` use a GR16 carrier and a two-byte push/pop pair,
with the same frame and red-zone ownership as the qword APIs. The compiler
continues to own its ABI and stack; flag images do not establish arbitrary
original-image continuations or make arithmetic flags live across other C
operations. The initial NeverC qualification used its x64 frontend; the full
LLVM fork additionally supports i386/code16. The i386 source route below now
exposes the shared word FLAGS builtins through LLVM IR.

[utils/lint/sync-x86-flags16.py](utils/lint/sync-x86-flags16.py) owns the appended machine descriptors and
printers. The shared native-intrinsic synchronizer owns signatures and
DAGISel ID remapping. `X86FlagsFrontendTest.*` checks both optimization levels,
ELF/COFF/Mach-O, native assembly, unused reads, memory ordering and malformed
argument lists. [tests/verify-flags16-completion.py](tests/verify-flags16-completion.py) compares compiled writes
with original word POPF, including every safe FLAGS word and all AC/ID images.
Its default x64 driver checks NeverC, full LLVM/Clang and unchanged emitted C;
`--bits 32` uses a separate cdecl assembly witness for full LLVM/Clang and
i386 emitted C, including NeverC with `--backend-llc`. Both drivers restore the host flags before returning to C,
preserve callee-saved registers, and require a widened-write negative control
to fail. The i386 driver needs the host's multilib C runtime and qualifies the
Linux compiler ABI; arbitrary original machine-state execution remains separate.

## Legacy BOUND qualification in the full LLVM fork

The full LLVM/Clang fork supports `__builtin_ia32_bound16(short, const void *)`
and `__builtin_ia32_bound32(int, const void *)`. The `_address32` variants take
an index, unsigned offset, constant address width (16/32), and constant segment
prefix (0, 26h, 2Eh, 36h, 3Eh, 64h or 65h). All return void and retain native
conditional memory accesses, faults and ordering, including unused calls.
The native LLVM instruction descriptors read the index without writing it or
EFLAGS. The pointer interface always uses a 32-bit DS offset, including code16;
explicit interfaces can select address16 and a nonflat segment.

BOUND has no long-mode encoding. These APIs belong to the full fork's i386 and
code16 targets. The initial qualification did not enable NeverC i386; the
source ABI work below adds that frontend route. NeverC's pruned x64 machine
tables still do not provide i386 code generation.

`tests/neverc/X86/verify_bound.py --llvm-bin LLVM_BIN --clang FULL_CLANG
--output OUT` executes original-byte ELF32 witnesses at O0/O2, with live,
unused and repeated native calls. It checks inclusive signed limits, untouched
full-width return values, nonflat FS, address16 wrap, read-only/missing pages,
segment limits and lower-before-upper faults. A changed index with an intact
BOUND must be rejected. Multilib libc and Linux modify_ldt are required.

`verify_bound_source.py --exports NEVERD_EXPORTS --llvm-bin LLVM_BIN
--clang CLANG --output OUT` compiles all 336 actual HighC/LLVM-to-C outputs from
`X86BoundSource.*`, across three object-format contexts and both pipeline
optimization settings. It executes the 72 flat-DS/nonflat-FS C cases plus twelve actual exported ELF
objects and rejects
an intact-BOUND control with a truncated return. Run once with stock Clang's
assembly path and once with the full fork's builtins.

`verify_protected_bound.py --llvm-bin LLVM_BIN --mode 16|32 --output OUT`
executes the complete compiler-generated functions in KVM and compares native
BOUND boundaries, all GPRs, flags, caches, event state, RAM and #BR/#GP/#PF/#AC
frames with independent original bytes. ABI-compatible successful checks return
through the compiler frame. Non-ABI DS changes stop at the instruction boundary.
The witness rejects changed input transport and truncated returned values.
These tests do not qualify real/VM86 mode, asynchronous delivery, device memory
or physical MPX; the NeverD software MPX owner is tested separately.

## i386 source ABI and BOUND builtins

NeverC now has an i386 Linux source target with an ILP32 data model, 12-byte
x87 `long double`, four-byte alignment for `double` and `long long`, and a
pointer `va_list`. [SysV32.cpp](neverc/lib/Emit/Target/X86/SysV32.cpp) owns its argument and return
classification, including hidden record returns, by-value stack records,
register parameters and callee stack cleanup. Legacy CPU defaults no longer
inherit long-mode macros or mandatory SSE2. The built-in target route records
32-bit pointers and distinguishes source support from machine-code support.
The C ABI implementation adapts
[Clang's X86_32ABIInfo](https://github.com/NeverSight/llvm-project/blob/0460a3fbb99808235c24003613eebd495f6c62d7/clang/lib/CodeGen/Targets/X86.cpp)
to NeverC's C23 interfaces and retains its Apache-2.0-with-LLVM-exception notice.

The four BOUND builtins described above now work in NeverC's i386 source
frontend, emitting the same native LLVM intrinsics without inline assembly.
Long-mode calls and invalid constant address controls fail during semantic
checking. The generated intrinsic signatures and immediate attributes are
maintained by [the intrinsic synchronizer](utils/lint/sync-x86-native-intrinsics.py).

NeverC can emit i386 ELF objects and assembly directly when an explicit full
NeverSight LLVM backend is selected. Its normal frontend and optimization
pipeline runs first; the backend receives verified LLVM IR in a private
workspace. No LLVM C++ objects cross the process boundary:

```sh
neverc --target=i386-unknown-linux-gnu -O2 -c input.c -o input.o \
  -fllvm-backend=/path/to/neversight-llvm/bin/llc
```

The full fork must answer `--neversight-codegen-protocol` with
`neversight-codegen-v1 x86`. Its strict `--neversight-codegen` mode rejects
unknown reserved `llvm.*` names instead of lowering them to external calls.
The adapter enables machine verification and validates a complete little-endian
ELF32 `EM_386` relocatable object before publishing it. Bitcode
input, `-S`, AT&T/Intel assembly, optimization level, CPU/features, code model,
section options and ordinary DWARF use the same route. The full fork also assembles complete
Intel OFFSET expressions while retaining i386 GOTPC relocations, so PIC
assembly can be assembled and linked again. Textual IR input remains
unsupported by the embedded IR reader; textual IR output remains available. Source PIC/PIE flags use
the last specified option to own i386 macros, IR module flags and relocation;
the Linux default remains large PIE. Imported IR retains its own module flags.

The embedded backend remains pruned to x86-64. Without the explicit full
backend, i386 machine output still fails. This route disables implicit LTO;
explicit LTO, `-mllvm`, split DWARF, integrated linking and custom target/MIR/MC
or DynCode machine hooks are rejected. IR-only output remains available without
an external backend. Link emitted objects with a separate i386 toolchain. This
interface does not restore the embedded register/frame/MC/linker implementation
or install a new production LLVM SDK.

`verify_i386_machine_backend.py --neverc NEVERC --llvm-bin LLVM_BIN --output OUT`
checks direct objects and assembly, static/PIC/PIE executable and shared-library
relocations, TLS, sections, debug info, bitcode input, stdout, paths containing
spaces, protocol/output failures and workspace cleanup. The ABI, intrinsic-header,
protected far-pointer, ARPL-source and BOUND-source qualification scripts accept
`--machine-backend` to exercise actual NeverC C-to-object compilation through
this entry point before their independent runtime/state oracles. The far-pointer
fixture also qualifies protected code16 with its existing 32-bit stack/pointer
ABI; this is not a 16-bit C data model or real/VM86 execution contract.

The source route covers Linux ELF. Other 32-bit platforms and register
conventions such as fastcall/vectorcall are not enabled. There is no bundled
i386 runtime or sysroot; this is not a claim of a complete 32-bit toolchain.

`tests/neverc/X86/verify_i386_abi.py --neverc NEVERC --llvm-bin LLVM_BIN
--clang FULL_CLANG --output OUT` cross-links NeverC and Clang callers/callees
at O0/O2. It checks scalar, record, packed/aligned, complex, vector, x87,
`_BitInt`, variadic, register-parameter and callback transport. Wide scalars
also check exhaustion of the register-parameter bank before indirect passing;
variadic calls and `va_arg` reuse the ordinary argument classifier. It also verifies
bitcode and rejects an intact call graph whose 64-bit return is truncated.
`verify_bound.py --neverc NEVERC --llvm-bin LLVM_BIN --output OUT` adds actual
NeverC-generated BOUND functions to the original-byte CPU comparison matrix.
`verify_bound_source.py --exports NEVERD_EXPORTS --neverc NEVERC
--llvm-bin LLVM_BIN --clang HOST_CLANG --output OUT` also compiles all 336
unchanged HighC/LLVM-to-C exports through native builtin IR, checks the full
LLVM machine verifier, and executes 72 source cases plus twelve exported ELF
objects. No BOUND assembly fallback may appear in the NeverC-generated IR.

This ABI work also exposed a Clang i386 `va_arg` bug: wide `_BitInt` arguments
were passed by address but read as values directly from the argument slot.
The full fork now uses the ABI classifier's indirect/byval distinction and
advances the slot by pointer size. The interoperation witness includes 65- and
97-bit variadic values, `va_copy`, and trailing scalar arguments; it requires
that Clang fix, not a stock Clang with the incorrect reader. Clang's focused
regression also checks Windows i386 and preserves direct integer/record slots.

#!/usr/bin/env python3
"""Execute compiler-owned VMRUN contexts in disposable AMD SVM TCG guests.

Checks all fourteen software-managed GPRs, x87, XMM/YMM, caller frames,
VMMCALL/HLT intercepts, invalid VMCBs and addr32 physical-address truncation.
The separate original-byte IDA counterexample runs in every guest. This does
not qualify physical AMD hardware, supervisor state or generic SVM emulation.
"""

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess

BASE = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location(
    "guest", BASE / "tests/verify-descriptor-state-qemu.py")
GUEST = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GUEST)
FIXTURE = BASE / "tests/fixtures/x86/svm-context/guest.c"


def boot(mode):
    text = GUEST.boot(mode).replace("  call probe", """  mov %cr0,%rax
  and $-13,%rax
  or $0x22,%rax
  mov %rax,%cr0
  mov %cr4,%rax
  or $0x40600,%rax
  mov %rax,%cr4
  xor %ecx,%ecx
  mov $7,%eax
  xor %edx,%edx
  xsetbv
  call probe""")
    registers = ("rbx", "rcx", "rdx", "rsi", "rdi", "rbp",
                 *(f"r{i}" for i in range(8, 16)))
    text += "\n.text\n.code64\n"
    for name, exit_op in (("guest_vmmcall", "vmmcall"), ("guest_hlt", "hlt")):
        text += f".global {name}\n{name}:\n"
        text += "".join(f"  add ${i+1}, %{reg}\n" for i, reg in enumerate(registers))
        text += "  add $7,%rax\n  sub $16,%rsp\n  fninit\n  fld1\n"
        text += "".join(f"  vpaddq addend(%rip),%ymm{i},%ymm{i}\n" for i in range(16))
        text += f"  {exit_op}\n  ud2\n"
    return text + """.global guest_rcx
guest_rcx:
  mov $0x5678,%ecx
  vmmcall
  ud2
.global ida_vmrun_rcx_result
ida_vmrun_rcx_result:
  mov %rdi,%rax
  mov $0x1234,%ecx
  vmrun %rax
  mov %rcx,%rax
  ret
.section .rodata
.balign 32
addend: .quad 0x1234,0x1234,0x1234,0x1234
.section .note.GNU-stack,"",@progbits
"""


def source(args, llvm_route):
    code = FIXTURE.read_text()
    if args.baseline:
        before = code[:code.index("__attribute__((noinline)) static U64 invoke64")]
        return before + """unsigned probe(void) {
  msr(0xc0000080,0x1500); msr(0xc0010117,(U64)hsave);
  init_vmcb(guest_rcx);
  U64 actual=ida_vmrun_rcx_result((U64)vmcb);
  if(actual!=0x5678) return fail(8,actual);
  put('I'); put('D'); put('A'); put(':'); put('5'); put('6'); put('7'); put('8'); put('\\n');
  return 16;
}
""", 1
    if args.neverd_source:
        code = (args.neverd_source.read_text() +
                "#define VMRUN64(p) ((void)vmrun_rcx_result((void *)(p)))\n"
                "#define NEVERD_RCXSOURCE 1\n" + code)
    if llvm_route:
        code = ('extern void __builtin_ia32_vmrun_context(void *, unsigned) '
                '__asm__("llvm.x86.vmrun.context");\n' + code)
    return code, 49


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    route = parser.add_mutually_exclusive_group()
    route.add_argument("--llc", type=Path)
    route.add_argument("--backend-llc", type=Path)
    route.add_argument("--baseline", action="store_true")
    parser.add_argument("--compiler", type=Path, default=BASE / "build-neverc/bin/neverc")
    parser.add_argument("--keep", type=Path, required=True)
    parser.add_argument("--opt", choices=("O0", "O2"), action="append")
    parser.add_argument("--neverd-source", type=Path,
                        help="execute the actual exported LLVMC vmrun_rcx_result wrapper")
    args = parser.parse_args()
    for option, tool in (("clang", "clang"), ("linker", "ld.lld"),
                         ("objcopy", "objcopy"), ("qemu", "qemu-system-x86_64")):
        setattr(args, option, shutil.which(tool))
        if not getattr(args, option):
            parser.error(f"{tool} is required")
    if args.baseline:
        args.compiler = Path(args.clang)
    for key in ("compiler", "llc", "backend_llc", "keep", "neverd_source"):
        if getattr(args, key):
            setattr(args, key, getattr(args, key).resolve())
    if args.baseline and args.neverd_source:
        parser.error("--baseline cannot execute a NeverD projection")
    args.keep.mkdir(parents=True, exist_ok=True)
    tools = {str(path): digest(path) for path in
             (args.compiler, args.llc, args.backend_llc, args.clang,
              args.linker, args.objcopy, args.qemu) if path}
    commands, profiles = [], []
    original = subprocess.run

    def run(command, *positional, **kwargs):
        command = list(map(str, command))
        if args.baseline:
            command = [value for value in command if value != "-fno-builtin-mimalloc"]
        # VMCB/xstate byte images and exported LLVMC use typed pointer views.
        if command[0] in (str(args.compiler), args.clang) and "-ffreestanding" in command:
            command += ["-fno-strict-aliasing"]
        if command[0] == args.qemu:
            command += ["-debugcon", "stdio"]
        result = original(command, *positional, **kwargs)
        commands.append({"argv": command, "returncode": result.returncode})
        (args.keep / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        if command[0] == args.qemu:
            kernel = Path(command[command.index("-kernel") + 1])
            (args.keep / (kernel.stem + ".run.log")).write_text(result.stdout + result.stderr)
            print(result.stdout, end="", flush=True)
        return result

    subprocess.run = run
    try:
        for opt in args.opt or ("O0", "O2"):
            GUEST.check_guest(args, args.keep, 64, opt,
                              lambda mode, route: source(args, route), boot)
            profiles.append({"optimization": opt,
                             "contexts": 0 if args.baseline else 49,
                             "original_ida_counterexample": True})
            message = ("original VMRUN returned RCX=0x5678" if args.baseline else
                       "49 returning VMRUN contexts and IDA counterexample passed")
            print(f"{opt}: {message}", flush=True)
    finally:
        subprocess.run = original
    assert tools == {path: digest(path) for path in tools}, "compiler changed during execution"
    record = {"passed": True, "profiles": profiles, "tools": tools,
              "fixture_sha256": digest(FIXTURE), "verifier_sha256": digest(__file__),
              "neverd_source_sha256": digest(args.neverd_source) if args.neverd_source else None,
              "files": {path.name: digest(path) for path in sorted(args.keep.iterdir())
                        if path.is_file() and path.name != "qualification.json"},
              "scope": "QEMU TCG long-mode SVM, XCR0=7; no physical AMD qualification"}
    (args.keep / "qualification.json").write_text(json.dumps(record, indent=2) + "\n")


if __name__ == "__main__":
    main()

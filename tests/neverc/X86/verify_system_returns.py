#!/usr/bin/env python3
"""Qualify SYSRET/SYSEXIT operand transport and transfers on native KVM."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import time


ROOT = Path(__file__).resolve().parent
OPERATIONS = ("sysret32", "sysret64", "sysexit32", "sysexit64")
VARIANTS = ("plain", "fixed", "dynamic", "pressure")
PROFILES = tuple(f"{compiler}-O{level}" for compiler in
                 ("llvm", "neverc", "neverc-ir") for level in (0, 2))


def sources(protected_mode=False):
    ir, c = "", ""
    for kind, operation in enumerate(OPERATIONS):
        if protected_mode and operation != "sysexit32":
            continue
        bits = 64 if kind & 1 else 32
        sysret = kind < 2
        args = f"i{bits}, i{bits}, i64" if sysret else f"i{bits}, i{bits}"
        ir += f"declare void @llvm.x86.{operation}({args})\n"
        for variant in VARIANTS:
            name = f"generated_{operation}_{variant}"
            frame_ir, frame_c, pc = "", "", "%pc"
            if variant == "pressure":
                # All volatile loads precede every use, keeping sixteen values
                # live alongside the terminal inputs and forcing real spills.
                frame_ir = "  %frame = alloca [32 x i64], align 32\n"
                frame_c = "  volatile unsigned long long frame[32];\n"
                for i in range(16):
                    frame_ir += (f"  %p{i} = getelementptr [32 x i64], ptr %frame, i64 0, i64 {i}\n"
                                 f"  store volatile i64 {i + 17}, ptr %p{i}\n")
                    frame_c += f"  frame[{i}] = {i + 17};\n"
                for i in range(16):
                    frame_ir += f"  %v{i} = load volatile i64, ptr %p{i}\n"
                    frame_c += f"  unsigned long long v{i} = frame[{i}];\n"
                for i in range(16):
                    frame_ir += (f"  %q{i} = getelementptr [32 x i64], ptr %frame, i64 0, i64 {i + 16}\n"
                                 f"  store volatile i64 %v{i}, ptr %q{i}\n")
                    frame_c += f"  frame[{i + 16}] = v{i};\n"
                for i in range(16):
                    previous = "%pc" if not i else f"%sum{i - 1}"
                    frame_ir += f"  %sum{i} = add i64 {previous}, %v{i}\n"
                frame_ir += "  %restored = sub i64 %sum15, 392\n"
                frame_c += "  pc += " + " + ".join(f"v{i}" for i in range(16)) + " - 392;\n"
                pc = "%restored"
            elif variant != "plain":
                allocation = ("alloca [512 x i8], align 32" if variant == "fixed"
                              else "alloca i8, i64 %n, align 32")
                frame_ir = f"""  %n = add i64 %size, 32
  %frame = {allocation}
  store volatile i8 17, ptr %frame
  %loaded = load volatile i8, ptr %frame
  %delta = zext i8 %loaded to i64
  %adjusted = add i64 %pc, %delta
  %restored = sub i64 %adjusted, 17
"""
                pc = "%restored"
                length = "512" if variant == "fixed" else "size + 32"
                frame_c = f"""  volatile unsigned char frame[{length}];
  frame[0] = 17;
  pc += (unsigned long long)frame[0] - 17;
"""
            convert = ""
            if bits == 32:
                convert = f"  %pc32 = trunc i64 {pc} to i32\n"
                convert += "  %flags32 = trunc i64 %flags to i32\n"
                convert += "  %sp32 = trunc i64 %sp to i32\n"
            pc_value = pc if bits == 64 else "%pc32"
            operands = (f"i{bits} {pc_value}, i{bits} " +
                        ("%flags" if bits == 64 else "%flags32") + ", i64 %sp"
                        if sysret else f"i{bits} {pc_value}, i{bits} " +
                        ("%sp" if bits == 64 else "%sp32"))
            ir += f"""
define void @{name}(i64 %pc, i64 %flags, i64 %sp, i64 %size, ptr %marker) #0 {{
{frame_ir}  store volatile i32 17, ptr %marker
{convert}  call void @llvm.x86.{operation}({operands})
  unreachable
}}
"""
            c += f"""
[[noreturn]] void {name}(unsigned long long pc, unsigned long long flags,
    unsigned long long sp, unsigned long long size, volatile unsigned *marker) {{
{frame_c}  *marker = 17;
  __builtin_ia32_{operation}(pc, {"flags, sp" if sysret else "sp"});
  *marker = 23;
}}
"""
    ir += 'attributes #0 = { "frame-pointer"="all" }\n'
    return ir, c


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--llvm-bin", required=True, type=Path)
    parser.add_argument("--neverc", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--profile", choices=PROFILES, action="append")
    parser.add_argument("--i386", action="store_true",
                        help="qualify LLVM SYSEXIT32 in protected 32-bit mode")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TMPDIR=str(output))
    records = []

    def run(command, stem, timeout=240, expected=0):
        command = [str(item) for item in command]
        started = time.monotonic()
        result = subprocess.run(command, text=True, capture_output=True,
                                env=env, timeout=timeout)
        (output / f"{stem}.stdout").write_text(result.stdout)
        (output / f"{stem}.stderr").write_text(result.stderr)
        records.append(dict(command=command, exit_code=result.returncode,
                            seconds=time.monotonic() - started,
                            stdout=f"{stem}.stdout", stderr=f"{stem}.stderr"))
        (output / "commands.json").write_text(json.dumps(records, indent=2) + "\n")
        if result.returncode != expected:
            raise RuntimeError(f"{stem}: exit {result.returncode}\n{result.stderr}")
        return result.stdout

    tools = {name: shutil.which(name) for name in ("clang", "ld.lld", "objcopy")}
    if not all(tools.values()):
        parser.error("clang, ld.lld and objcopy are required")
    for name in ("llc", "opt", "llvm-objdump"):
        tools[name] = str((args.llvm_bin / name).resolve())
    tools["neverc"] = str(args.neverc.resolve())
    profiles = args.profile or (PROFILES[:2] if args.i386 else PROFILES)
    if args.i386 and any(not profile.startswith("llvm-") for profile in profiles):
        parser.error("NeverC's native target is x86-64")
    ir, c = sources(args.i386)
    (output / "input.ll").write_text(ir)
    (output / "input.c").write_text(c)
    entry = "generated_sysexit32_plain" if args.i386 else "generated_sysret32_plain"
    (output / "guest.ld").write_text(f"ENTRY({entry})\n" + """
SECTIONS {
  . = 0x10000;
  .text : { *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss : { *(.bss*) }
  /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }
}
""")
    runner = output / "kvm-runner"
    run([tools["clang"], "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
         ROOT / "system_return_kvm.c", "-o", runner], "host-build")
    results = []
    for profile in profiles:
        compiler, level = profile.rsplit("-", 1)
        obj = output / f"{profile}.o"
        source = output / "input.ll"
        if compiler.startswith("neverc"):
            options = [tools["neverc"], "--target=x86_64-unknown-linux-gnu",
                       "-std=c23", f"-{level}", "-fno-lto", "-ffreestanding",
                       "-fno-builtin-mimalloc", "-fno-stack-protector",
                       "-fno-pic", "-fno-omit-frame-pointer", "-mgeneral-regs-only",
                       "-Werror", "-mllvm", "-verify-machineinstrs"]
            if compiler == "neverc":
                run([*options, "-c", output / "input.c", "-o", obj], profile + "-compile")
            else:
                source = output / f"{profile}.ll"
                run([*options, "-S", "-emit-llvm", output / "input.c", "-o", source],
                    profile + "-ir")
        if compiler != "neverc":
            if level == "O2":
                optimized = output / f"{profile}-optimized.ll"
                run([tools["opt"], "-S", "-passes=default<O2>", source,
                     "-o", optimized], profile + "-opt")
                source = optimized
            triple = "i386-unknown-linux-gnu" if args.i386 else "x86_64-unknown-linux-gnu"
            run([tools["llc"], f"-mtriple={triple}", f"-{level}",
                 "-verify-machineinstrs", "-filetype=obj", source, "-o", obj],
                profile + "-compile")
        elf, binary = output / f"{profile}.elf", output / f"{profile}.bin"
        run([tools["ld.lld"], "-m", "elf_i386" if args.i386 else "elf_x86_64",
             "-T", output / "guest.ld", obj, "-o", elf], profile + "-link")
        run([tools["objcopy"], "-O", "binary", elf, binary], profile + "-image")
        assembly = run([tools["llvm-objdump"], "-d", elf], profile + "-disassembly")
        functions = re.findall(r"^([0-9a-f]+) <(generated_\w+)>:\n(.*?)(?=\n[0-9a-f]+ <|\Z)",
                               assembly, re.M | re.S)
        assert len(functions) == (4 if args.i386 else 16), assembly
        manifest = []
        narrow_move = None
        for address, name, body in functions:
            operation = next(op for op in OPERATIONS if name.startswith("generated_" + op + "_"))
            mnemonic = operation[:-2] + ("q" if operation.endswith("64") else "l")
            instructions = re.findall(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2}[ \t]+)+\s*([^\n]+)", body, re.M)
            terminals = [(int(pc, 16), text) for pc, text in instructions
                         if re.match(r"(?:sysret|sysexit)[lq]\b", text)]
            assert len(terminals) == 1 and terminals[0][1].strip() == mnemonic, body
            assert not any(re.match(r"(?:call|ret)[lq]?\b", text) for _, text in instructions), body
            index = next(i for i, (pc, _) in enumerate(instructions)
                         if int(pc, 16) == terminals[0][0])
            if operation.startswith("sysret"):
                assert re.match(r"movq\s+%\w+, %rsp\b", instructions[index - 1][1]), body
            if args.i386 and name == "generated_sysexit32_plain":
                move_index = next(i for i, (_, text) in enumerate(instructions[:index])
                                  if re.match(r"movl\s+.*, %ecx\b", text))
                narrow_move = (int(instructions[move_index][0], 16),
                               int(instructions[move_index + 1][0], 16))
            # Padding after a terminal is allowed; no executable frame cleanup.
            assert all(re.match(r"(?:nop\w*|int3|data16|cs nop)\b", text)
                       for _, text in instructions[index + 1:]), body
            manifest.append(f"{name} {address} {terminals[0][0]:x} {OPERATIONS.index(operation)}")
        manifest_path = output / f"{profile}.manifest"
        manifest_path.write_text("\n".join(manifest) + "\n")
        mode = "32" if args.i386 else "64"
        native = json.loads(run([runner, binary, manifest_path, mode], profile + "-native"))
        assert native["complete_state_pairs"] == (21760 if args.i386 else 87040), native
        # Keep the instruction identity and move adjacency, but feed SYSRET's
        # PC into RSP. The independent operand check must reject this candidate
        # before allowing a comparison against the captured boundary state.
        corrupted = bytearray(binary.read_bytes())
        if args.i386:
            start, stop = narrow_move
            assert stop - start >= 2
            corrupted[start - 0x10000:stop - 0x10000] = bytes.fromhex("31 c9") + b"\x90" * (stop - start - 2)
        else:
            plain = next(line.split() for line in manifest
                         if line.startswith("generated_sysret32_plain "))
            switch_offset = int(plain[2], 16) - 0x10000 - 3
            move = corrupted[switch_offset:switch_offset + 3]
            assert move[0] in (0x48, 0x4c) and move[1] == 0x89 and move[2] & 0xc7 == 0xc4
            corrupted[switch_offset:switch_offset + 3] = bytes.fromhex("48 89 cc")
        negative = output / f"{profile}-wrong-stack.bin"
        negative.write_bytes(corrupted)
        run([runner, negative, manifest_path, mode], profile + "-negative", expected=1)
        diagnostic = (output / f"{profile}-negative.stderr").read_text()
        assert ("boundary.rcx" in diagnostic or "boundary_pc" in diagnostic
                if args.i386 else "boundary.rsp == stacks[pi]" in diagnostic)
        results.append(dict(profile=profile, **native, elf_sha256=sha256(elf),
                            wrong_stack_negative_control="rejected"))
        print(f"{profile}: {native['complete_state_pairs']} complete state pairs", flush=True)
    report = dict(
        scope="Native compiler input transport and successful transfers; ambient CPL/MSRs, CET disabled",
        host=platform.platform(), code_bits=32 if args.i386 else 64, profiles=results,
        source_sha256={p.name: sha256(p) for p in (Path(__file__), ROOT / "system_return_kvm.c", ROOT / "KvmTransferFixture.h")},
        tools={name: dict(path=path, sha256=sha256(path)) for name, path in tools.items()},
        reference="Intel SDM 325462-085US, Vol. 2B SYSRET/SYSEXIT; Vol. 1 3.4.1.1",
        reference_url="https://cdrdv2-public.intel.com/835781/325462-sdm-vol-1-2abcd-3abcd-4.pdf",
        exclusions=["complete original-image register restoration", "CET enabled",
                    "fault ordering", "asynchronous unwind/interrupt delivery",
                    "other CPU models",
                    "architectural promises for compatibility GPR high halves"])
    (output / "evidence.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()

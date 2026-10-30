#!/usr/bin/env python3
"""Qualify native IRET/RETF/SYSENTER compiler transport against original bytes."""

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
# Kind, operand width, immediate adjustment, expected native mnemonic.
OPERATIONS = {}
for bits, suffix in ((16, "w"), (32, "l"), (64, "q")):
    OPERATIONS[f"iret{bits}"] = (0, bits, 0, "iret" + suffix)
    OPERATIONS[f"retf{bits}"] = (1, bits, 0, "lret" + suffix)
    OPERATIONS[f"retf{bits}_imm"] = (2, bits, {16: 65535, 32: 0, 64: 32}[bits], "lret" + suffix)
OPERATIONS["sysenter"] = (3, 64, 0, "sysenter")
VARIANTS = ("plain", "fixed", "dynamic", "pressure")
PROFILES = tuple(f"{compiler}-O{level}" for compiler in
                 ("llvm", "neverc", "neverc-ir") for level in (0, 2))


def sources(protected_mode=False, operations=OPERATIONS):
    ir, c = "", ""
    for operation, (kind, width, adjustment, _) in operations.items():
        if protected_mode and width == 64 and kind != 3:
            continue
        intrinsic = operation.replace("_", ".")
        args = "" if kind >= 3 else "ptr" + (", i16" if kind == 2 else "")
        ir += f"declare void @llvm.x86.{intrinsic}({args})\n"
        for variant in VARIANTS:
            name = f"generated_{operation}_{variant}"
            frame_ir, frame_c, frame = "", "", "%destination"
            if variant == "pressure":
                frame_ir = "  %local = alloca [32 x i64], align 32\n"
                frame_c = "  volatile unsigned long long local[32];\n"
                for i in range(16):
                    frame_ir += (f"  %p{i} = getelementptr [32 x i64], ptr %local, i64 0, i64 {i}\n"
                                 f"  store volatile i64 {i + 17}, ptr %p{i}\n")
                    frame_c += f"  local[{i}] = {i + 17};\n"
                for i in range(16):
                    frame_ir += f"  %v{i} = load volatile i64, ptr %p{i}\n"
                    frame_c += f"  unsigned long long v{i} = local[{i}];\n"
                for i in range(16):
                    frame_ir += (f"  %q{i} = getelementptr [32 x i64], ptr %local, i64 0, i64 {i + 16}\n"
                                 f"  store volatile i64 %v{i}, ptr %q{i}\n")
                    frame_c += f"  local[{i + 16}] = v{i};\n"
                for i in range(16):
                    previous = "0" if not i else f"%sum{i - 1}"
                    frame_ir += f"  %sum{i} = add i64 {previous}, %v{i}\n"
                frame_ir += "  %delta = sub i64 %sum15, 392\n  %restored = getelementptr i8, ptr %destination, i64 %delta\n"
                frame_c += "  destination = (const char *)destination + (" + " + ".join(f"v{i}" for i in range(16)) + " - 392);\n"
                frame = "%restored"
            elif variant != "plain":
                allocation = "alloca [512 x i8], align 32" if variant == "fixed" else "alloca i8, i64 %n, align 32"
                frame_ir = f"  %n = add i64 %size, 32\n  %local = {allocation}\n  store volatile i8 17, ptr %local\n  %loaded = load volatile i8, ptr %local\n  %extended = zext i8 %loaded to i64\n  %delta = sub i64 %extended, 17\n  %restored = getelementptr i8, ptr %destination, i64 %delta\n"
                length = "512" if variant == "fixed" else "size + 32"
                frame_c = f"  volatile unsigned char local[{length}];\n  local[0] = 17;\n  destination = (const char *)destination + (local[0] - 17);\n"
                frame = "%restored"
            operands = "" if kind >= 3 else f"ptr {frame}" + (f", i16 {adjustment}" if kind == 2 else "")
            ir += f"\ndefine void @{name}(ptr %destination, i64 %size, ptr %marker) #0 {{\n{frame_ir}  store volatile i32 17, ptr %marker\n  call void @llvm.x86.{intrinsic}({operands})\n  unreachable\n}}\n"
            arguments = "" if kind >= 3 else "destination" + (f", {adjustment}" if kind == 2 else "")
            c += f"\n[[noreturn]] void {name}(const void *destination, unsigned long long size, volatile unsigned *marker) {{\n{frame_c}  *marker = 17;\n  __builtin_ia32_{operation}({arguments});\n  *marker = 23;\n}}\n"
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
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--smm", action="store_true", help="qualify RSM using KVM SMM save state")
    modes.add_argument("--i386", action="store_true",
                        help="qualify the code32 LLVM frame transfers")
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
    operations = {"rsm": (4, 64, 0, "rsm")} if args.smm else OPERATIONS
    ir, c = sources(args.i386, operations)
    (output / "input.ll").write_text(ir)
    (output / "input.c").write_text(c)
    entry = "generated_rsm_plain" if args.smm else "generated_iret16_plain"
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
         ROOT / ("smm_return_kvm.c" if args.smm else "frame_return_kvm.c"), "-o", runner], "host-build")
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
        expected_functions = 4 if args.smm else 28 if args.i386 else 40
        assert len(functions) == expected_functions, assembly
        manifest = []
        first_switch = None
        for address, name, body in functions:
            operation = name.removeprefix("generated_").rsplit("_", 1)[0]
            kind, width, adjustment, mnemonic = operations[operation]
            if operation == "sysenter":
                width = 32 if args.i386 else 64
            instructions = re.findall(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2}[ \t]+)+\s*([^\n]+)", body, re.M)
            terminals = [(int(pc, 16), text) for pc, text in instructions
                         if re.match(r"(?:iret[wql]|lret[wql]|sysenter|rsm)\b", text)]
            assert len(terminals) == 1 and terminals[0][1].split()[0] == mnemonic, body
            assert not any(re.match(r"(?:call|ret)[lq]?\b", text) for _, text in instructions), body
            index = next(i for i, (pc, _) in enumerate(instructions) if int(pc, 16) == terminals[0][0])
            if kind < 3:
                pattern = r"movl\s+%\w+, %esp\b" if args.i386 else r"movq\s+%\w+, %rsp\b"
                assert re.match(pattern, instructions[index - 1][1]), body
                if name == "generated_iret16_plain":
                    first_switch = int(instructions[index - 1][0], 16)
            assert all(re.match(r"(?:nop\w*|int3|data16|cs nop)\b", text)
                       for _, text in instructions[index + 1:]), body
            manifest.append(f"{name} {address} {terminals[0][0]:x} {kind} {width} {adjustment}")
        manifest_path = output / f"{profile}.manifest"
        manifest_path.write_text("\n".join(manifest) + "\n")
        mode = "32" if args.i386 else "64"
        native = json.loads(run([runner, binary, manifest_path, mode], profile + "-native"))
        assert native["complete_state_pairs"] == expected_functions * (96 if args.smm else 192), native
        # Feed an unrelated register into SP while keeping the native opcode.
        # This must fail operand transport before consulting the CPU oracle.
        corrupted = bytearray(binary.read_bytes())
        if args.smm:
            plain = next(line.split() for line in manifest if line.startswith("generated_rsm_plain "))
            offset = int(plain[2], 16) - 0x10000
            corrupted[offset:offset + 2] = bytes.fromhex("0f 0b")
        else:
            offset = first_switch - 0x10000
            corrupted[offset:offset + (2 if args.i386 else 3)] = bytes.fromhex("89 cc" if args.i386 else "48 89 cc")
        negative = output / f"{profile}-corrupted.bin"
        negative.write_bytes(corrupted)
        run([runner, negative, manifest_path, mode], profile + "-negative", expected=1)
        diagnostic = (output / f"{profile}-negative.stderr").read_text()
        assert ("smram[instruction]" if args.smm else "boundary.rsp == frame") in diagnostic
        results.append(dict(profile=profile, **native, elf_sha256=sha256(elf),
                            negative_control="wrong opcode rejected" if args.smm else "wrong stack rejected"))
        print(f"{profile}: {native['complete_state_pairs']} complete state pairs", flush=True)
    report = dict(
        scope="KVM-mediated SMM save/restore through native RSM" if args.smm else
              "Native frame input transport and successful IRET/RETF/SYSENTER transfers; CET disabled",
        host=platform.platform(), code_bits=32 if args.i386 else 64, profiles=results,
        source_sha256={p.name: sha256(p) for p in (Path(__file__), ROOT / "frame_return_kvm.c", ROOT / "smm_return_kvm.c", ROOT / "KvmTransferFixture.h")},
        tools={name: dict(path=path, sha256=sha256(path)) for name, path in tools.items()},
        reference="Intel SDM 325462-085US, Vol. 2 IRET/RET/SYSENTER; Vol. 1 3.4.1.1",
        reference_url="https://cdrdv2-public.intel.com/835781/325462-sdm-vol-1-2abcd-3abcd-4.pdf",
        exclusions=["complete original-image register restoration", "CET enabled",
                    "FRED and UIRET successful paths", "bare-metal SMM implementation",
                    "NT task return", "XSTATE",
                    "fault ordering", "asynchronous unwind/interrupt delivery",
                    "other CPU models",
                    "architectural promises for compatibility GPR high halves"])
    (output / "evidence.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()

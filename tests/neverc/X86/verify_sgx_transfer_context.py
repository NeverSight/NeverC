#!/usr/bin/env python3
"""Qualify native SGX input transport, stopping before enclave execution."""

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("frames", ROOT / "verify_frame_returns.py")
FRAMES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FRAMES)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--llvm-bin", type=Path, required=True)
    parser.add_argument("--neverc", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mode", choices=("64",), action="append")
    parser.add_argument("--backend", choices=("llvm-legacy", "llvm-new", "neverc", "neverc-ir-legacy", "neverc-ir-new"), action="append")
    parser.add_argument("--original-site", action="store_true",
                        help="restore state then enter immutable ENCLU at 0x1000")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    tools = {name: Path(shutil.which(name) or "") for name in ("clang", "ld.lld", "objcopy")}
    tools.update({name: (args.llvm_bin / name).resolve() for name in ("llc", "opt", "llvm-objdump")})
    tools["neverc"] = args.neverc.resolve()
    for name, path in tools.items():
        if not path.is_file():
            parser.error(f"missing {name}: {path}")
    backends = args.backend or ("llvm-legacy", "llvm-new", "neverc", "neverc-ir-legacy", "neverc-ir-new")
    uses_neverc = any(backend.startswith("neverc") for backend in backends)
    tool_hashes = {name: sha(path) for name, path in tools.items()
                   if name != "neverc" or uses_neverc}
    environment = dict(os.environ, TMPDIR=str(output))
    commands, profiles = [], []

    def run(command, name, expected=0):
        command = list(map(str, command))
        result = subprocess.run(command, capture_output=True, text=True, env=environment, timeout=240)
        (output / (name + ".stdout")).write_text(result.stdout)
        (output / (name + ".stderr")).write_text(result.stderr)
        commands.append(dict(command=command, exit_code=result.returncode))
        (output / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        if result.returncode != expected:
            raise RuntimeError(f"{name}: exit {result.returncode}\n{result.stderr}")
        return result.stdout

    runner = output / "sgx-input-oracle"
    run([tools["clang"], "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", ROOT / "sgx_transfer_context_kvm.c", "-o", runner], "oracle")
    linker = output / "guest.ld"
    linker.write_text('''ENTRY(generated_enclu_transfer_context_plain)
SECTIONS {
 . = 0x10000;
 .text : { *(.text*) }
 .rodata : { *(.rodata*) }
 .data : { *(.data*) }
 .bss : { *(.bss*) }
 /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }
}
''')
    for mode in args.mode or ("64",):
        bits = 64
        triple = "x86_64-unknown-linux-gnu"
        ir, c = FRAMES.sources( {"enclu_transfer_context": (0, bits, 0, "enclu")})
        intrinsic = "llvm.x86.enclu.transfer.context"
        if args.original_site:
            intrinsic = "llvm.x86.enclu.transfer.at.context"
            ir = ir.replace("@llvm.x86.enclu.transfer.context(ptr)",
                            "@" + intrinsic + "(ptr, i64)")
            ir = re.sub(r"@llvm\.x86\.enclu\.transfer\.context\((ptr %\w+)\)",
                        r"@" + intrinsic + r"(\1, i64 4096)", ir)
            c = c.replace("__builtin_ia32_enclu_transfer_context(destination)",
                          "__builtin_ia32_enclu_transfer_at_context(destination, 4096ULL)")
        ir = ir.replace('"frame-pointer"="all"', '"frame-pointer"="all" "target-features"="+sgx,+xsave,+avx"')
        raw = output / (mode + ".ll")
        raw.write_text(ir)
        source = output / (mode + ".c")
        source.write_text(c)
        for backend in backends:
            if mode != "64" and backend.startswith("neverc"):
                continue
            for level in ("O0", "O2"):
                name = f"{mode}-{backend}-{level}"
                obj = output / (name + ".o")
                flags = ["--target=" + triple, "-std=c23", "-" + level, "-fno-lto", "-ffreestanding", "-fno-builtin-mimalloc",
                         "-fno-pic", "-fno-pie", "-fno-stack-protector", "-mno-red-zone", "-msgx", "-mxsave", "-mavx"]
                lowered = raw
                if backend == "neverc":
                    run([tools["neverc"], *flags, "-c", source, "-o", obj], name + "-compile")
                else:
                    if backend.startswith("neverc"):
                        lowered = output / (name + ".ll")
                        run([tools["neverc"], *flags, "-S", "-emit-llvm", source, "-o", lowered], name + "-ir")
                        if "@" + intrinsic + "(" not in lowered.read_text() or "asm " in lowered.read_text():
                            raise RuntimeError("missing native SGX context IR")
                    if level == "O2":
                        optimized = output / (name + "-optimized.ll")
                        run([tools["opt"], "-S", "-passes=default<O2>", lowered, "-o", optimized], name + "-opt")
                        lowered = optimized
                    run([tools["llc"], "-mtriple=" + triple, "-" + level, "-filetype=obj", "-verify-machineinstrs",
                         *(["-enable-new-pm"] if backend.endswith("new") else []), lowered, "-o", obj], name + "-compile")
                elf, binary = output / (name + ".elf"), output / (name + ".bin")
                run([tools["ld.lld"], "-m", "elf_x86_64", "-T", linker, obj, "-o", elf], name + "-link")
                run([tools["objcopy"], "-O", "binary", elf, binary], name + "-image")
                assembly = run([tools["llvm-objdump"], "-d", elf], name + "-assembly")
                functions = re.findall(r"^([0-9a-f]+) <(generated_\w+)>:\n(.*?)(?=\n[0-9a-f]+ <|\Z)", assembly, re.M | re.S)
                assert len(functions) == 4, assembly
                entries, xrstor, target_literal = [], None, None
                for address, function, body in functions:
                    instructions = re.findall(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2}[ \t]+)+\s*([^\n]+)", body, re.M)
                    terminal_pattern = r"jmp[lq]?\s+\*" if args.original_site else r"enclu\b"
                    terminal = [(int(pc, 16), text) for pc, text in instructions if re.match(terminal_pattern, text)]
                    assert len(terminal) == 1, body
                    rejection = [(int(pc, 16), text) for pc, text in instructions if text == "ud2" and int(pc, 16) < terminal[0][0]]
                    assert len(rejection) == 1, body
                    destination = 0x1000 if args.original_site else terminal[0][0]
                    entries.append(f"{function} {address} {destination:x} {rejection[0][0]:x}\n")
                    if function.endswith("_plain"):
                        xrstor = next(int(pc, 16) for pc, text in instructions if text.startswith("xrstor"))
                        if args.original_site:
                            jump = terminal[0][0] - 0x10000
                            data = binary.read_bytes()
                            assert data[jump:jump+2] == b"\xff\x25"
                            operand = int.from_bytes(data[jump+2:jump+6], "little", signed=bits == 64)
                            literal_pc = terminal[0][0] + 6 + operand if bits == 64 else operand
                            assert literal_pc % (bits // 8) == 0, "unaligned original-site target"
                            target_literal = literal_pc - 0x10000
                manifest = output / (name + ".manifest")
                manifest.write_text("".join(entries))
                native = json.loads(run([runner, binary, manifest, mode], name + "-run"))
                assert native["input_state_observations"] == 9216 and native["invalid_leaves_rejected"] == 24, native
                assert native["enclave_instructions_executed"] == 0
                damaged = bytearray(binary.read_bytes())
                start = xrstor - 0x10000
                instruction = bytes.fromhex("48 0f ae 2e" if bits == 64 else "0f ae 2e")
                assert damaged[start:start+len(instruction)] == instruction
                damaged[start:start+len(instruction)] = b"\x90" * len(instruction)
                negative = output / (name + "-missing-xrstor.bin")
                negative.write_bytes(damaged)
                run([runner, negative, manifest, mode], name + "-negative", expected=1)
                assert "actual_xstate" in (output / (name + "-negative.stderr")).read_text()
                if args.original_site:
                    damaged = bytearray(binary.read_bytes())
                    word = bits // 8
                    assert int.from_bytes(damaged[target_literal:target_literal+word], "little") == 0x1000
                    damaged[target_literal:target_literal+word] = (0x1001).to_bytes(word, "little")
                    negative = output / (name + "-wrong-site.bin")
                    negative.write_bytes(damaged)
                    run([runner, negative, manifest, mode], name + "-wrong-site", expected=1)
                    assert "expected 0x1000" in (output / (name + "-wrong-site.stderr")).read_text()
                profiles.append(dict(mode=mode, backend=backend, optimization=level,
                                     original_site=args.original_site,
                                     image_sha256=sha(binary), source_sha256=sha(source if backend.startswith("neverc") else raw),
                                     **native))
                print(name, native["input_state_observations"], flush=True)
    assert tool_hashes == {name: sha(tools[name]) for name in tool_hashes}, "compiler changed during qualification"
    report = dict(profiles=profiles, input_state_observations=sum(p["input_state_observations"] for p in profiles),
                  source_sha256={p.name: sha(p) for p in (Path(__file__), ROOT / "verify_frame_returns.py", ROOT / "sgx_transfer_context_kvm.c", ROOT / "KvmTransferFixture.h", ROOT / "NativeXStateFixture.h")},
                  tools={name: dict(sha256=digest) for name, digest in tool_hashes.items()},
                  scope="KVM instruction-boundary transport only; no EPC/SSA/AEX or successful enclave instruction execution")
    (output / "qualification.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Check every native transfer's object bytes through both C compiler routes."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


# These independent opcode witnesses include the zero-immediate CA spelling.
OPERATIONS = {
    "iret16": ("frame", "66 cf"),
    "iret32": ("frame", "cf"),
    "iret64": ("frame", "48 cf"),
    "retf16": ("frame", "66 cb"),
    "retf32": ("frame", "cb"),
    "retf64": ("frame", "48 cb"),
    "retf16_imm": ("frame, 65535", "66 ca ff ff"),
    "retf32_imm": ("frame, 0", "ca 00 00"),
    "retf64_imm": ("frame, 32", "48 ca 20 00"),
    "erets": ("frame", "f2 0f 01 ca"),
    "eretu": ("frame", "f3 0f 01 ca"),
    "uiret": ("frame", "f3 0f 01 ec"),
    "sysenter": ("", "0f 34"),
    "rsm": ("", "0f aa"),
}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--llvm-bin", required=True, type=Path)
    parser.add_argument("--neverc", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    source = output / "transfers.c"
    source.write_text("".join(
        f"[[noreturn]] void generated_{name}(const void *frame) {{ "
        f"__builtin_ia32_{name}({arguments}); }}\n"
        for name, (arguments, _) in OPERATIONS.items()))
    tools = {"neverc": args.neverc.resolve(),
             **{name: (args.llvm_bin / name).resolve() for name in ("llc", "llvm-objdump")}}
    commands, results = [], []

    def run(command, stem):
        command = [str(value) for value in command]
        result = subprocess.run(command, text=True, capture_output=True,
                                timeout=120, env=dict(os.environ, TMPDIR=str(output)))
        (output / f"{stem}.stdout").write_text(result.stdout)
        (output / f"{stem}.stderr").write_text(result.stderr)
        commands.append(dict(command=command, exit_code=result.returncode,
                             stdout=f"{stem}.stdout", stderr=f"{stem}.stderr"))
        (output / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        if result.returncode:
            raise RuntimeError(f"{stem}: {result.returncode}\n{result.stderr}")
        return result.stdout

    for triple in ("x86_64-unknown-linux-gnu", "x86_64-pc-windows-msvc", "x86_64-apple-darwin"):
        for level in (0, 2):
            for route in ("neverc", "neverc-ir"):
                profile = f"{triple}-{route}-O{level}"
                obj = output / f"{profile}.o"
                options = [tools["neverc"], f"--target={triple}", f"-O{level}",
                           "-std=c23", "-ffreestanding", "-fno-lto", "-muintr",
                           "-fno-builtin-mimalloc", "-fno-stack-protector",
                           "-Werror", "-mllvm", "-verify-machineinstrs"]
                if route == "neverc":
                    run([*options, "-c", source, "-o", obj], profile + "-compile")
                else:
                    ir = output / f"{profile}.ll"
                    run([*options, "-S", "-emit-llvm", source, "-o", ir], profile + "-ir")
                    run([tools["llc"], f"-mtriple={triple}", "-mattr=+uintr", f"-O{level}",
                         "-verify-machineinstrs", "-filetype=obj", ir, "-o", obj], profile + "-compile")
                disassembly = run([tools["llvm-objdump"], "-d", "--mattr=+uintr", obj],
                                  profile + "-disassembly")
                functions = re.findall(r"^[0-9a-f]+ <_?generated_(\w+)>:\n(.*?)(?=\n[0-9a-f]+ <|\Z)",
                                       disassembly, re.M | re.S)
                assert {name for name, _ in functions} == set(OPERATIONS), disassembly
                for name, body in functions:
                    instructions = re.findall(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2}[ \t]+)+)\s*([^\n]+)",
                                              body, re.M)
                    terminal = [(index, encoding, mnemonic) for index, (_, encoding, mnemonic)
                                in enumerate(instructions)
                                if re.match(r"(?:iret[wql]|lret[wql]|erets|eretu|uiret|sysenter|rsm)\b", mnemonic)]
                    assert len(terminal) == 1, body
                    index, encoding, _ = terminal[0]
                    arguments, expected = OPERATIONS[name]
                    assert bytes.fromhex(encoding) == bytes.fromhex(expected), body
                    if arguments:
                        assert re.match(r"movq\s+%\w+, %rsp\b", instructions[index - 1][2]), body
                    assert not any(re.match(r"(?:call|ret)[lq]?\b", text) for _, _, text in instructions), body
                    assert all(re.match(r"(?:nop\w*|int3|data16|cs nop)\b", text)
                               for _, _, text in instructions[index + 1:]), body
                results.append(dict(profile=profile, verified_operations=len(functions),
                                    object_sha256=digest(obj)))
                print(f"{profile}: {len(functions)} native operation encodings", flush=True)
    report = dict(profiles=results, total=sum(p["verified_operations"] for p in results),
                  source_sha256=digest(__file__),
                  tools={name: dict(path=str(path), sha256=digest(path)) for name, path in tools.items()},
                  scope="Code generation only; native success and faults require separate qualification")
    (output / "evidence.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Qualify the explicit NeverC/full-LLVM i386 object and assembly boundary.

Requires the NeverSight full llc protocol, a Clang driver and Linux multilib.
Backend impersonators are negative controls only; successful output always
comes from the real full LLVM fork.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--neverc", required=True, type=Path)
    parser.add_argument("--llvm-bin", required=True, type=Path)
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    llc = (args.llvm_bin / "llc").resolve()
    neverc = args.neverc.resolve()
    clang = shutil.which(args.clang)
    assert clang
    records, profiles, controls = [], [], []

    def run(command, name, fail=None, stdout_file=None):
        command = list(map(str, command))
        p = subprocess.run(
            command,
            capture_output=True,
            timeout=180,
            env=dict(os.environ, TMPDIR=str(out)),
        )
        (out / (name + ".stderr")).write_bytes(p.stderr)
        (stdout_file or out / (name + ".stdout")).write_bytes(p.stdout)
        records.append(dict(command=command, exit_code=p.returncode, log=name))
        (out / "commands.json").write_text(json.dumps(records, indent=2) + "\n")
        error = p.stderr.decode(errors="replace")
        if fail is None:
            assert p.returncode == 0, (name, p.returncode, error)
        else:
            assert p.returncode > 0 and fail in error, (name, p.returncode, error)
        return p.stdout.decode(errors="replace")

    backend = out / "full LLVM with spaces"
    backend.symlink_to(llc) if not backend.exists() else None
    base = [
        neverc,
        "--target=i386-unknown-linux-gnu",
        "-fno-builtin-mimalloc",
        "-fllvm-backend=" + str(backend),
    ]
    source = out / "source with spaces.c"
    source.write_text("""
__thread unsigned tls = 7;
unsigned global = 13;
unsigned *address(void) { return &global; }
unsigned calc(unsigned x) { tls += x; return tls ^ global; }
""")
    peer = out / "peer.c"
    peer.write_text("""
extern __thread unsigned tls;
extern unsigned global;
extern unsigned *address(void);
extern unsigned calc(unsigned);
int main(void) {
  if (address() != &global || tls != 7 || global != 13) return 1;
  unsigned expected = 7;
  for (unsigned i = 0; i < 10000; ++i) {
    global = i * 99; expected += i;
    if (calc(i) != (expected ^ global) || tls != expected) return 2;
  }
  return 0;
}
""")
    for opt in ("O0", "O2"):
        for mode, flag in (("static", "-fno-pic"), ("pic", "-fPIC"), ("pie", "-fPIE")):
            name = f"{opt}-{mode}"
            obj, exe = out / (name + ".o"), out / (name + ".exe")
            flags = ["-" + opt, flag, "-g", "-ffunction-sections", "-fdata-sections"]
            run(base + flags + ["-c", source, "-o", obj], name + "-object")
            assert obj.read_bytes()[:5] == b"\x7fELF\x01", name
            sections = run(
                [args.llvm_bin / "llvm-objdump", "-h", obj], name + "-sections"
            )
            assert ".text.calc" in sections and ".data.global" in sections
            assert ".debug_info" in sections
            if mode == "pic":
                shared = out / ("lib" + name + ".so")
                run(
                    [clang, "-m32", "-shared", "-Wl,-z,defs", obj, "-o", shared],
                    name + "-shared",
                )
                inputs = [shared, "-Wl,-rpath," + str(out)]
            else:
                inputs = [obj]
            run(
                [
                    clang,
                    "-m32",
                    "-pie" if mode != "static" else "-no-pie",
                    peer,
                    *inputs,
                    "-o",
                    exe,
                ],
                name + "-link",
            )
            run([exe], name + "-run")
            profiles.append(
                dict(
                    name=name,
                    checks=10000,
                    object_sha256=hashlib.sha256(obj.read_bytes()).hexdigest(),
                )
            )
        for dialect in ("att", "intel"):
            name = f"{opt}-{dialect}"
            asm, obj = out / (name + ".s"), out / (name + ".o")
            run(
                base + ["-" + opt, "-masm=" + dialect, "-S", source, "-o", asm],
                name + "-assembly",
            )
            run(
                [
                    args.llvm_bin / "llvm-mc",
                    "-triple=i386-linux-gnu",
                    "-filetype=obj",
                    asm,
                    "-o",
                    obj,
                ],
                name + "-assemble",
            )
            exe = out / (name + ".exe")
            run([clang, "-m32", "-pie", peer, obj, "-o", exe], name + "-link")
            run([exe], name + "-run")
            profiles.append(dict(name=name, checks=10000))

    for kind, emit in (("ll", "-S"), ("bc", "-c")):
        ir, obj = out / ("input." + kind), out / (kind + ".o")
        run(base + [emit, "-emit-llvm", source, "-o", ir], kind + "-emit")
        if kind == "ll":
            run(
                base + ["-c", ir, "-o", obj],
                kind + "-read",
                fail="textual LLVM IR (.ll) input is not supported",
            )
            assert not obj.exists()
            controls.append("textual-ir-input")
        else:
            run(base + ["-c", ir, "-o", obj], kind + "-read")
            assert obj.read_bytes()[:5] == b"\x7fELF\x01"
    stdout_obj = out / "stdout.o"
    run(base + ["-c", source, "-o", "-"], "stdout", stdout_file=stdout_obj)
    assert stdout_obj.read_bytes()[:5] == b"\x7fELF\x01"

    def reject(flags, name, diagnostic, input_file=source):
        obj = out / (name + ".o")
        run(base + flags + ["-c", input_file, "-o", obj], name, fail=diagnostic)
        assert not obj.exists(), (name, "failed compilation committed output")
        controls.append(name)

    reject(["-flto"], "lto", "cannot be combined")
    reject(["-mllvm", "-verify-machineinstrs"], "mllvm", "cannot be combined")
    reject(["-g", "-gsplit-dwarf"], "split-dwarf", "does not accept")
    reject(["--target=x86_64-unknown-linux-gnu"], "wrong-target", "supported i386")
    reject(["-fllvm-backend=/missing/full/llvm/llc"], "missing", "doesn't exist")
    unknown = out / "unknown.c"
    unknown.write_text(
        'extern void unknown(void) __asm__("llvm.x86.unknown.neverc");\n'
        "void bad(void) { unknown(); }\n"
    )
    reject([], "unknown-intrinsic", "unknown LLVM intrinsic", unknown)

    host = out / "host.o"
    run([clang, "--target=x86_64-linux-gnu", "-c", source, "-o", host], "host")
    for name, behavior, diagnostic in (
        ("bad-protocol", "print('unrelated compiler'); sys.exit(0)", "incompatible"),
        (
            "backend-failure",
            "sys.stderr.write('deliberate failure'); sys.exit(1)",
            "deliberate failure",
        ),
        (
            "wrong-object",
            f"shutil.copyfile({str(host)!r}, sys.argv[sys.argv.index('-o') + 1])",
            "not an i386 relocatable",
        ),
        ("no-object", "sys.exit(0)", "produced no output"),
    ):
        fake = out / name
        handshake = (
            "if '--neversight-codegen-protocol' in sys.argv:\n"
            "    print('neversight-codegen-v1 x86'); sys.exit(0)\n"
        )
        fake.write_text(
            f"#!{sys.executable}\nimport sys, shutil\n"
            + ("" if name == "bad-protocol" else handshake)
            + behavior
            + "\n"
        )
        fake.chmod(0o755)
        reject(["-fllvm-backend=" + str(fake)], name, diagnostic)
    assert not list(out.glob("neverc-full-llvm-*")), "backend workspaces leaked"
    report = dict(
        schema_version=1,
        profiles=profiles,
        negative_controls=controls,
        checks=sum(p.get("checks", 0) for p in profiles),
        affinity=sorted(os.sched_getaffinity(0)),
        tools={
            str(p): hashlib.sha256(Path(p).read_bytes()).hexdigest()
            for p in (neverc, llc, args.llvm_bin / "llvm-mc", clang)
        },
        boundary="Explicit full backend, Linux i386; no embedded-backend, integrated linker, LTO, split DWARF or custom machine-hook claim",
    )
    (out / "qualification.json").write_text(json.dumps(report, indent=2) + "\n")
    print(
        json.dumps(
            dict(
                profiles=len(profiles), controls=len(controls), checks=report["checks"]
            )
        )
    )


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Compare native SNP register transport against independent raw instructions.

Linux x64 faults are serviced by a deterministic signal fixture. This qualifies
instruction encoding, register/flags transport and compiler liveness, not AMD
RMP operations or real SEV-ES/GHCB services. Missing native SNP hardware is never
reported as architectural execution coverage.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
FIXTURE = ROOT / "tests/fixtures/x86/snp-context"


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=ROOT / "build-neverc/bin/neverc")
    parser.add_argument("--backend-llc", type=Path)
    parser.add_argument("--new-pm", action="store_true")
    parser.add_argument("--llvm-only", action="store_true")
    parser.add_argument("--source", type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--keep", type=Path, required=True)
    args = parser.parse_args()
    if (args.new_pm or args.llvm_only) and not args.backend_llc:
        parser.error("--new-pm and --llvm-only require --backend-llc")
    if args.llvm_only and args.source:
        parser.error("source compilation requires the NeverC frontend")
    if bool(args.source) != bool(args.manifest):
        parser.error("source qualification requires its manifest")
    if args.source:
        manifest = json.loads(args.manifest.read_text())
        assert manifest["source_sha256"] == digest(args.source)
        assert manifest["native_requests"] == 0
        assert manifest["request"]["native_snp_instructions"] is True
        assert manifest["request"]["native_sev_guest_exit"] is True
    args.keep.mkdir(parents=True, exist_ok=True)
    directory = args.keep.resolve()
    paths = {"host": shutil.which("clang")}
    if not args.llvm_only:
        paths["compiler"] = str(args.compiler.resolve())
    if not paths["host"]:
        parser.error("a host Clang compiler is required")
    if args.backend_llc:
        paths["llc"] = str(args.backend_llc.resolve())
    hashes = {name: digest(path) for name, path in paths.items()}
    commands = []

    def run(command, timeout=180):
        command = list(map(str, command))
        result = subprocess.run(command, cwd=directory, capture_output=True,
                                text=True, timeout=timeout)
        commands.append({"argv": command, "returncode": result.returncode,
                         "stdout": result.stdout, "stderr": result.stderr})
        (directory / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        if result.returncode:
            raise RuntimeError(f"{command}\n{result.stdout}\n{result.stderr}")
        return result

    candidate = (args.source.read_text() + "\n#define SNP_SOURCE 1\n") if args.source else ""
    (directory / "candidate.c").write_text(candidate + (FIXTURE / "candidate.c").read_text())
    run([paths["host"], "-c", FIXTURE / "original.S", "-o", "original.o"])
    profiles = []
    for opt in ("O0", "O2"):
        flags = ["-" + opt, "-std=c23", "-ffreestanding", "-fno-lto",
                 "-fno-builtin-mimalloc", "-fno-builtin", "-mno-red-zone",
                 "-fno-stack-protector", "-fno-pie"]
        if args.llvm_only:
            shutil.copyfile(FIXTURE / "candidate.ll", directory / (opt + ".ll"))
        else:
            command = [paths["compiler"], "--target=x86_64-unknown-linux-gnu", *flags]
            run([*command, *(["-S", "-emit-llvm"] if args.backend_llc else ["-c"]),
                 "candidate.c", "-o", opt + (".ll" if args.backend_llc else ".o")])
        if args.backend_llc:
            run([paths["llc"], "-mtriple=x86_64-unknown-linux-gnu", "-filetype=obj",
                 "-verify-machineinstrs", "-" + opt,
                 *(["-enable-new-pm"] if args.new_pm else []), opt + ".ll", "-o", opt + ".o"])
        exe = directory / opt
        run([paths["host"], "-O2", "-no-pie", *(["-DSNP_SOURCE"] if args.source else []),
             FIXTURE / "runner.c", "original.o", opt + ".o", "-o", exe])
        result = json.loads(run([exe]).stdout)
        profiles.append({"optimization": opt, **result})
    assert hashes == {name: digest(path) for name, path in paths.items()}, "compiler changed during qualification"
    record = {"passed": True, "scope": "native register transport with a synthetic fault service",
              "physical_snp_qualified": False, "sev_es_services_qualified": False,
              "profiles": profiles, "new_pass_manager": args.new_pm, "llvm_only": args.llvm_only,
              "tools": {name: {"path": path, "sha256": hashes[name]} for name, path in paths.items()},
              "source_sha256": digest(args.source) if args.source else None,
              "manifest_sha256": digest(args.manifest) if args.manifest else None,
              "fixture_sha256": {p.name: digest(p) for p in FIXTURE.iterdir() if p.is_file()},
              "files": {p.name: digest(p) for p in directory.iterdir()
                        if p.is_file() and p.name != "qualification.json"}}
    (directory / "qualification.json").write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps({"passed": True, "profiles": profiles}))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Compare the indivisible native pair with raw bytes under Intel SDE.

Both explicit XABORT and forced early-abort statuses are checked. Emulator
execution is recorded separately from physical RTM support.
"""
import argparse
from functools import lru_cache
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
FIXTURE = ROOT / "tests/fixtures/x86/rtm-abort-pair"


@lru_cache()
def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--backend-llc", type=Path)
    parser.add_argument("--new-pm", action="store_true")
    parser.add_argument("--sde", type=Path, required=True)
    parser.add_argument("--keep", type=Path, required=True)
    args = parser.parse_args()
    if args.new_pm and not args.backend_llc:
        parser.error("--new-pm requires --backend-llc")
    directory = args.keep.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location("fixture", FIXTURE / "generate.py")
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)
    generator.generate(directory)
    paths = {"compiler": str(args.compiler.resolve()), "sde": str(args.sde.resolve()),
             "host": shutil.which("clang")}
    if args.backend_llc:
        paths["llc"] = str(args.backend_llc.resolve())
    if not paths["host"]:
        parser.error("host Clang is required")
    tools = {key: {"path": value, "sha256": digest(value)} for key, value in paths.items()}
    commands, profiles = [], []

    def run(command, allowed=(0,)):
        command = list(map(str, command))
        result = subprocess.run(command, cwd=directory, capture_output=True,
                                text=True, timeout=180)
        commands.append({"argv": command, "returncode": result.returncode,
                         "stdout": result.stdout, "stderr": result.stderr})
        (directory / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        if result.returncode not in allowed:
            raise RuntimeError(f"{command}\n{result.stdout}\n{result.stderr}")
        return result

    run([paths["host"], "-c", "original.S", "-o", "original.o"])
    run([paths["host"], "-O2", "-c", "reference.c", "-o", "reference.o"])
    for opt in ("O0", "O2"):
        flags = ["-" + opt, "-std=c23", "-ffreestanding", "-fno-lto",
                 "-fno-builtin-mimalloc", "-fno-pie", "-fno-stack-protector"]
        run([paths["compiler"], "--target=x86_64-unknown-linux-gnu", *flags,
             *(["-S", "-emit-llvm"] if args.backend_llc else ["-c"]),
             "candidate.c", "-o", opt + (".ll" if args.backend_llc else ".o")])
        if args.backend_llc:
            run([paths["llc"], "-mtriple=x86_64-unknown-linux-gnu", "-filetype=obj",
                 "-verify-machineinstrs", "-" + opt,
                 *(["-enable-new-pm"] if args.new_pm else []), opt + ".ll", "-o", opt + ".o"])
        # Require the complete original pair, including its exact local target.
        object_bytes = (directory / (opt + ".o")).read_bytes()
        for code in range(256):
            assert bytes([0xc7, 0xf8, 3, 0, 0, 0, 0xc6, 0xf8, code]) in object_bytes, code
        exe = directory / opt
        run([paths["host"], "-O2", "-no-pie", "-I", directory, FIXTURE / "runner.c",
             "original.o", "reference.o", opt + ".o", "-o", exe])
        for mode in ("full", "0", "2", "6", "8"):
            # SDE 10.13.1's -tsx overrides abort mode regardless of order.
            # Include it only for full emulation, and assert exact EAX below.
            arguments = (["-tsx", "-rtm_mode", "full"] if mode == "full" else
                         ["-rtm_mode", "abort", "-rtm_abort_reason", mode])
            result = json.loads(run([paths["sde"], "-hsw", *arguments,
                                     "--", exe, "full" if mode == "full" else "sde-abort-" + mode]).stdout)
            assert result["pairs"] == 256 * 32
            profiles.append({"optimization": opt, "engine": "Intel SDE",
                             "mode": mode, **result})
    digest.cache_clear()
    assert all(digest(tool["path"]) == tool["sha256"] for tool in tools.values())
    report = {"passed": True, "physical_rtm_qualified": False,
              "scope": "native immediate-abort pair under independent Intel SDE emulation",
              "tools": tools, "new_pass_manager": args.new_pm, "profiles": profiles,
              "fixture_sha256": {p.name: digest(str(p)) for p in FIXTURE.iterdir() if p.is_file()},
              "files": {p.name: digest(str(p)) for p in directory.iterdir()
                        if p.is_file() and p.name != "qualification.json"}}
    (directory / "qualification.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"passed": True, "profiles": len(profiles),
                      "pairs": sum(p["pairs"] for p in profiles)}))


if __name__ == "__main__":
    main()

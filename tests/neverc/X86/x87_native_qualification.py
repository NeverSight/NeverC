"""Shared compiler profiles and retained evidence for native x87 oracles."""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_qualification(description, harness_text, sources, expected, expected_rows,
                      reference, script_path):
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("--llc", type=Path)
    parser.add_argument("--opt", type=Path,
                        help="LLVM optimizer; defaults to the sibling of --llc")
    parser.add_argument("--neverc", type=Path)
    parser.add_argument("--clang", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    if not args.llc and not args.neverc:
        parser.error("provide --llc and/or --neverc")
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        parser.error("native Linux x64 is required")
    if args.llc and not args.opt:
        args.opt = args.llc.with_name("opt")
    tools = {str(path.resolve()): digest(path.resolve())
             for path in (args.llc, args.opt, args.neverc, args.clang) if path}
    script_hashes = {str(Path(name).resolve()): digest(Path(name))
                     for name in (script_path, __file__)}
    root = args.output_dir.resolve()
    root.mkdir(parents=True, exist_ok=False)
    (root / "cpuinfo.txt").write_text(Path("/proc/cpuinfo").read_text())
    harness = root / "harness.c"
    harness.write_text(harness_text)
    commands, observations = [], []

    def run(command, name):
        command = list(map(str, command))
        result = subprocess.run(command, capture_output=True, text=True, timeout=120)
        (root / (name + ".stdout")).write_text(result.stdout)
        (root / (name + ".stderr")).write_text(result.stderr)
        commands.append({"argv": command, "returncode": result.returncode})
        (root / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        result.check_returncode()
        return result.stdout

    profiles = [name for name in ("llc", "neverc") if getattr(args, name)]
    if args.llc and args.neverc:
        profiles.append("neverc-llvm")
    for compiler in profiles:
        tool = (args.llc if compiler == "llc" else args.neverc).resolve()
        source = root / (compiler + (".ll" if compiler == "llc" else ".c"))
        source.write_text(sources["ir" if compiler == "llc" else "c"])
        for opt in (0, 2):
            name = f"{compiler}-O{opt}"
            obj, binary = root / (name + ".o"), root / name
            if compiler == "llc":
                ir = source
                if opt:
                    ir = root / (name + ".ll")
                    run([args.opt.resolve(), "-S", "-passes=default<O2>",
                         source, "-o", ir], name + "-optimize")
                command = [tool, "-mtriple=x86_64-unknown-linux-gnu",
                           "-verify-machineinstrs", "-filetype=obj", f"-O{opt}", ir, "-o", obj]
            elif compiler == "neverc":
                command = [tool, "--target=x86_64-unknown-linux-gnu", "-fno-lto",
                           "-fno-builtin-mimalloc", "-c", f"-O{opt}", source, "-o", obj]
            else:
                ir = root / (name + ".ll")
                run([tool, "--target=x86_64-unknown-linux-gnu", "-fno-lto",
                     "-fno-builtin-mimalloc", "-S", "-emit-llvm", f"-O{opt}", source,
                     "-o", ir], name + "-frontend")
                command = [args.llc.resolve(), "-mtriple=x86_64-unknown-linux-gnu",
                           "-verify-machineinstrs", "-filetype=obj", f"-O{opt}", ir, "-o", obj]
            run(command, name + "-compile")
            run([args.clang.resolve(), "-O2", "-mno-red-zone", harness, obj,
                 "-o", binary], name + "-link")
            rows = [json.loads(row) for row in run([binary], name + "-execute").splitlines()]
            result = rows[-1]
            if result != expected or len(rows) != expected_rows:
                raise ValueError("incomplete native observation inventory")
            observations.append({"profile": name, **result, "object_sha256": digest(obj),
                                 "binary_sha256": digest(binary)})
    if tools != {path: digest(Path(path)) for path in tools} or \
            script_hashes != {name: digest(Path(name)) for name in script_hashes}:
        raise ValueError("compiler or qualification script changed during execution")
    record = {"passed": True, "profiles": observations,
              "harness_sha256": digest(harness),
              "scripts": script_hashes, "stable_inputs": True,
              "tools": tools, "platform": platform.platform(),
              "files": {str(path.relative_to(root)): {"sha256": digest(path),
                                                       "bytes": path.stat().st_size}
                        for path in sorted(root.rglob("*")) if path.is_file()},
              "exclusions": ["other CPU models", "i386 runtime", "privileged #NM",
                             "alignment-check exceptions", "general x87 state equivalence"],
              "reference": reference}
    (root / "qualification.json").write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps({"passed": True, "profiles": len(observations)}, indent=2))

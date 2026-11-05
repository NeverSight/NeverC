#!/usr/bin/env python3
"""Execute NeverC i386 IR through the full LLVM fork against a Clang C ABI peer.

This deliberately does not claim the embedded, 64-bit-only machine backend or
linker can emit i386 objects. Their refusal without an explicit full backend is part of this regression.
"""

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--neverc", type=Path, required=True)
    parser.add_argument("--llvm-bin", type=Path, required=True)
    parser.add_argument("--clang", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--machine-backend", action="store_true",
                        help="use NeverC -c with its explicit full LLVM backend")
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    records = []

    def run(command, name, expected=0, source=None):
        command = [str(x) for x in command]
        p = subprocess.run(
            command, input=source, text=True, capture_output=True, timeout=120
        )
        (out / (name + ".stdout")).write_text(p.stdout)
        (out / (name + ".stderr")).write_text(p.stderr)
        records.append({"command": command, "returncode": p.returncode, "name": name})
        if expected is None:
            assert p.returncode > 0, (name, p.returncode, p.stderr)
        else:
            assert p.returncode == expected, (name, p.returncode, p.stderr, p.stdout)
        return p.stdout + p.stderr

    nc = [
        args.neverc,
        "--target=i386-unknown-linux-gnu",
        "-std=gnu2x",
        "-msse2",
        "-fno-pie",
    ]
    cc = [args.clang, "-m32", "-std=gnu2x", "-msse2", "-fno-pie"]
    results = []
    for opt in ("O0", "O2"):
        files = {}
        for role in ("producer", "consumer"):
            source = ROOT / f"i386_abi_{role}.c"
            ir = out / f"neverc-{role}-{opt}.ll"
            obj = out / f"neverc-{role}-{opt}.o"
            run(
                nc + ["-" + opt, "-S", "-emit-llvm", source, "-o", ir],
                f"neverc-{role}-{opt}",
            )
            text = ir.read_text()
            assert "p:32:32" in text and "f80:32" in text
            run(
                [args.llvm_bin / "opt", "-passes=verify", "-disable-output", ir],
                f"verify-{role}-{opt}",
            )
            run(
                [
                    args.llvm_bin / "llc",
                    "-" + opt,
                    "-verify-machineinstrs",
                    "-filetype=obj",
                    ir,
                    "-o",
                    obj,
                ],
                f"lower-{role}-{opt}",
            )
            if args.machine_backend:
                run(nc + ["-" + opt, "-fllvm-backend=" + str(args.llvm_bin / "llc"),
                          "-c", source, "-o", obj], f"native-{role}-{opt}")
            if role == "producer":
                bitcode = out / f"neverc-{role}-{opt}.bc"
                run(
                    nc + ["-" + opt, "-c", "-emit-llvm", source, "-o", bitcode],
                    f"bitcode-{opt}",
                )
                run(
                    [
                        args.llvm_bin / "opt",
                        "-passes=verify",
                        "-disable-output",
                        bitcode,
                    ],
                    f"verify-bitcode-{opt}",
                )
            files[("neverc", role)] = obj
            obj = out / f"clang-{role}-{opt}.o"
            run(cc + ["-" + opt, "-c", source, "-o", obj], f"clang-{role}-{opt}")
            files[("clang", role)] = obj
        for producer, consumer in (
            ("clang", "clang"),
            ("neverc", "clang"),
            ("clang", "neverc"),
            ("neverc", "neverc"),
        ):
            name = f"{producer}-to-{consumer}-{opt}"
            exe = out / name
            run(
                [
                    args.clang,
                    "-m32",
                    "-no-pie",
                    files[(producer, "producer")],
                    files[(consumer, "consumer")],
                    "-o",
                    exe,
                ],
                name + "-link",
            )
            result = run([exe], name)
            assert "checks=320000" in result, result
            results.append({"name": name, "checks": 320000})
        # Keep the signature and every call intact, truncate only the returned
        # wide value. The foreign caller must catch this independent control.
        ir = out / f"neverc-producer-{opt}.ll"
        text = ir.read_text()
        match = re.search(r"define [^\n]*@wide\([^\n]*\).*?\n}", text, re.S)
        assert match
        body, count = re.subn(
            r"ret i64 ([^\n]+)",
            r"%negative.trunc = and i64 \1, 4294967295\n  ret i64 %negative.trunc",
            match[0],
        )
        assert count == 1
        negative = out / f"negative-{opt}.ll"
        negative.write_text(text[: match.start()] + body + text[match.end() :])
        obj = out / f"negative-{opt}.o"
        run(
            [args.llvm_bin / "llc", "-" + opt, "-filetype=obj", negative, "-o", obj],
            f"negative-{opt}-lower",
        )
        exe = out / f"negative-{opt}"
        run(
            [
                args.clang,
                "-m32",
                "-no-pie",
                obj,
                files[("clang", "consumer")],
                "-o",
                exe,
            ],
            f"negative-{opt}-link",
        )
        result = run([exe], f"negative-{opt}-run", expected=1)
        assert "line=" in result

    probe = out / "probe.c"
    probe.write_text("int f(int x) { return x + 1; }\n")
    for mode in ("-c", "-S"):
        text = run(
            nc + [mode, probe, "-o", out / "unsupported.o"],
            "unsupported-" + mode[1:],
            expected=None,
        )
        assert "full LLVM fork" in text and not (out / "unsupported.o").exists(), text
    for triple in ("i386-pc-windows-msvc", "i386-apple-macosx", "i386-linux-android29"):
        text = run(
            [
                args.neverc,
                "--target=" + triple,
                "-S",
                "-emit-llvm",
                probe,
                "-o",
                out / "unsupported.ll",
            ],
            triple,
            expected=None,
        )
        assert "unsupported" in text
    # Validate explicit address controls and reject BOUND in long mode before IR.
    bad = {
        "width64": "void f(int x) { __builtin_ia32_bound32_address32(x, 0, 64, 0); }",
        "prefix": "void f(int x) { __builtin_ia32_bound32_address32(x, 0, 32, 0x66); }",
        "variable": "void f(int x) { __builtin_ia32_bound32_address32(x, 0, x, 0); }",
        "fastcall": "int __attribute__((fastcall)) f(int x) { return x; }",
    }
    for name, source in bad.items():
        run(nc + ["-fsyntax-only", "-x", "c", "-"], name, expected=None, source=source)
    run(
        [
            args.neverc,
            "--target=x86_64-unknown-linux-gnu",
            "-fsyntax-only",
            "-x",
            "c",
            "-",
        ],
        "bound-long-mode",
        expected=None,
        source="void f(int x, void *p) { __builtin_ia32_bound32(x, p); }",
    )
    tools = [args.neverc, args.clang, args.llvm_bin / "llc", args.llvm_bin / "opt"]
    (out / "summary.json").write_text(
        json.dumps(
            {
                "scope": "i386 Linux C23 ABI through full LLVM; embedded backend remains x64-only",
                "machine_backend": args.machine_backend,
                "results": results,
                "total_checks": sum(x["checks"] for x in results),
                "negative_controls": 2,
                "tools": [
                    {
                        "path": str(x.resolve()),
                        "sha256": hashlib.sha256(x.read_bytes()).hexdigest(),
                    }
                    for x in tools
                ],
                "commands": records,
            },
            indent=2,
        )
        + "\n"
    )
    print(
        json.dumps(
            {"profiles": len(results), "checks": sum(x["checks"] for x in results)}
        )
    )


if __name__ == "__main__":
    main()

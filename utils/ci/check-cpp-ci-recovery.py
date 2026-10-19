#!/usr/bin/env python3
"""Focused native CI evidence; this does not replace the complete CI gates."""
from pathlib import Path
import argparse
import hashlib
import json
import platform
import re
import struct
import subprocess
import sys
import time
import traceback

TARGETS = ["i686-pc-windows-msvc", "x86_64-pc-windows-msvc",
           "aarch64-pc-windows-msvc", "i686-unknown-linux-gnu",
           "x86_64-unknown-linux-gnu", "aarch64-unknown-linux-gnu",
           "arm64-apple-macosx15.0.0", "x86_64-apple-macosx15.0.0"]


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def thread_sections(path):
    raw = path.read_bytes()
    assert struct.unpack_from("<I", raw)[0] == 0xFEEDFACF
    count = struct.unpack_from("<I", raw, 16)[0]
    offset = 32
    found = []
    for _ in range(count):
        command, size = struct.unpack_from("<II", raw, offset)
        assert size >= 8 and offset + size <= len(raw)
        if command == 0x19:
            sections = struct.unpack_from("<I", raw, offset + 64)[0]
            for index in range(sections):
                item = struct.unpack_from("<16s16sQQIIIIIIII", raw,
                                          offset + 72 + index * 80)
                if item[8] & 0xFF == 0x13:
                    found.append({"address": item[2], "size": item[3],
                                  "alignment_power": item[5]})
        offset += size
    assert found
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    repo, output = args.repo.resolve(), args.output.resolve()
    compiler = args.compiler.resolve()
    if sys.platform == "win32" and not compiler.is_file():
        compiler = compiler.with_suffix(".exe")
    assert sys.platform in ("darwin", "win32"), "Run only on the native CI runners"
    assert compiler.is_file()
    output.mkdir(parents=True, exist_ok=False)
    report = {"passed": False, "completed": False, "commands": [],
              "host": platform.platform(), "compiler_sha256": sha(compiler),
              "sdk_protocols": [], "objects": [], "native_executions": [],
              "tlv_cases": [], "full_ci_passed": False,
              "frontend_timeout_seconds": 60}
    def save():
        (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    def run(command, label, expected=0):
        number = len(report["commands"])
        started = time.monotonic()
        entry = {"label": label, "command": list(map(str, command)), "timeout": 60}
        report["commands"].append(entry)
        save()
        result = subprocess.run(entry["command"], cwd=repo, capture_output=True,
                                timeout=60)
        stdout, stderr = output / f"{number:03}.stdout", output / f"{number:03}.stderr"
        stdout.write_bytes(result.stdout)
        stderr.write_bytes(result.stderr)
        entry.update(returncode=result.returncode, seconds=time.monotonic()-started,
                     stdout=stdout.name, stderr=stderr.name,
                     stdout_sha256=sha(stdout), stderr_sha256=sha(stderr))
        save()
        assert (result.returncode == expected if expected is not None else
                result.returncode > 0), f"{label}: exit {result.returncode}"
        return result
    try:
        report["commit"] = run(["git", "rev-parse", "HEAD"], "source revision").stdout.decode().strip()
        sdk = repo / "neverc/lib/Translate/Cpp/SDK"
        assert sha(sdk / "catalog.json") == "1497f7d37860d32d9e53798fbcdc0fe473a4c4e66ac05fa81a45910adfb1d167"
        catalog = json.loads((sdk / "catalog.json").read_text())
        entries = catalog["headers"] + catalog["metadata"]
        assert len(catalog["headers"]) == 664 and len(entries) == 665
        for entry in entries:
            assert sha(sdk / entry["root"] / entry["path"]) == entry["sha256"]
        report["sdk_files_verified"] = len(entries)
        tests = (repo / "tests/neverc/TranslateTests.cpp").read_text()
        block = tests.split("TEST_F(TranslateTest, CoreV2PinnedSdkDoesNotDetectHostMsvcVersion)", 1)[1]
        source = re.search(r'writeFile\(Source, R"cpp\((.*?)\)cpp"\);', block, re.S).group(1)
        input_path = output / "sdk-host-msvc-version.cpp"
        input_path.write_text(source)
        report["sdk_source_sha256"] = sha(input_path)
        for target in TARGETS:
            generated = output / (target + ".nc")
            run([compiler, "translate", "--from", "cpp", "--profile", "cpp-core-v2",
                 "--target", target, input_path, "-o", generated], "SDK " + target)
            assert generated.is_file()
            report["sdk_protocols"].append({"target": target, "generated_sha256": sha(generated)})
            if not target.startswith("i686-"):
                obj = output / (target + ".o")
                run([compiler, "--target=" + target, "-O0", "-fno-lto",
                     "-fno-builtin-mimalloc", "-fno-fast-math", "-ffp-contract=off",
                     "-c", generated, "-o", obj], "object " + target)
                assert obj.is_file() and obj.stat().st_size > 32
                raw = obj.read_bytes()
                if "-linux-" in target:
                    assert raw[:4] == b"\x7fELF" and raw[4:6] == b"\x02\x01"
                    assert struct.unpack_from("<H", raw, 18)[0] == (183 if target.startswith("aarch64") else 62)
                elif "-windows-" in target:
                    assert struct.unpack_from("<H", raw)[0] == (0xAA64 if target.startswith("aarch64") else 0x8664)
                else:
                    assert struct.unpack_from("<I", raw)[0] == 0xFEEDFACF
                    assert struct.unpack_from("<I", raw, 4)[0] == (0x100000C if target.startswith("arm64") else 0x1000007)
                report["objects"].append({"target": target, "sha256": sha(obj)})
        native = "arm64-apple-macosx15.0.0" if sys.platform == "darwin" else "x86_64-pc-windows-msvc"
        for optimization in ["-O0", "-O2"]:
            executable = output / ("sdk-native" + optimization + (".exe" if sys.platform == "win32" else ""))
            run([compiler, optimization, "-fno-lto", "-fno-builtin-mimalloc",
                 "-fno-fast-math", "-ffp-contract=off", output / (native + ".nc"),
                 "-o", executable], "native SDK compile " + optimization)
            run([executable], "native SDK execute " + optimization)
            report["native_executions"].append({"optimization": optimization, "sha256": sha(executable)})
        if sys.platform == "darwin":
            text = (repo / "tests/neverc/LinkerTests.cpp").read_text().split(
                "TEST_F(LinkerTest, MachOTLVDescriptorAtomsUsePointerAlignment)", 1)[1].split(
                "TEST_F(LinkerTest, NativeMachOBindingAndLoadingOptions)", 1)[0]
            sources = re.findall(r'writeFile\((?:Source|BadSource), R"asm\((.*?)\)asm"\);', text, re.S)
            assert len(sources) == 2
            for architecture in ["arm64", "x86_64"]:
                target = "--target=" + architecture + "-apple-macos13"
                for index, source in enumerate(sources):
                    src = output / f"tlv-{architecture}-{index}.s"
                    obj, image = src.with_suffix(".o"), src.with_suffix(".dylib")
                    src.write_text(source)
                    run([compiler, target, "-fno-lto", "-fno-builtin-mimalloc", "-c",
                         src, "-o", obj], "TLV assembly " + src.name)
                    inputs = thread_sections(obj)
                    assert all(s["alignment_power"] == 0 for s in inputs)
                    linked = run([compiler, target, "-nostdlib", "-fno-builtin-mimalloc",
                                  "-dynamiclib", "-Wl,-undefined,dynamic_lookup",
                                  "-Wl,-unaligned_pointers,error", obj, "-o", image],
                                 "TLV link " + src.name, expected=0 if index == 0 else None)
                    item = {"architecture": architecture, "negative": index == 1,
                            "input": inputs, "input_sha256": sha(obj)}
                    if index == 0:
                        sections = thread_sections(image)
                        assert len(sections) == 1 and sections[0]["size"] == 48
                        assert sections[0]["alignment_power"] >= 3 and sections[0]["address"] % 8 == 0
                        item.update(output=sections, output_sha256=sha(image))
                    else:
                        assert b"is not word-aligned" in linked.stderr and not image.exists()
                    report["tlv_cases"].append(item)
            for example in ["network-authoritative-server", "network-anticheat-collector"]:
                exe = output / example
                run([compiler, "--target=arm64-apple-macos", "-I" + str(repo / "std/include"),
                     "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", "-o", exe,
                     repo / "examples" / example / "main.c"], "network product " + example)
                assert exe.is_file()
        assert len(report["sdk_protocols"]) == 8 and len(report["objects"]) == 6
        assert len(report["native_executions"]) == 2
        assert len(report["tlv_cases"]) == (4 if sys.platform == "darwin" else 0)
        report.update(passed=True, completed=True)
    except Exception:
        report.update(completed=True, failure=traceback.format_exc())
    finally:
        save()
    if not report["passed"]:
        print(report["failure"], file=sys.stderr)
        return 1
    print("PASS focused recovery evidence; complete original CI remains required")
    return 0


if __name__ == "__main__":
    sys.exit(main())

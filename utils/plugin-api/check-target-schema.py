#!/usr/bin/env python3
"""Generate or verify LOCKSTEP builtin target schemas."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCHEMA_DIR = ROOT / "pluginsdk" / "schemas" / "targets"
DEFAULT_ARCHES = ("x86_64", "aarch64")


def find_generator(explicit: str | None) -> Path:
    if explicit:
        path = Path(explicit)
        if not path.is_file():
            raise SystemExit(f"generator not found: {path}")
        return path

    env = os.environ.get("NEVERC_PLUGIN_TARGET_SCHEMA_GEN")
    if env:
        path = Path(env)
        if path.is_file():
            return path

    candidates = [
        ROOT / "build-neverc" / "bin" / "neverc-plugin-target-schema-gen",
        ROOT / "build" / "bin" / "neverc-plugin-target-schema-gen",
    ]
    for path in candidates:
        if path.is_file():
            return path
    raise SystemExit(
        "neverc-plugin-target-schema-gen not found; build it or pass --generator"
    )


def canonical_payload(schema: dict) -> bytes:
    def enc_str(value: str) -> bytes:
        data = value.encode("utf-8")
        return f"{len(data)}:".encode("ascii") + data + b"\n"

    def enc_u32(value: int) -> bytes:
        return f"{value}\n".encode("ascii")

    def enc_bool(value: bool) -> bytes:
        return b"1\n" if value else b"0\n"

    def enc_u32_list(values: list[int]) -> bytes:
        out = enc_u32(len(values))
        for item in values:
            out += enc_u32(item)
        return out

    def enc_str_list(values: list[str]) -> bytes:
        out = enc_u32(len(values))
        for item in values:
            out += enc_str(item)
        return out

    out = b""
    out += enc_str(schema["architecture"])
    out += enc_str(schema["triple"])
    out += enc_str(schema["producer_build_id"])
    out += enc_u32(int(schema["schema_version"]))

    registers = schema["registers"]
    out += enc_u32(len(registers))
    for reg in registers:
        out += enc_u32(int(reg["stable_id"]))
        out += enc_u32(int(reg["backend_value"]))
        out += enc_str(reg["name"])
        out += enc_u32(int(reg["encoding"]))
        out += f'{int(reg["dwarf"])}\n'.encode("ascii")
        out += f'{int(reg["eh"])}\n'.encode("ascii")
        out += enc_u32(int(reg["size_bits"]))
        out += enc_u32(int(reg["align_bits"]))
        out += enc_u32_list([int(x) for x in reg["aliases"]])
        out += enc_u32_list([int(x) for x in reg["subregs"]])
        out += enc_u32_list([int(x) for x in reg["superregs"]])
        out += enc_u32_list([int(x) for x in reg["subreg_indices"]])
        out += enc_u32_list([int(x) for x in reg["reg_classes"]])
        out += enc_u32(int(reg.get("flags", 0)))

    instructions = schema["instructions"]
    out += enc_u32(len(instructions))
    for inst in instructions:
        out += enc_u32(int(inst["stable_id"]))
        out += enc_u32(int(inst["backend_value"]))
        out += enc_str(inst["name"])
        out += enc_u32(int(inst["num_operands"]))
        out += enc_u32(int(inst["num_defs"]))
        out += enc_u32(int(inst["sched_class"]))
        out += enc_bool(bool(inst["is_branch"]))
        out += enc_bool(bool(inst["is_call"]))
        out += enc_bool(bool(inst["is_return"]))
        out += enc_bool(bool(inst["is_terminator"]))
        out += enc_bool(bool(inst["has_side_effects"]))
        out += enc_u32_list([int(x) for x in inst["implicit_uses"]])
        out += enc_u32_list([int(x) for x in inst["implicit_defs"]])
        out += enc_u32(int(inst.get("flags", 0)))

    features = schema["features"]
    out += enc_u32(len(features))
    for feature in features:
        out += enc_u32(int(feature["stable_id"]))
        out += enc_u32(int(feature["backend_value"]))
        out += enc_str(feature["key"])
        out += enc_str(feature["description"])
        out += enc_bool(bool(feature["default"]))
        out += enc_str_list(list(feature["implies"]))
        out += enc_str_list(list(feature["conflicts"]))
        out += enc_u32(int(feature.get("flags", 0)))
    return out


def expected_digest(schema: dict) -> str:
    return hashlib.sha256(canonical_payload(schema)).hexdigest()


def validate_schema_file(path: Path) -> list[str]:
    errors: list[str] = []
    try:
        schema = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        return [f"{path}: invalid JSON: {exc}"]

    for key in (
        "architecture",
        "triple",
        "producer_build_id",
        "schema_version",
        "digest",
        "registers",
        "instructions",
        "features",
    ):
        if key not in schema:
            errors.append(f"{path}: missing field {key}")
    if errors:
        return errors

    if not schema["registers"]:
        errors.append(f"{path}: registers must be non-empty")
    if not schema["instructions"]:
        errors.append(f"{path}: instructions must be non-empty")
    if not schema["features"]:
        errors.append(f"{path}: features must be non-empty")

    digest = expected_digest(schema)
    if schema["digest"] != digest:
        errors.append(
            f"{path}: digest drift (file={schema['digest']} expected={digest})"
        )
    return errors


def regenerate(generator: Path, output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    subprocess.check_call(
        [str(generator), f"--output-dir={output_dir}"],
        cwd=str(ROOT),
    )


def check_generated_files(generator: Path, schema_dir: Path) -> list[str]:
    errors: list[str] = []
    with tempfile.TemporaryDirectory(prefix="neverc-target-schema-") as temporary:
        generated_dir = Path(temporary)
        regenerate(generator, generated_dir)
        for arch in DEFAULT_ARCHES:
            expected = schema_dir / f"{arch}.json"
            generated = generated_dir / f"{arch}.json"
            if not expected.is_file() or not generated.is_file():
                continue
            if expected.read_bytes() != generated.read_bytes():
                errors.append(
                    f"{expected}: generated schema drift; "
                    "run check-target-schema.py --update"
                )
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--update", action="store_true")
    parser.add_argument("--generator")
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=SCHEMA_DIR,
        help="Schema output/check directory",
    )
    args = parser.parse_args()

    if args.update:
        regenerate(find_generator(args.generator), args.output_dir)

    if not args.check and not args.update:
        parser.error("pass --check and/or --update")

    errors: list[str] = []
    for arch in DEFAULT_ARCHES:
        path = args.output_dir / f"{arch}.json"
        if not path.is_file():
            errors.append(f"missing schema: {path}")
            continue
        errors.extend(validate_schema_file(path))
    if args.check and not errors:
        errors.extend(
            check_generated_files(find_generator(args.generator), args.output_dir)
        )

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print(f"ok: {len(DEFAULT_ARCHES)} target schemas")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

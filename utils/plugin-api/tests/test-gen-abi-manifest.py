#!/usr/bin/env python3
"""Self-test for gen-abi-manifest.py.

The manifest gate is host-dependent: it only fails on a host whose ABI key is
absent, which previously meant a missing key stayed invisible until an hour of
CI on that host. These checks guard the parts that keep the gate honest from
any machine:

* the supported-key set covers every architecture/calling-convention pair the
  conformance matrix runs on, and a manifest missing one of them is rejected;
* the cross probe's value ordering round-trips, so a cross-measured entry means
  what a natively measured one means;
* the IR constant parser and its arity check reject malformed probe output
  rather than silently producing a short entry.
"""

from __future__ import annotations

import importlib.util
import pathlib
import sys


HERE = pathlib.Path(__file__).resolve().parent
GENERATOR = HERE.parent / "gen-abi-manifest.py"


def load_generator():
    spec = importlib.util.spec_from_file_location("gen_abi_manifest", GENERATOR)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = mod
    spec.loader.exec_module(mod)
    return mod


def expect(cond: bool, message: str, failures: list[str]) -> None:
    if not cond:
        failures.append(message)


def main() -> int:
    mod = load_generator()
    failures: list[str] = []
    checks = 0

    # 1. Every architecture x calling-convention pair the conformance matrix
    #    covers (linux/macOS sysv and Windows, on x86_64 and aarch64) ships a
    #    layout, so no native host can hit an unrecorded key.
    for arch in ("x86_64", "aarch64"):
        for cc in ("sysv", "win"):
            key = f"{arch}-le-64-{cc}"
            expect(key in mod.SUPPORTED_ABI_KEYS,
                   f"SUPPORTED_ABI_KEYS is missing {key}", failures)
            checks += 1

    # 2. A manifest that drops a supported key is rejected, and the report names
    #    exactly the dropped key.
    full = {"abi_keys": {name: {} for name in mod.SUPPORTED_ABI_KEYS}}
    expect(mod.missing_supported_keys(full) == [],
           "complete manifest reported as missing keys", failures)
    checks += 1

    dropped = sorted(mod.SUPPORTED_ABI_KEYS)[0]
    partial = {"abi_keys": {name: {} for name in mod.SUPPORTED_ABI_KEYS
                            if name != dropped}}
    expect(mod.missing_supported_keys(partial) == [dropped],
           f"dropping {dropped} was not reported", failures)
    checks += 1

    expect(mod.missing_supported_keys({}) == sorted(mod.SUPPORTED_ABI_KEYS),
           "empty manifest did not report every supported key", failures)
    checks += 1

    # 3. The cross probe's value ordering round-trips: what build_cross_probe
    #    emits is what parse_cross_probe reads back.
    layouts = [("NevercAlpha", ["First", "Second"]), ("NevercBeta", [])]
    source = mod.build_cross_probe(layouts, None)
    for fragment in ("sizeof(void *)", "sizeof(NevercAlpha)",
                     "_Alignof(NevercAlpha)", "offsetof(NevercAlpha, First)",
                     "sizeof(((NevercAlpha *)0)->Second)", "sizeof(NevercBeta)"):
        expect(fragment in source,
               f"cross probe omitted {fragment}", failures)
        checks += 1

    # ptr, then (size, align) + (offset, size) per field, per struct.
    values = [8, 24, 8, 0, 4, 8, 16, 16, 8]
    width, structs = mod.parse_cross_probe(values, layouts, None)
    expect(width == 64, f"pointer width parsed as {width}", failures)
    expect(structs["NevercAlpha"]["size"] == 24
           and structs["NevercAlpha"]["align"] == 8,
           "struct size/align misparsed", failures)
    expect(structs["NevercAlpha"]["fields"]["Second"] == {"offset": 8, "size": 16},
           "field offset/size misparsed", failures)
    expect(structs["NevercBeta"] == {"size": 16, "align": 8, "fields": {}},
           "fieldless struct misparsed", failures)
    checks += 4

    # Pack variants carry size/align only, so their arity differs.
    packed_width, packed = mod.parse_cross_probe([8, 24, 8, 16, 8], layouts, 1)
    expect(packed_width == 64 and packed["NevercAlpha"]["size"] == 24
           and packed["NevercAlpha"]["fields"] == {},
           "pack-variant probe misparsed", failures)
    checks += 1

    # 4. A truncated probe is an error, not a short entry.
    try:
        mod.parse_cross_probe([8, 24], layouts, None)
        failures.append("truncated cross probe was accepted")
    except ValueError:
        pass
    checks += 1

    # 5. The IR constant parser reads clang's real spelling.
    ir = ("@NevercAbiProbe = dso_local constant [3 x i64] "
          "[i64 8, i64 16, i64 8], align 16\n")
    match = mod.PROBE_IR.search(ir)
    expect(match is not None, "PROBE_IR did not match clang output", failures)
    if match is not None:
        parsed = [int(v) for v in mod.PROBE_VALUE.findall(match.group(1))]
        expect(parsed == [8, 16, 8], f"PROBE_VALUE parsed {parsed}", failures)
    checks += 2

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1
    print(f"test-gen-abi-manifest: OK ({checks} checks passed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

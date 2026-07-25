#!/usr/bin/env python3
"""Self-test for gen-abi-manifest.py.

The manifest gate is host-dependent: it only fails on a host whose ABI key is
absent, which previously meant a missing key stayed invisible until an hour of
CI on that host. These checks guard the parts that keep the gate honest from
any machine:

* the supported-key set covers every architecture/calling-convention pair the
  conformance matrix runs on, and a manifest missing one of them is rejected;
* the probe's value ordering round-trips, so a cross-measured entry means what a
  natively measured one means;
* the object decoder and its arity check reject malformed probe output rather
  than silently producing a short entry.
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

    # 3. The probe's value ordering round-trips: what build_probe emits is what
    #    parse_probe reads back.
    layouts = [("NevercAlpha", ["First", "Second"]), ("NevercBeta", [])]
    source = mod.build_probe(layouts, None)
    for fragment in ("sizeof(void *)", "sizeof(NevercAlpha)",
                     "_Alignof(NevercAlpha)", "offsetof(NevercAlpha, First)",
                     "sizeof(((NevercAlpha *)0)->Second)", "sizeof(NevercBeta)"):
        expect(fragment in source,
               f"probe omitted {fragment}", failures)
        checks += 1

    # ptr, then (size, align) + (offset, size) per field, per struct.
    values = [8, 24, 8, 0, 4, 8, 16, 16, 8]
    expect(mod.probe_value_count(layouts, None) == len(values),
           "probe arity disagrees with the emitted ordering", failures)
    checks += 1
    width, structs = mod.parse_probe(values, layouts, None)
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
    packed_width, packed = mod.parse_probe([8, 24, 8, 16, 8], layouts, 1)
    expect(packed_width == 64 and packed["NevercAlpha"]["size"] == 24
           and packed["NevercAlpha"]["fields"] == {},
           "pack-variant probe misparsed", failures)
    checks += 1

    # 4. A truncated probe is an error, not a short entry.
    try:
        mod.parse_probe([8, 24], layouts, None)
        failures.append("truncated probe was accepted")
    except ValueError:
        pass
    checks += 1

    # 5. Values are decoded little-endian from behind the marker, and an object
    #    without the marker -- or with it twice -- is rejected rather than
    #    guessed at.
    payload = b"\x00pad" + mod.PROBE_SENTINEL + (8).to_bytes(8, "little") \
        + (16).to_bytes(8, "little") + b"trailing"
    expect(mod.decode_probe(payload, 2) == [8, 16],
           "decode_probe misread the marked values", failures)
    checks += 1
    for bad, why in ((b"no marker here", "unmarked"),
                     (mod.PROBE_SENTINEL + (8).to_bytes(8, "little")
                      + mod.PROBE_SENTINEL, "doubly marked"),
                     (mod.PROBE_SENTINEL + b"\x01\x02", "truncated")):
        try:
            mod.decode_probe(bad, 2)
            failures.append(f"{why} probe object was accepted")
        except ValueError:
            pass
        checks += 1

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1
    print(f"test-gen-abi-manifest: OK ({checks} checks passed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

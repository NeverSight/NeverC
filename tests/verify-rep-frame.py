#!/usr/bin/env python3
"""Check i386 REP register transport without borrowing the compiler's ESI base.

Compile realigned dynamic frames in three formats and at O0/O2. Controlled
completions check exact native inputs, all outputs, DF/flags and stack canaries;
they do not execute port I/O or qualify hardware REP restart semantics.
Requires capstone, lief and unicorn. NeverC itself remains x64-only.
"""

import argparse
import itertools
import json
import struct
import subprocess
from pathlib import Path

import capstone
import lief
import unicorn
import unicorn.x86_const as regs

CODE, STACK, DATA, PAGE = 0x10000, 0x80000, 0xA0000, 0x1000
STOP = CODE + PAGE - 1
TRIPLES = ("i386-unknown-linux-gnu", "i386-pc-windows-msvc", "i386-apple-darwin")
FRAME = 0x12345678
RESULTS = {"ESI": 0x90807060, "EDI": 0x76543210, "ECX": 0x24681357, "EAX": 0xCAFEBABE}


def run(command):
    result = subprocess.run(list(map(str, command)), capture_output=True, text=True)
    assert result.returncode == 0, (command, result.stderr)


def source(operation, address, raw, width):
    count = 2 if operation == "outs" else 3
    compare = operation == "cmps"
    ty = "{" + ",".join(["i32"] * count + (["i16"] if compare else [])) + "}"
    if raw:
        names = {1: "b", 2: "w", 4: "d.rep"}
        api = (
            "llvm.x86.movs" + names[width] + "32"
            if operation == "movs"
            else "llvm.x86.out" + {1: "byte", 2: "word", 4: "dword"}[width] + "string"
        )
        parameters = (
            "ptr %dst_ptr,ptr %src_ptr,i32 %count"
            if operation == "movs"
            else "i16 %port,ptr %src_ptr,i32 %count"
        )
        signature = "ptr,ptr,i32" if operation == "movs" else "i16,ptr,i32"
        ty = "void"
    else:
        api = "llvm.x86.rep." + operation + ".state32"
        parameters = (
            "i32 %src,i32 %dst,i32 %count"
            if operation in ("movs", "cmps")
            else "i32 %src,i32 %count,"
            + ("i16 %port" if operation == "outs" else "i32 %value")
        )
        parameters += ",i8 %direction" + (",i16 %flags" if compare else "")
        parameters += f",i32 {width},i32 {address},i32 0" + (
            ",i32 0" if compare else ""
        )
        signature = ",".join(p.split()[0] for p in parameters.split(","))
    lines = [
        f"declare {ty} @{api}({signature})",
        'define i32 @probe(i32 %src,i32 %dst,i32 %count,i32 %value,i8 %direction,i16 %flags,i32 %n,ptr %out) "no-stack-arg-probe" {',
        "%fixed = alloca [16 x i32], align 64",
        f"store volatile i32 {FRAME}, ptr %fixed",
        "%dynamic = alloca i8, i32 %n, align 32",
        "store volatile i8 165, ptr %dynamic",
        "%src_ptr = inttoptr i32 %src to ptr",
        "%dst_ptr = inttoptr i32 %dst to ptr",
        "%port = trunc i32 %value to i16",
        ("" if raw else "%r = ") + f"call {ty} @{api}({parameters})",
    ]
    if not raw:
        for i in range(count + int(compare)):
            lines.append(f"%r{i} = extractvalue {ty} %r, {i}")
            value = f"%r{i}"
            if i == count:
                lines.append(f"%flags_out = zext i16 {value} to i32")
                value = "%flags_out"
            lines += [
                f"%p{i} = getelementptr i32, ptr %out, i32 {i}",
                f"store volatile i32 {value}, ptr %p{i}",
            ]
    lines += [
        "%canary = load volatile i32, ptr %fixed",
        "%byte = load volatile i8, ptr %dynamic",
        "%b = zext i8 %byte to i32",
        "%ret = xor i32 %canary, %b",
        "ret i32 %ret",
        "}",
    ]
    return "\n".join(lines) + "\n"


def text_section(path):
    obj = lief.parse(str(path))
    return bytes(
        next(
            s for s in obj.sections if s.name in (".text", "__text") and len(s.content)
        ).content
    )


def execute(code, operation, raw, direction, flags, allocation):
    reg = lambda name: getattr(regs, "UC_X86_REG_" + name)
    machine = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
    for base in (CODE, STACK, DATA):
        machine.mem_map(base, PAGE)
    machine.mem_write(CODE, code)
    source, destination, count, value = 0xDEAD0010, 0xBAAD0040, 7, 0x98760123
    params = (source, destination, count, value, direction, flags, allocation, DATA)
    sp = STACK + PAGE - 64
    machine.mem_write(sp, struct.pack("<9I", STOP, *params))
    machine.reg_write(reg("ESP"), sp)
    machine.reg_write(reg("EFLAGS"), 0x202 | (direction << 10))
    saved = {
        name: 0xA0100010 + i for i, name in enumerate(("EBX", "EBP", "ESI", "EDI"))
    }
    for name, val in saved.items():
        machine.reg_write(reg(name), val)
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    accesses = [
        i
        for i in decoder.disasm(code, CODE)
        if i.mnemonic.startswith(("rep ", "repe ", "repne "))
    ]
    assert len(accesses) == 1, ("native REP count", len(accesses))
    access = accesses[0]
    assert access.mnemonic.split()[-1].startswith(operation[:-1]), access.mnemonic
    machine.emu_start(CODE, access.address, count=5000)
    assert machine.reg_read(reg("EIP")) == access.address
    assert machine.reg_read(reg("ESI")) == source, "source transport"
    assert machine.reg_read(reg("ECX")) == count, "count transport"
    if operation in ("movs", "cmps"):
        assert machine.reg_read(reg("EDI")) == destination, "destination transport"
    if operation == "lods":
        assert machine.reg_read(reg("EAX")) == value, "accumulator transport"
    if operation == "outs":
        assert machine.reg_read(reg("EDX")) & 0xFFFF == value & 0xFFFF, "port transport"
    ef = machine.reg_read(reg("EFLAGS"))
    assert (ef >> 10) & 1 == direction, "direction transport"
    if operation == "cmps":
        assert ((ef & 0xD5) << 8 | ((ef >> 11) & 1)) == flags & 0xD501
    for name, val in RESULTS.items():
        if name == "EAX" and operation not in ("lods", "cmps"):
            continue
        if name == "EDI" and operation not in ("movs", "cmps"):
            continue
        machine.reg_write(reg(name), val)
    packed = flags ^ 0xD501
    if operation == "cmps":
        machine.reg_write(
            reg("EFLAGS"), (ef & ~0x8D5) | ((packed >> 8) & 0xD5) | ((packed & 1) << 11)
        )
    machine.emu_start(access.address + access.size, STOP, count=5000)
    assert machine.reg_read(reg("EIP")) == STOP
    assert machine.reg_read(reg("EAX")) == FRAME ^ 0xA5, "stack canary"
    assert machine.reg_read(reg("ESP")) == sp + 4, "cdecl stack"
    assert all(
        machine.reg_read(reg(n)) == v for n, v in saved.items()
    ), "callee-saved registers"
    assert ((machine.reg_read(reg("EFLAGS")) >> 10) & 1) == (direction if raw else 0)
    if not raw:
        names = (
            ("ESI", "EDI", "ECX")
            if operation in ("movs", "cmps")
            else ("ESI", "ECX", "EAX") if operation == "lods" else ("ESI", "ECX")
        )
        expected = tuple(RESULTS[n] for n in names) + (
            (packed & 0xD501,) if operation == "cmps" else ()
        )
        assert (
            struct.unpack(
                "<" + "I" * len(expected),
                bytes(machine.mem_read(DATA, 4 * len(expected))),
            )
            == expected
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--llc", type=Path, required=True)
    parser.add_argument("--keep", type=Path, required=True)
    args = parser.parse_args()
    args.keep.mkdir(parents=True, exist_ok=True)
    forms = [
        (op, address, False, width)
        for op, address, width in itertools.product(
            ("movs", "lods", "cmps", "outs"), (16, 32), (1, 2, 4)
        )
    ]
    forms += [
        (op, 32, True, width)
        for op, width in itertools.product(("movs", "outs"), (1, 2, 4))
    ]
    reports = []
    for (operation, address, raw, width), triple, opt in itertools.product(
        forms, TRIPLES, ("O0", "O2")
    ):
        stem = f"{operation}-{address}-{raw}-{width}-{triple}-{opt}"
        ir, obj = (args.keep / (stem + suffix) for suffix in (".ll", ".o"))
        ir.write_text(source(operation, address, raw, width))
        cmd = [args.llc, "-mtriple=" + triple, "-" + opt, "-verify-machineinstrs", ir]
        run(cmd + ["-filetype=obj", "-o", obj])
        code = text_section(obj)
        for direction, flags, allocation in itertools.product(
            (0, 1), (0, 0xD501, 0x5100), (65, 129)
        ):
            try:
                execute(code, operation, raw, direction, flags, allocation)
            except Exception as error:
                raise AssertionError(
                    (stem, direction, flags, allocation, error)
                ) from error
        for dialect in ("att", "intel"):
            asm = args.keep / (stem + "-" + dialect + ".s")
            assembled = asm.with_suffix(".o")
            run(cmd + ["-x86-asm-syntax=" + dialect, "-o", asm])
            run(
                [
                    args.llc.parent / "llvm-mc",
                    "-triple=" + triple,
                    "-filetype=obj",
                    asm,
                    "-o",
                    assembled,
                ]
            )
            assert text_section(assembled) == code, (
                stem,
                dialect,
                "assembly round trip",
            )
        reports.append(dict(case=stem, controlled_completions=12))
    (args.keep / "qualification.json").write_text(json.dumps(reports, indent=2) + "\n")
    print(
        f"PASS: {len(reports)} i386 REP frame objects, {12 * len(reports)} controlled completions, both assembly dialects"
    )


if __name__ == "__main__":
    main()

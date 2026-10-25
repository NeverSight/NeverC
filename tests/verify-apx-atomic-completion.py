#!/usr/bin/env python3
"""Check APX native atomic encoding, operand transport, effects and C ABI.

Controlled completions execute surrounding machine code, independently decode
the APX instruction, and supply its old-memory result, write-back and flags.
They do not qualify physical atomicity, cache coherence, privilege or faults.
Requires capstone, lief and unicorn. --llc selects the full LLVM fork;
--backend-llc compiles NeverC IR with that fork.
"""
import argparse
import itertools
import json
import struct
import subprocess
import tempfile
from pathlib import Path

import capstone
import lief
import unicorn
import unicorn.x86_const as regs

TRIPLES = ("x86_64-unknown-linux-gnu", "x86_64-pc-windows-msvc", "x86_64-apple-darwin")
SEGMENTS = (0, 0x26, 0x2e, 0x36, 0x3e, 0x64, 0x65)
GPRS = ("RAX", "RCX", "RDX", "RBX", "RSP", "RBP", "RSI", "RDI",
        "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15")
CODE, STACK, DATA, PAGE = 0x1000000, 0x80000, 0xa0000, 4096
MASK = (1 << 64) - 1
SAMPLES = ((0, 0, 0), (MASK, 1, MASK), (0x80000000, 0x7fffffff, 0x80000001),
           (0x8000000000000000, 0x7fffffffffffffff, 0x8000000000000001),
           (0x3141592653589793, 0x0123456789abcdef, 0x8765432101234567),
           (0x55aa55aaaa55aa55, 0x55aa55aaaa55aa55, 0))


def run(command, failure=None):
    result = subprocess.run(list(map(str, command)), capture_output=True, text=True)
    if failure:
        assert result.returncode and failure in result.stderr, (command, result.returncode, result.stderr)
    elif result.returncode:
        raise AssertionError(f"command failed: {command}\n{result.stderr}")
    return result.stdout


def text_section(binary):
    sections = [s for s in binary.sections
                if s.name in (".text", "__text") and len(s.content)]
    assert len(sections) == 1, "compile this controlled fixture with -fno-function-sections"
    return sections[0]


def object_text(path):
    return bytes(text_section(lief.parse(str(path))).content)


def assembly_roundtrips(command, obj, triple, compiler, backend):
    """Exercise both assembly printers and parsers, including pruned tables."""
    expected = function_instruction_bytes(obj)
    for dialect in ("att", "intel"):
        asm = obj.with_suffix("." + dialect + ".s")
        rebuilt = obj.with_suffix("." + dialect + ".o")
        if backend:
            run(command + ["-x86-asm-syntax=" + dialect, "-filetype=asm", "-o", asm])
            run([backend.parent / "llvm-mc", "-triple=" + triple,
                 "-filetype=obj", asm, "-o", rebuilt])
        else:
            run(command + ["-masm=" + dialect, "-S", "-o", asm])
            run([compiler, "--target=" + triple, "-c", asm, "-o", rebuilt])
        assert function_instruction_bytes(rebuilt) == expected, (dialect, obj, rebuilt)
    return 2


def cases():
    result = []
    for bits, address, segment in itertools.product((32, 64), (32, 64), SEGMENTS):
        for op, cc in [("aadd", 0), ("aand", 0), ("aor", 0), ("axor", 0)] + [("cmpccxadd", cc) for cc in range(16)]:
            for variant in ("live", "dead", "twice"):
                result.append((op, bits, address, segment, cc, variant))
    return result


def source(matrix, ir, legacy=False):
    declarations, functions = set(), []
    for index, (op, bits, address, segment, cc, variant) in enumerate(matrix):
        compare = op == "cmpccxadd"
        word, api = f"i{bits}", f"llvm.x86.{op}{bits}{'.legacy' if legacy else ''}.address"
        ty = word if compare else "void"
        count = 2 if variant == "twice" else 1
        if ir:
            types = ["i64", word] + ([word, "i32"] if compare else []) + ["i32", "i32"]
            declarations.add(f"declare {ty} @{api}({', '.join(types)})")
            body = ["%condition = icmp ult i64 %compare, %add",
                    "%before = select i1 %condition, i64 %compare, i64 %add",
                    "store i64 %before, ptr %ordinary",
                    "%condition.word = zext i1 %condition to i64"]
            if bits == 32:
                body += ["%cmp = trunc i64 %compare to i32", "%src = trunc i64 %add to i32"]
            else:
                body += ["%cmp = add i64 %compare, 0", "%src = add i64 %add, 0"]
            values = ["%address", "%cmp", "%src", str(cc), str(address), str(segment)] if compare else ["%address", "%src", str(address), str(segment)]
            terms = ["%address", "%compare", "%add", "%condition.word", "%memory"]
            for call in range(count):
                body.append((f"%old{call} = " if compare else "") +
                            f"call {ty} @{api}({', '.join(t + ' ' + v for t, v in zip(types, values))})")
                if compare and variant != "dead":
                    if bits == 32:
                        body.append(f"%wide{call} = zext i32 %old{call} to i64")
                    else:
                        body.append(f"%wide{call} = add i64 %old{call}, 0")
                    terms.append(f"%wide{call}")
            body.append("%memory = load i64, ptr %ordinary")
            current = terms[0]
            for i, term in enumerate(terms[1:]):
                body.append(f"%result{i} = xor i64 {current}, {term}")
                current = f"%result{i}"
            body.append("ret i64 " + current)
            functions.append(f"define i64 @apx_{index}(i64 %address, i64 %compare, i64 %add, ptr %ordinary) {{\n" + "\n".join(body) + "\n}")
        else:
            word = "unsigned long long" if bits == 64 else "unsigned"
            args = ["address", f"({word})compare", f"({word})add", str(cc), str(address), str(segment)] if compare else ["address", f"({word})add", str(address), str(segment)]
            body = ["unsigned long long condition = compare < add;",
                    "*ordinary = condition ? compare : add;"]
            terms = ["address", "compare", "add", "condition", "*ordinary"]
            for call in range(count):
                body.append((f"{word} old{call} = " if compare else "") +
                            f"__builtin_ia32_{op}{bits}{'_legacy' if legacy else ''}_address({', '.join(args)});")
                if compare and variant != "dead":
                    terms.append(f"(unsigned long long)old{call}")
            body.append("return " + " ^ ".join(terms) + ";")
            functions.append(f"unsigned long long apx_{index}(unsigned long long address, unsigned long long compare, unsigned long long add, unsigned long long *ordinary) {{\n" + "\n".join(body) + "\n}")
    return "\n".join(sorted(declarations)) + "\n" + "\n".join(functions) + "\n"


def decode(code, stop_at_return=False, mode64=True):
    """Read atomic memory encodings independently of the producer's decoder."""
    cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64 if mode64 else capstone.CS_MODE_32)
    rex_prefixes = tuple(range(0x40, 0x50)) if mode64 else ()
    result, at = {}, 0
    while at < len(code):
        start, segment, address, mandatory, rex = at, 0, 64 if mode64 else 32, 0, 0
        while at < len(code) and code[at] in SEGMENTS[1:] + (0x67, 0x66, 0xf2, 0xf3) + rex_prefixes:
            if code[at] in rex_prefixes:
                rex = code[at]
                at += 1
                continue
            rex = 0
            if code[at] in (0x66, 0xf2, 0xf3):
                mandatory = code[at]
            elif code[at] == 0x67:
                address = 32 if mode64 else 16
            else:
                segment = code[at]
            at += 1
        if at + 6 <= len(code) and code[at] == 0x62:
            p0, p1, p2, opcode, modrm = code[at + 1:at + 6]
            opmap, pp = p0 & 7, p1 & 3
            compare = opmap == 2 and pp == 1 and 0xe0 <= opcode <= 0xef
            rao = opmap == 4 and opcode == 0xfc
            if compare or rao:
                assert mode64, "APX atomic encoding requires long mode"
                assert (p2 & 0xf7) == 0 and modrm >> 6 != 3
                if rao:
                    assert p2 == 8 and p1 & 0x78 == 0x78
                register = ((modrm >> 3) & 7) | ((~p0 & 0x80) >> 4) | (~p0 & 0x10)
                add = ((~p1 & 0x78) >> 3) | ((~p2 & 8) << 1) if compare else register
                base = (modrm & 7) | ((~p0 & 0x20) >> 2) | ((p0 & 8) << 1)
                # Native address controls deliberately keep DS-default RBX.
                assert modrm >> 6 == 0 and base == 3, (start, modrm, base)
                assert add < len(GPRS) and register < len(GPRS), "controlled oracle does not execute EGPR setup"
                name = "cmpccxadd" if compare else {0: "aadd", 1: "aand", 3: "aor", 2: "axor"}[pp]
                result[start] = dict(op=name, bits=64 if p1 & 0x80 else 32,
                                     address=address, segment=segment,
                                     cc=opcode - 0xe0 if compare else 0,
                                     compare=register, add=add, base=base, end=at + 6, legacy=False)
                at += 6
                continue
        is_vex = at + 5 <= len(code) and code[at] == 0xc4 and code[at + 1] & 31 == 2 and 0xe0 <= code[at + 3] <= 0xef
        is_rao = at + 4 <= len(code) and code[at:at + 3] == b"\x0f\x38\xfc"
        if is_vex or is_rao:
            if is_vex:
                assert mode64, "VEX CMPccXADD requires long mode"
                p0, p1, opcode, modrm = code[at + 1:at + 5]
                assert p1 & 7 == 1 and not mandatory and not rex
                register = ((modrm >> 3) & 7) | ((~p0 & 0x80) >> 4)
                add = (~p1 & 0x78) >> 3
                base = (modrm & 7) | ((~p0 & 0x20) >> 2)
                bits, cc, end, name = 64 if p1 & 0x80 else 32, opcode - 0xe0, at + 5, "cmpccxadd"
            else:
                modrm = code[at + 3]
                register = ((modrm >> 3) & 7) | ((rex & 4) << 1)
                add, base = register, (modrm & 7) | ((rex & 1) << 3)
                bits, cc, end = 64 if rex & 8 else 32, 0, at + 4
                name = {0: "aadd", 0x66: "aand", 0xf2: "aor", 0xf3: "axor"}[mandatory]
                if address == 16:
                    assert modrm & 7 == 7, "addr16 native carrier must be BX"
                    base = 3
            assert modrm >> 6 == 0 and base == 3, (start, modrm, base)
            result[start] = dict(op=name, bits=bits, address=address, segment=segment,
                                 cc=cc, compare=register, add=add, base=base, end=end, legacy=True)
            at = end
            continue
        instruction = next(cs.disasm(code[start:], start, count=1), None)
        assert instruction is not None, (start, code[start:start + 16].hex())
        at = start + instruction.size
        if stop_at_return and instruction.mnemonic == "ret":
            break
    return result


def instruction_bytes(code):
    # llc and llvm-mc can choose different multi-byte NOPs for alignment.
    # Compare every real instruction's bytes AND address, including APX, so
    # padding differences cannot conceal an encoding or layout regression.
    native, result, at = decode(code, stop_at_return=True), [], 0
    cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    while at < len(code):
        if at in native:
            end = native[at]["end"]
            result.append((at, code[at:end]))
            at = end
            continue
        instruction = next(cs.disasm(code[at:], at, count=1), None)
        assert instruction is not None, (at, code[at:at + 16].hex())
        if instruction.mnemonic != "nop":
            result.append((at, bytes(instruction.bytes)))
        at += instruction.size
        if instruction.mnemonic == "ret":
            break
    return result


def function_instruction_bytes(path):
    # NeverC can emit independent functions in different orders. Match by
    # symbol identity and compare instruction offsets relative to that entry.
    binary = lief.parse(str(path))
    text = text_section(binary)
    code = bytes(text.content)
    functions = sorted((s.value - getattr(text, "virtual_address", 0), s.name.lstrip("_"))
                       for s in binary.symbols if s.name.lstrip("_").startswith("apx_"))
    result = {}
    for index, (start, name) in enumerate(functions):
        end = functions[index + 1][0] if index + 1 < len(functions) else len(code)
        result[name] = instruction_bytes(code[start:end])
    return result


def flags_sub(old, compare, bits):
    mask = (1 << bits) - 1
    difference = (old - compare) & mask
    cf, zf, sf = old < compare, difference == 0, difference >> (bits - 1)
    of = ((old ^ compare) & (old ^ difference)) >> (bits - 1) & 1
    af = (old ^ compare ^ difference) >> 4 & 1
    pf = (difference & 255).bit_count() % 2 == 0
    return int(cf) | int(pf) << 2 | af << 4 | int(zf) << 6 | sf << 7 | of << 11


def condition(cc, flags):
    cf, pf, zf, sf, of = ((flags >> bit) & 1 for bit in (0, 2, 6, 7, 11))
    base = (of, cf, zf, cf | zf, sf, pf, sf ^ of, zf | (sf ^ of))[cc // 2]
    return bool(base ^ (cc & 1))


def qualify(obj, matrix, windows, legacy=False):
    binary = lief.parse(str(obj))
    text = text_section(binary)
    code = bytes(text.content)
    symbols = {s.name.lstrip("_"): s.value - getattr(text, "virtual_address", 0)
               for s in binary.symbols if s.name.lstrip("_").startswith("apx_")}
    assert len(symbols) == len(matrix), (len(symbols), len(matrix))
    # All generated wrappers have one final return. Parallel object emission
    # can insert zero-filled alignment between independently emitted groups;
    # it is outside the functions and must not be decoded as instructions.
    native = {}
    starts = sorted(symbols.values())
    for index, start in enumerate(starts):
        end = starts[index + 1] if index + 1 < len(starts) else len(code)
        for offset, insn in decode(code[start:end], stop_at_return=True).items():
            native[start + offset] = {**insn, "end": start + insn["end"]}
    assert len(native) == sum(2 if c[-1] == "twice" else 1 for c in matrix)
    assert all(i["legacy"] == legacy for i in native.values()), "native encoding family changed"
    patched = bytearray(code)
    for offset, insn in native.items():
        patched[offset:insn["end"]] = b"\x90" * (insn["end"] - offset)
    machine = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_64)
    span = (len(code) + 2 * PAGE - 1) // PAGE * PAGE
    stop = CODE + span - 1
    machine.mem_map(CODE, span)
    machine.mem_write(CODE, bytes(patched))
    machine.mem_map(STACK, 4 * PAGE)
    machine.mem_map(DATA, 4 * PAGE)
    reg = lambda name: getattr(regs, "UC_X86_REG_" + name)
    def boundary(uc, address, size, user):
        if address - CODE in native or address == stop:
            uc.emu_stop()
    machine.hook_add(unicorn.UC_HOOK_CODE, boundary)
    saved_names = ("RBX", "RBP", "R12", "R13", "R14", "R15") + (("RSI", "RDI") if windows else ())
    checks = 0
    for index, case in enumerate(matrix):
        op, bits, address_bits, segment, cc, variant = case
        width, mask = bits // 8, (1 << bits) - 1
        for old, compare, add in SAMPLES:
            for alias in (False, True):
                for i, name in enumerate(GPRS):
                    machine.reg_write(reg(name), 0x2345678900000000 + i)
                saved = {name: machine.reg_read(reg(name)) for name in saved_names}
                # Exercise high address bits with 67H and nonzero FS/GS bases.
                offset = 0x208 if segment in (0x64, 0x65) else DATA + 0x208
                if address_bits == 32:
                    offset |= 0x8765432100000000
                fs, gs = DATA, DATA + PAGE
                machine.reg_write(reg("FS_BASE"), fs)
                machine.reg_write(reg("GS_BASE"), gs)
                linear = (offset & (0xffffffff if address_bits == 32 else MASK)) + (fs if segment == 0x64 else gs if segment == 0x65 else 0)
                ordinary = linear if alias else DATA + 0x800
                machine.mem_write(linear, struct.pack("<Q", old))
                if not alias:
                    machine.mem_write(ordinary, bytes(8))
                args = (offset, compare, add, ordinary)
                names = ("RCX", "RDX", "R8", "R9") if windows else ("RDI", "RSI", "RDX", "RCX")
                for name, value in zip(names, args):
                    machine.reg_write(reg(name), value)
                sp = STACK + 2 * PAGE - 8
                machine.reg_write(reg("RSP"), sp)
                machine.mem_write(sp, struct.pack("<Q", stop))
                machine.reg_write(reg("EFLAGS"), 0x202)
                pc, seen, results = CODE + symbols[f"apx_{index}"], 0, []
                while pc != stop:
                    machine.emu_start(pc, stop, count=10000)
                    pc = machine.reg_read(reg("RIP"))
                    if pc == stop:
                        break
                    assert pc - CODE in native, (index, hex(pc))
                    insn = native[pc - CODE]
                    assert tuple(insn[k] for k in ("op", "bits", "address", "segment", "cc")) == case[:5], (case, insn)
                    assert machine.reg_read(reg(GPRS[insn["base"]])) == (offset & (0xffffffff if address_bits == 32 else MASK))
                    assert machine.reg_read(reg(GPRS[insn["add"]])) & mask == add & mask
                    before = min(compare, add)
                    if seen == 0:
                        assert struct.unpack("<Q", machine.mem_read(ordinary, 8))[0] == before, "ordinary store moved after atomic"
                    value = int.from_bytes(machine.mem_read(linear, width), "little")
                    source_value = add & mask
                    if op == "cmpccxadd":
                        assert machine.reg_read(reg(GPRS[insn["compare"]])) & mask == compare & mask
                        flags = flags_sub(value, compare & mask, bits)
                        updated = (value + source_value) & mask if condition(cc, flags) else value
                        machine.reg_write(reg(GPRS[insn["compare"]]), value)
                        machine.reg_write(reg("EFLAGS"), (machine.reg_read(reg("EFLAGS")) & ~0x8d5) | flags)
                        results.append(value)
                    else:
                        updated = {"aadd": lambda: (value + source_value) & mask,
                                   "aand": lambda: value & source_value,
                                   "aor": lambda: value | source_value,
                                   "axor": lambda: value ^ source_value}[op]()
                    machine.mem_write(linear, updated.to_bytes(width, "little"))
                    seen += 1
                    pc = CODE + insn["end"]
                    machine.reg_write(reg("RIP"), pc)
                assert seen == (2 if variant == "twice" else 1), "atomic operation was removed or duplicated"
                expected = offset ^ compare ^ add ^ int(compare < add) ^ struct.unpack("<Q", machine.mem_read(ordinary, 8))[0]
                if variant != "dead":
                    for result in results:
                        expected ^= result
                assert machine.reg_read(reg("RAX")) == expected, (case, old, compare, add, hex(machine.reg_read(reg("RAX"))), hex(expected))
                assert all(machine.reg_read(reg(name)) == value for name, value in saved.items()), "callee-saved register corrupted"
                checks += 1
    return {"functions": len(matrix), "boundaries": len(native), "controlled_completions": checks}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=Path("build-neverc/bin/neverc"))
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--llc", type=Path)
    group.add_argument("--backend-llc", type=Path)
    parser.add_argument("--keep", type=Path)
    parser.add_argument("--quick", action="store_true")
    parser.add_argument("--legacy", action="store_true", help="qualify exact legacy RAO/VEX CMPccXADD")
    args = parser.parse_args()
    for name in ("compiler", "llc", "backend_llc"):
        if getattr(args, name):
            setattr(args, name, getattr(args, name).resolve())
    matrix = cases()
    if args.quick:
        matrix = [c for c in matrix if c[3] in (0, 0x64, 0x65) and c[4] in (0, 4, 15)]
    with tempfile.TemporaryDirectory(prefix="apx-native-") as temp:
        directory = args.keep or Path(temp)
        directory.mkdir(parents=True, exist_ok=True)
        results = []
        for opt, triple in itertools.product(("O0", "O2"), TRIPLES[:1] if args.quick else TRIPLES):
            stem = directory / f"{opt}-{TRIPLES.index(triple)}"
            src, ir, obj = (stem.with_suffix(suffix) for suffix in (".c", ".ll", ".o"))
            if args.llc:
                ir.write_text(source(matrix, True, args.legacy))
            else:
                src.write_text(source(matrix, False, args.legacy))
                # Match standalone assembler relaxation at O0 as well as O2.
                command = [args.compiler, "--target=" + triple, "-" + opt,
                           "-mno-relax-all", "-fno-function-sections",
                           "-ffreestanding", "-fno-stack-protector", "-fno-lto",
                           "-fno-builtin-mimalloc", "-mllvm", "-verify-machineinstrs", src]
                run(command + ["-S", "-emit-llvm", "-o", ir])
                assert " asm " not in ir.read_text()
                if not args.backend_llc:
                    run(command + ["-c", "-o", obj])
            backend = args.llc or args.backend_llc
            if backend:
                selected = ir
                if opt == "O2":
                    selected = stem.with_suffix(".opt.ll")
                    run([backend.parent / "opt", "-passes=default<O2>", "-S", ir, "-o", selected])
                command = [backend, "-mtriple=" + triple, "-" + opt,
                           "-verify-machineinstrs", selected]
                run(command + ["-filetype=obj", "-o", obj])
            roundtrips = assembly_roundtrips(command, obj, triple, args.compiler, backend)
            result = {"optimization": opt, "triple": triple,
                      "assembly_roundtrips": roundtrips,
                      **qualify(obj, matrix, "windows" in triple, args.legacy)}
            results.append(result)
            print(result, flush=True)
        report = {"legacy_encoding": args.legacy, "results": results, "physical_atomicity_qualified": False}
        (directory / "qualification.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()


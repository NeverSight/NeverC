#!/usr/bin/env python3
"""Qualify native bounded SVM, VMFUNC EPTP switching and supervisor state.

Executes surrounding scalar code while checking independent native byte and
register boundaries. Tests compiler transport, ordering, resumable HLT code,
three object formats, both assembly dialects and actual Linux CPL3 faults.
Successful virtualization/state effects require a CPL0 guest qualification.
Requires capstone, lief and unicorn; --backend-llc checks the full LLVM bridge.
"""

import argparse
import importlib.util
import itertools
import platform
import signal
import subprocess
import sys
import struct
import tempfile
from pathlib import Path

import capstone
import unicorn
import unicorn.x86_const as regs


SPEC = importlib.util.spec_from_file_location(
    "descriptor", Path(__file__).with_name("verify-descriptor-query-completion.py"))
COMMON = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(COMMON)
run, text_section = COMMON.run, COMMON.text_section
CODE, STACK, DATA, PAGE = COMMON.CODE, COMMON.STACK, COMMON.DATA, COMMON.PAGE
STOP, SOURCE, ORDINARY = COMMON.STOP, COMMON.SOURCE, COMMON.ORDINARY
VALUES = ((0, 0, 0), (0xd012345689ab002b, 0xaabbccdd12345678, 0x91726354aabbccdd),
          (0xffffffffffffffff, 0xffffffffffffffff, 0x8000000080000000))
DELTA = (0x1234, 0x55667788, 0xdeadbeef)


ENCODINGS = {
    "vmload": bytes.fromhex("0f01da"), "vmsave": bytes.fromhex("0f01db"),
    "invlpga": bytes.fromhex("0f01df"), "stgi": bytes.fromhex("0f01dc"),
    "clgi": bytes.fromhex("0f01dd"), "swapgs": bytes.fromhex("0f01f8"),
    "stac": bytes.fromhex("0f01cb"), "clac": bytes.fromhex("0f01ca"),
    "hlt": bytes.fromhex("f4"), "vmfunc.eptp.switch": bytes.fromhex("0f01d4"),
}

for operation in ("vmload", "vmsave", "invlpga"):
    for bits in (16, 32):
        ENCODINGS[f"{operation}.addr{bits}"] = b"\x67" + ENCODINGS[operation]


def instructions(code, mode):
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64 if mode == 64 else capstone.CS_MODE_32)
    result = list(decoder.disasm(code, CODE))
    assert sum(i.size for i in result) == len(code), "incomplete instruction decoding"
    return result


def compile_case(args, directory, operation, variant, mode, opt, target=COMMON.TRIPLES[0]):
    name = f"{operation}-{variant}-{mode}-{opt}-{COMMON.TRIPLES.index(target)}"
    ir, obj = directory / (name + ".ll"), directory / (name + ".o")
    word = f"i{mode}"
    base = operation.split(".")[0]
    address_bits = int(operation.rsplit("addr", 1)[1]) if ".addr" in operation else mode
    triple = ("x86_64" if mode == 64 else "i686") + "-" + target
    api = "llvm.x86." + operation + ("32" if operation in ("vmload", "vmsave", "invlpga") and mode == 32 else "")
    count = 2 if variant == "twice" else 1
    if args.llc:
        body = [f"%pointer = ptrtoint ptr %source to {word}"]
        if variant == "memory":
            body.append(f"store {word} %a, ptr %ordinary")
        if variant == "condition":
            body += [f"%compare = icmp ult {word} %a, %b", f"%condition = zext i1 %compare to {word}"]
        for i in range(count):
            inputs = []
            for parameter, delta in zip(("a", "b", "c"), DELTA):
                value = "%" + parameter
                if i:
                    value += str(i)
                    body.append(f"{value} = xor {word} %{parameter}, {delta}")
                inputs.append(value)
            if mode == 64:
                for j, value in enumerate(inputs):
                    body.append(f"%narrow{i}_{j} = trunc {word} {value} to i32")
                narrow = [f"%narrow{i}_{j}" for j in range(3)]
            else:
                narrow = inputs
            address = inputs[0]
            if address_bits != mode:
                address = f"%address{i}"
                body.append(f"{address} = trunc {word} {inputs[0]} to i{address_bits}")
            parameters = (f"i{address_bits}" if base in ("vmload", "vmsave") else
                          f"i{address_bits}, i32" if base == "invlpga" else
                          "i32" if operation == "vmfunc.eptp.switch" else "")
            call = (f"i{address_bits} {address}" if base in ("vmload", "vmsave") else
                    f"i{address_bits} {address}, i32 {narrow[1]}" if base == "invlpga" else
                    f"i32 {narrow[1]}" if operation == "vmfunc.eptp.switch" else "")
            body.append(f"call void @{api}({call})")
        terms = ["%a", "%b", "%c", "%pointer"]
        if variant == "memory":
            body.append(f"%loaded = load {word}, ptr %ordinary")
            terms.append("%loaded")
        if variant == "condition":
            terms.append("%condition")
        value = terms[0]
        for i, term in enumerate(terms[1:]):
            body.append(f"%xor{i} = xor {word} {value}, {term}")
            value = f"%xor{i}"
        body.append(f"ret {word} {value}")
        ir.write_text(f"declare void @{api}({parameters})\ndefine {word} @probe({word} %a, {word} %b, {word} %c, ptr %source, ptr %ordinary) {{\n" +
                      "\n".join(body) + "\n}\n")
    else:
        src = directory / (name + ".c")
        body = ["*ordinary = a;"] if variant == "memory" else []
        if variant == "condition":
            body.append("unsigned condition = a < b;")
        for i in range(count):
            a, b, c = (p + (f" ^ {delta}ULL" if i else "") for p, delta in zip(("a", "b", "c"), DELTA))
            call = (a if base in ("vmload", "vmsave") else
                    f"{a}, {b}" if base == "invlpga" else
                    b if operation == "vmfunc.eptp.switch" else "")
            body.append(f"__builtin_ia32_{operation.replace('.', '_')}({call});")
        result = "a ^ b ^ c ^ (unsigned long long)source"
        if variant == "memory":
            result += " ^ *ordinary"
        if variant == "condition":
            result += " ^ condition"
        body.append("return " + result + ";")
        src.write_text('unsigned long long probe(unsigned long long a, unsigned long long b, unsigned long long c, const void *source, unsigned long long *ordinary) {\n' +
                       "\n".join(body) + "\n}\n")
        common = [args.compiler, "--target=" + triple, "-" + opt, "-Werror", "-ffreestanding", "-fno-builtin-mimalloc",
                  "-fno-lto", "-fno-stack-protector", "-mllvm", "-verify-machineinstrs", src]
        run(common + ["-S", "-emit-llvm", "-o", ir])
        assert " asm " not in ir.read_text() and f"@{api}(" in ir.read_text()
        if not args.backend_llc:
            run(common + ["-c", "-o", obj])
    backend = args.llc or args.backend_llc
    if backend:
        common_backend = [backend, "-mtriple=" + triple, "-" + opt, "-verify-machineinstrs", ir]
        run(common_backend + ["-filetype=obj", "-o", obj])
    if variant == "retained":
        for dialect in ("att", "intel"):
            asm = directory / (name + "-" + dialect + ".s")
            assembled = asm.with_suffix(".o")
            if backend:
                run(common_backend + ["-filetype=asm", "-x86-asm-syntax=" + dialect, "-o", asm])
                run([Path(backend).parent / "llvm-mc", "-triple=" + triple, "-filetype=obj", asm, "-o", assembled])
            else:
                run(common + ["-S", "-masm=" + dialect, "-o", asm])
                run([args.compiler, "--target=" + triple, "-c", asm, "-o", assembled])
            assert text_section(obj) == text_section(assembled), "assembly round trip changed bytes"
    return text_section(obj)


def execute(code, operation, variant, mode, values):
    mask = (1 << mode) - 1
    base = operation.split(".")[0]
    address_bits = int(operation.rsplit("addr", 1)[1]) if ".addr" in operation else mode
    a, b, c = (v & mask for v in values)
    source = SOURCE
    machine = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_64 if mode == 64 else unicorn.UC_MODE_32)
    reg = lambda name: getattr(regs, "UC_X86_REG_" + ("R" if mode == 64 else "E") + name)
    pack = lambda value: struct.pack("<Q" if mode == 64 else "<I", value & mask)
    for base in (CODE, STACK, DATA):
        machine.mem_map(base, PAGE)
    machine.mem_write(CODE, code)
    machine.mem_write(SOURCE, b"\x2b\x00")
    machine.mem_write(ORDINARY, pack(COMMON.SENTINEL))
    sp = STACK + PAGE - 64 - mode // 8
    machine.reg_write(reg("SP"), sp)
    machine.mem_write(sp, pack(STOP))
    arguments = (a, b, c, source, ORDINARY)
    if mode == 64:
        for r, v in zip(("CX", "DX", "8", "9"), arguments):
            machine.reg_write(reg(r), v)
        machine.mem_write(sp + 40, pack(ORDINARY))
    else:
        machine.mem_write(sp + 4, b"".join(pack(v) for v in arguments))
    saved = {reg(name): (0xa1b2c3d4e5f60710 + i) & mask for i, name in
             enumerate(("BX", "BP", "SI", "DI", *(("12", "13", "14", "15") if mode == 64 else ())))}
    for r, value in saved.items():
        machine.reg_write(r, value)
    accesses = [i for i in instructions(code, mode) if i.mnemonic == operation.split('.')[0]]
    assert len(accesses) == (2 if variant == "twice" else 1), "native effects were lost or merged"
    assert all(bytes(i.bytes) == ENCODINGS[operation] for i in accesses), "native opcode/prefix changed"
    start = CODE
    for index, instruction in enumerate(accesses):
        machine.emu_start(start, instruction.address, count=1000)
        assert machine.reg_read(reg("IP")) == instruction.address
        av, bv, cv = (v ^ (d if index else 0) for v, d in zip((a, b, c), DELTA))
        if base in ("vmload", "vmsave", "invlpga"):
            address_reg = {16: regs.UC_X86_REG_AX, 32: regs.UC_X86_REG_EAX, 64: regs.UC_X86_REG_RAX}[address_bits]
            assert machine.reg_read(address_reg) == av & ((1 << address_bits) - 1), "native address input changed"
            if base == "invlpga":
                assert machine.reg_read(regs.UC_X86_REG_ECX) == bv & 0xffffffff, "ASID input changed"
        elif operation == "vmfunc.eptp.switch":
            assert machine.reg_read(regs.UC_X86_REG_EAX) == 0, "wrong VMFUNC leaf"
            assert machine.reg_read(regs.UC_X86_REG_ECX) == bv & 0xffffffff, "EPTP index changed"
        if variant == "memory":
            assert bytes(machine.mem_read(ORDINARY, mode // 8)) == pack(a)
            machine.mem_write(ORDINARY, pack(COMMON.SENTINEL))
        start = instruction.address + instruction.size
    machine.emu_start(start, STOP, count=1000)
    result = a ^ b ^ c ^ source
    if variant == "memory":
        result ^= COMMON.SENTINEL & mask
    if variant == "condition":
        result ^= int(a < b)
    assert machine.reg_read(reg("IP")) == STOP and machine.reg_read(reg("AX")) == result, "live inputs or ordinary memory changed"
    assert all(machine.reg_read(r) == value for r, value in saved.items()), "callee-saved register corrupted"


def native_fault_oracle(args, directory):
    if args.llc or platform.system() != "Linux" or platform.machine() != "x86_64":
        return 0
    count = 0
    for operation, opt in itertools.product((op for op in ENCODINGS if not op.endswith("addr16")), ("O0", "O2")):
        base_op = operation.split(".")[0]
        base = directory / f"fault-{operation.replace('.', '_')}-{opt}"
        src, ir, obj, library = (base.with_suffix(suffix) for suffix in (".c", ".ll", ".o", ".so"))
        parameters = "0xdead0000ULL, 0x87654321U" if base_op == "invlpga" else "0xdead0000ULL" if base_op in ("vmload", "vmsave") else "0x87654321U" if operation == "vmfunc.eptp.switch" else ""
        encoded = ", ".join(hex(v) for v in ENCODINGS[operation])
        src.write_text(f'''void probe(void) {{ __builtin_ia32_{operation.replace('.', '_')}({parameters}); }}
void original(void) {{
  unsigned long long address = {"0" if operation == "vmfunc.eptp.switch" else "0xdead0000ULL"};
  unsigned index = 0x87654321U;
  __asm__ volatile(".byte {encoded}" : : "a"(address), "c"(index) : "memory");
}}
''')
        compiler = [args.compiler, "--target=x86_64-unknown-linux-gnu", "-" + opt, "-Werror", "-ffreestanding",
                    "-fno-lto", "-fno-builtin-mimalloc", "-fPIC", "-mllvm", "-verify-machineinstrs", src]
        if args.backend_llc:
            run(compiler + ["-S", "-emit-llvm", "-o", ir])
            run([args.backend_llc, "-" + opt, "-verify-machineinstrs", "-relocation-model=pic", "-filetype=obj", ir, "-o", obj])
        else:
            run(compiler + ["-c", "-o", obj])
        run([args.compiler, "-shared", "-nostdlib", "-fno-lto", "-fno-builtin-mimalloc", obj, "-o", library])
        statuses = []
        for function in ("original", "probe"):
            child = subprocess.run([sys.executable, "-c", "import ctypes,sys; getattr(ctypes.CDLL(sys.argv[1]),sys.argv[2])()", str(library.resolve()), function],
                                   capture_output=True, text=True, timeout=5)
            statuses.append(child.returncode)
            count += 1
        assert statuses[0] in (-signal.SIGILL, -signal.SIGSEGV) and statuses[0] == statuses[1], f"{operation}/{opt}: CPL3 fault mismatch {statuses}"
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=Path("build-neverc/bin/neverc"))
    backend = parser.add_mutually_exclusive_group()
    backend.add_argument("--llc", type=Path)
    backend.add_argument("--backend-llc", type=Path)
    parser.add_argument("--keep", type=Path)
    args = parser.parse_args()
    for key in ("compiler", "llc", "backend_llc"):
        if getattr(args, key):
            setattr(args, key, getattr(args, key).resolve())
    count = 0
    with tempfile.TemporaryDirectory(prefix="bounded-virtualization-") as temp:
        directory = args.keep or Path(temp)
        directory.mkdir(parents=True, exist_ok=True)
        for mode in ((32, 64) if args.llc else (64,)):
            operations = [op for op in ENCODINGS if (mode == 64 or op != "swapgs") and not op.endswith("addr16" if mode == 64 else "addr32")]
            for operation, opt, variant in itertools.product(operations, ("O0", "O2"), ("retained", "twice", "memory", "condition")):
                code = compile_case(args, directory, operation, variant, mode, opt)
                for values in VALUES:
                    try:
                        execute(code, operation, variant, mode, values)
                    except Exception as error:
                        raise AssertionError(f"{operation}, {variant}, {mode}, {opt}, {values}: {error}") from error
                    count += 1
                if variant == "retained":
                    for triple in COMMON.TRIPLES[1:]:
                        compile_case(args, directory, operation, variant, mode, opt, triple)
                        count += 1
        native = native_fault_oracle(args, directory)
    print(f"PASS: {count} bounded virtualization/state checks; {native} real CPL3 fault boundaries")


if __name__ == "__main__":
    main()

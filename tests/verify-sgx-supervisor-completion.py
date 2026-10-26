#!/usr/bin/env python3
"""Qualify native SGX/SMX register, flag, memory and ABI transport.

Controlled completions execute surrounding scalar code and inject all four
register outputs plus CF/ZF where applicable. They do not implement SGX or SMX.
Separate Linux children compare real CPL3 faults or query completion with
independently encoded bytes. Successful enclave, VMX and measured-environment
operations require a suitable execution environment.
--bounded-enclu checks constant fallthrough leaves, including rejected
continuations in the compiler regressions; it does not implement an enclave.
--bounded-getsec checks the four SMX fallthrough leaves without requiring SGX.
--explicit-state checks the bounded register and six-arithmetic-flag APIs.
Controlled flag completions qualify transport, not enclave or SMX semantics.
Requires capstone, lief and unicorn. --llc tests both LLVM code modes;
--backend-llc tests NeverC IR through the full LLVM fork.
"""

import argparse
import importlib.util
import itertools
import json
import platform
import re
import signal
import struct
import subprocess
import sys
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
STOP, ORDINARY = COMMON.STOP, COMMON.ORDINARY
VALUES = ((4, 0, 0, 0), (0x8800000000000007, 0xd0123456789abcde, 0x798ab6cdaaaaffff, 0x80000000000000c0),
          (0xffffffffffffffff, 0x0102030405060708, 0x8000000180000001, 0xa0009))
DELTA = (0x1234, 0x8796, 0xc0ffee, 0xdeadbeef)
VARIANTS = ("all", "rax", "rbx", "rcx", "rdx", "flags", "dead", "twice", "memory", "condition", "realigned")
FRAME_VALUES = (0x12345678, 0x76543210, 0xa5)
REGISTERS = ("AX", "BX", "CX", "DX")
BOUNDED_LEAVES = (0, 1, 5, 6, 7, 8, 9, 10, 11)
SMX_LEAVES = (0, 6, 7, 8)
OPCODES = {"encls": b"\x0f\x01\xcf", "enclv": b"\x0f\x01\xc0",
           "enclu": b"\x0f\x01\xd7", "getsec": b"\x0f\x37"}


def leaf_cases(args):
    if args.bounded_getsec:
        return [("getsec", leaf) for leaf in SMX_LEAVES]
    if args.bounded_enclu:
        return [("enclu", leaf) for leaf in BOUNDED_LEAVES]
    return [("encls", None), ("enclv", None)]


def bounded_control(leaf, mode, index, standard=False):
    return leaf if mode == 32 or standard else (0x8800000000000000 ^ (0x0444000000000000 if index else 0)) | leaf


def observed(variant, flags=True):
    if variant in ("dead", "dead_standard"):
        return ()
    if variant in ("rax", "rbx", "rcx", "rdx", "flags"):
        return (dict(zip(("rax", "rbx", "rcx", "rdx", "flags"), range(5)))[variant],)
    return range(4 if variant == "standard" or not flags else 5)


def compile_case(args, directory, operation, variant, mode, opt, target=COMMON.TRIPLES[0], bounded_leaf=None):
    name = f"{operation}-{variant}-{mode}-{opt}-{COMMON.TRIPLES.index(target)}-{bounded_leaf}"
    ir, obj = directory / (name + ".ll"), directory / (name + ".o")
    word, count = f"i{mode}", 2 if variant == "twice" else 1
    smx = operation == "getsec"
    state = args.explicit_state
    has_flags = state or not smx
    flag_type = "i16" if state else "i8"
    feature = [] if smx else ["-mattr=+sgx"]
    if state:
        feature = ["-mattr=" + ("" if smx else "+sgx,") + "+sahf"]
    ty = "{ " + ", ".join([word] * 4 + ([flag_type] if has_flags else [])) + " }"
    api = "llvm.x86." + operation + (".bounded" if bounded_leaf is not None else "") + (".state" if state else "") + ("32" if mode == 32 else "")
    triple = ("x86_64" if mode == 64 else "i686") + "-" + target
    if args.llc:
        body = [f"store {word} %a, ptr %memory"] if variant == "memory" else []
        if state:
            body.append(f"%input_flags = trunc {word} %a to i16")
        terms = ["%a", "%b", "%c", "%d"]
        if variant == "realigned":
            body += [f"%fixed = alloca [16 x {word}], align 64",
                     f"%last = getelementptr {word}, ptr %fixed, i32 15",
                     f"store volatile {word} {FRAME_VALUES[0]}, ptr %fixed",
                     f"store volatile {word} {FRAME_VALUES[1]}, ptr %last",
                     f"%size = and {word} %a, 63",
                     f"%count = add {word} %size, 64",
                     f"%dynamic = alloca i8, {word} %count, align 32",
                     f"store volatile i8 {FRAME_VALUES[2]}, ptr %dynamic"]
        if variant == "condition":
            body += [f"%cmp = icmp ult {word} %a, %d", f"%condition = zext i1 %cmp to {word}"]
            terms.append("%condition")
        for index in range(count):
            parameters = []
            for parameter, delta in zip(("a", "b", "c", "d"), DELTA):
                value = "%" + parameter
                if index:
                    value += str(index)
                    body.append(f"{value} = xor {word} %{parameter}, {delta}")
                if parameter == "a" and bounded_leaf is not None:
                    value = str(bounded_control(bounded_leaf, mode, index))
                parameters.append(f"{word} {value}")
            if state:
                parameters.append("i16 %input_flags")
            body.append(f"%call{index} = call {ty} @{api}({', '.join(parameters)})")
            for field in observed(variant, has_flags):
                value = f"%v{index}_{field}"
                body.append(f"{value} = extractvalue {ty} %call{index}, {field}")
                if field == 4:
                    body.append(f"%flags{index} = zext {flag_type} {value} to {word}")
                    value = f"%flags{index}"
                terms.append(value)
        if variant == "memory":
            body.append(f"%loaded = load {word}, ptr %memory")
            terms.append("%loaded")
        if variant == "realigned":
            body += [f"%first_value = load volatile {word}, ptr %fixed",
                     f"%last_value = load volatile {word}, ptr %last",
                     "%dynamic_byte = load volatile i8, ptr %dynamic",
                     f"%dynamic_value = zext i8 %dynamic_byte to {word}"]
            terms += ["%first_value", "%last_value", "%dynamic_value"]
        value = terms[0]
        for index, term in enumerate(terms[1:]):
            body.append(f"%xor{index} = xor {word} {value}, {term}")
            value = f"%xor{index}"
        body.append(f"ret {word} {value}")
        frame_attribute = ' "no-stack-arg-probe"' if variant == "realigned" else ""
        ir.write_text(f"declare {ty} @{api}({', '.join([word] * 4 + (['i16'] if state else []))})\n"
                      f"define {word} @probe({word} %a, {word} %b, {word} %c, {word} %d, ptr %memory){frame_attribute} {{\n" +
                      "\n".join(body) + "\n}\n")
    else:
        src = directory / (name + ".c")
        body = ["*memory = a;"] if variant == "memory" else []
        terms = ["a", "b", "c", "d"]
        if variant == "realigned":
            body += ["volatile unsigned long long fixed[16] __attribute__((aligned(64)));",
                     f"fixed[0] = {FRAME_VALUES[0]}; fixed[15] = {FRAME_VALUES[1]};",
                     "volatile unsigned char *dynamic = __builtin_alloca((a & 63) + 64);",
                     f"*dynamic = {FRAME_VALUES[2]};"]
            terms += ["fixed[0]", "fixed[15]", "*dynamic"]
        check_evaluation = bounded_leaf is not None and variant in ("standard", "dead_standard")
        if check_evaluation:
            body.append("volatile unsigned evaluations = 0;")
            terms.append(f"(evaluations != {count})")
        if variant == "condition":
            body.append("unsigned condition = a < d;")
            terms.append("condition")
        for index in range(count):
            arguments = [p + (f" ^ {delta}ULL" if index else "") for p, delta in zip(("a", "b", "c", "d"), DELTA)]
            suffix = operation + ("_bounded" if bounded_leaf is not None else "")
            if bounded_leaf is not None:
                arguments[0] = str(bounded_control(bounded_leaf, mode, index)) + "ULL"
            if state:
                body += [f"unsigned long long out{index}[4];",
                         f"unsigned short flags{index};",
                         f"__builtin_ia32_{suffix}_state({', '.join(arguments)}, (unsigned short)a, out{index}, &flags{index});"]
            elif variant in ("standard", "dead_standard"):
                leaf = str(bounded_leaf) if bounded_leaf is not None else "(unsigned)a"
                data = f"(evaluations++, out{index})" if check_evaluation else f"out{index}"
                body += [f"__SIZE_TYPE__ out{index}[3] = {{b, c, d}};",
                         f"unsigned long long result{index} = _{suffix}_u32({leaf}, {data});"]
            else:
                body.append(f"unsigned long long out{index}[3];")
                flag_argument = ""
                if not smx:
                    body.append(f"unsigned char flags{index};")
                    flag_argument = f", &flags{index}"
                body.append(f"unsigned long long result{index} = __builtin_ia32_{suffix}({', '.join(arguments)}, out{index}{flag_argument});")
            for field in observed(variant, has_flags):
                terms.append(f"flags{index}" if field == 4 else
                             f"out{index}[{field}]" if state else
                             (f"result{index}" if field == 0 else f"out{index}[{field - 1}]"))
        if variant == "memory":
            terms.append("*memory")
        body.append("return " + " ^ ".join(terms) + ";")
        features = ([] if smx else ["sgx"]) + (["sahf"] if state else [])
        attribute = '__attribute__((target("' + ",".join(features) + '")))\n' if features else ""
        src.write_text('#include <immintrin.h>\n' + attribute +
                       "unsigned long long probe(unsigned long long a, unsigned long long b, unsigned long long c, unsigned long long d, unsigned long long *memory) {\n" +
                       "\n".join(body) + "\n}\n")
        compiler = [args.compiler, "--target=" + triple, "-" + opt, "-Werror", "-ffreestanding", "-fno-builtin-mimalloc",
                    "-fno-lto", "-fno-stack-protector", "-mllvm", "-verify-machineinstrs", src]
        if variant == "realigned":
            compiler.append("-mno-stack-arg-probe")
        run(compiler + ["-S", "-emit-llvm", "-o", ir])
        assert " asm " not in ir.read_text() and f"@{api}(" in ir.read_text()
        if not args.backend_llc:
            run(compiler + ["-c", "-o", obj])
    backend = args.llc or args.backend_llc
    if backend:
        if args.llc and opt == "O2":
            optimized = ir.with_suffix(".optimized.ll")
            run([Path(backend).parent / "opt", "-S", "-passes=default<O2>", ir, "-o", optimized])
            ir = optimized
        backend_command = [backend, "-mtriple=" + triple, *feature, "-" + opt, "-verify-machineinstrs", ir]
        run(backend_command + ["-filetype=obj", "-o", obj])
    if variant in ("all", "standard"):
        for dialect in ("att", "intel"):
            asm = directory / (name + "-" + dialect + ".s")
            assembled = asm.with_suffix(".o")
            if backend:
                run(backend_command + ["-filetype=asm", "-x86-asm-syntax=" + dialect, "-o", asm])
                run([Path(backend).parent / "llvm-mc", "-triple=" + triple, *feature, "-filetype=obj", asm, "-o", assembled])
            else:
                run(compiler + ["-S", "-masm=" + dialect, "-o", asm])
                run([args.compiler, "--target=" + triple, *([] if smx else ["-msgx"]), "-c", asm, "-o", assembled])
            assert text_section(obj) == text_section(assembled), "assembly round trip changed bytes"
    return text_section(obj)


def execute(code, operation, variant, mode, values, statuses, stop_at_boundary=False, bounded_leaf=None, explicit_state=False, windows=True):
    mask = (1 << mode) - 1
    inputs = tuple(value & mask for value in values)
    machine = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_64 if mode == 64 else unicorn.UC_MODE_32)
    reg = lambda name: getattr(regs, "UC_X86_REG_" + ("R" if mode == 64 else "E") + name)
    pack = lambda value: struct.pack("<Q" if mode == 64 else "<I", value & mask)
    for base in (CODE, STACK, DATA):
        machine.mem_map(base, PAGE)
    machine.mem_write(CODE, code)
    machine.mem_write(ORDINARY, pack(COMMON.SENTINEL))
    sp = STACK + PAGE - 64 - mode // 8
    machine.reg_write(reg("SP"), sp)
    machine.mem_write(sp, pack(STOP))
    if mode == 64:
        for r, value in zip(("CX", "DX", "8", "9") if windows else ("DI", "SI", "DX", "CX"), inputs):
            machine.reg_write(reg(r), value)
        if windows:
            machine.mem_write(sp + 40, pack(ORDINARY))
        else:
            machine.reg_write(reg("8"), ORDINARY)
    else:
        machine.mem_write(sp + 4, b"".join(pack(v) for v in (*inputs, ORDINARY)))
    saved = {reg(name): (0xa1b2c3d4e5f60710 + index) & mask for index, name in
             enumerate(("BX", "BP", *(("SI", "DI") if windows or mode == 32 else ()), *(("12", "13", "14", "15") if mode == 64 else ())))}
    for r, value in saved.items():
        machine.reg_write(r, value)
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64 if mode == 64 else capstone.CS_MODE_32)
    accesses = [instruction for instruction in decoder.disasm(code, CODE) if instruction.mnemonic == operation]
    assert len(accesses) == len(statuses), "native operations were lost or merged"
    result, start = inputs[0] ^ inputs[1] ^ inputs[2] ^ inputs[3], CODE
    if variant == "condition":
        result ^= int(inputs[0] < inputs[3])
    if variant == "realigned":
        result ^= FRAME_VALUES[0] ^ FRAME_VALUES[1] ^ FRAME_VALUES[2]
    for index, (instruction, status) in enumerate(zip(accesses, statuses)):
        assert bytes(instruction.bytes) == OPCODES[operation]
        machine.emu_start(start, instruction.address, count=1000)
        assert machine.reg_read(reg("IP")) == instruction.address
        for field, register in enumerate(REGISTERS):
            expected = inputs[field] ^ (DELTA[field] if index else 0)
            if bounded_leaf is not None and field == 0:
                expected = bounded_control(bounded_leaf, mode, index, variant in ("standard", "dead_standard"))
            if variant in ("standard", "dead_standard") and field == 0:
                expected &= 0xffffffff
            assert machine.reg_read(reg(register)) == expected, f"incorrect {operation} {register} input"
        if explicit_state:
            flags = machine.reg_read(regs.UC_X86_REG_EFLAGS)
            packed = ((flags & 0xd5) << 8) | ((flags >> 11) & 1)
            assert packed == (inputs[0] & 0xd501), "incoming arithmetic flags changed"
        if stop_at_boundary:
            return  # No implicit enclave-address preload occurred before the native boundary.
        outputs = [((0x9182736455aa0123 + field * 0x17659843) ^ (inputs[field] >> 2) ^ (0xfedcba98 if index else 0)) & mask
                   for field in range(4)]
        for register, value in zip(REGISTERS, outputs):
            machine.reg_write(reg(register), value)
        # Defined-status leaves clear the remaining arithmetic flags. The
        # four CF/ZF combinations are diagnostic transport injections.
        if explicit_state:
            flags = machine.reg_read(regs.UC_X86_REG_EFLAGS)
            machine.reg_write(regs.UC_X86_REG_EFLAGS,
                              (flags & ~0x8d5) | ((status >> 8) & 0xd5) | ((status & 1) << 11))
        elif operation != "getsec":
            flags = machine.reg_read(regs.UC_X86_REG_EFLAGS)
            machine.reg_write(regs.UC_X86_REG_EFLAGS, (flags & ~0x8d5) | (status & 1) | ((status >> 1) << 6))
        if variant == "standard":
            outputs[0] &= 0xffffffff
        for field in observed(variant, explicit_state or operation != "getsec"):
            result ^= status if field == 4 else outputs[field]
        if variant == "memory":
            assert bytes(machine.mem_read(ORDINARY, mode // 8)) == pack(inputs[0])
            machine.mem_write(ORDINARY, pack(COMMON.SENTINEL))
            result ^= COMMON.SENTINEL & mask
        start = instruction.address + instruction.size
    machine.emu_start(start, STOP, count=1000)
    assert machine.reg_read(reg("IP")) == STOP and machine.reg_read(reg("AX")) == result, "native result/flags/live-input transport changed"
    assert all(machine.reg_read(r) == value for r, value in saved.items()), "callee-saved register corrupted"


def native_state_oracle(args, directory):
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        return 0
    fixtures = Path(__file__).with_name("fixtures") / "bounded-leaf-state"
    driver = directory / "bounded-state-host"
    run([args.host_cc, "-O2", "-Wall", "-Wextra", "-Werror",
         fixtures / "host.c", "-ldl", "-o", driver])
    count = 0
    observations = []
    for operation, leaf in leaf_cases(args):
        smx = operation == "getsec"
        features = ("" if smx else "+sgx,") + "+sahf"
        reference = directory / f"reference-{operation}.o"
        run([args.host_cc, "-fPIC", "-c", fixtures / "reference.S",
             *([] if smx else ["-DNEVERC_TEST_ENCLU"]), "-o", reference])
        control = bounded_control(leaf, 64, 0)
        for opt in ("O0", "O2"):
            base = directory / f"native-state-{operation}-{leaf}-{opt}"
            src, ir, obj, library = (base.with_suffix(suffix)
                                     for suffix in (".c", ".ll", ".o", ".so"))
            if args.llc:
                result_type = "{ i64, i64, i64, i64, i16 }"
                api = f"llvm.x86.{operation}.bounded.state"
                body = []
                for field in range(1, 5):
                    body += [f"%in{field} = getelementptr i64, ptr %input, i64 {field}",
                             f"%v{field} = load i64, ptr %in{field}"]
                body += ["%flags = trunc i64 %v4 to i16",
                         f"%call = call {result_type} @{api}(i64 {control}, i64 %v1, i64 %v2, i64 %v3, i16 %flags)"]
                for field in range(5):
                    body.append(f"%r{field} = extractvalue {result_type} %call, {field}")
                    value = f"%r{field}"
                    if field == 4:
                        body.append("%outflags = zext i16 %r4 to i64")
                        value = "%outflags"
                    body += [f"%out{field} = getelementptr i64, ptr %output, i64 {field}",
                             f"store i64 {value}, ptr %out{field}"]
                ir.write_text(f"declare {result_type} @{api}(i64, i64, i64, i64, i16)\n"
                              "define void @probe(ptr %input, ptr %output) {\n" +
                              "\n".join(body) + "\nret void\n}\n")
            else:
                attributes = ("" if smx else "sgx,") + "sahf"
                src.write_text(f'''__attribute__((target("{attributes}")))
void probe(const unsigned long long *input, unsigned long long *output) {{
  unsigned short flags;
  __builtin_ia32_{operation}_bounded_state({control}ULL, input[1], input[2],
                                          input[3], (unsigned short)input[4],
                                          output, &flags);
  output[4] = flags;
}}
''')
                compiler = [args.compiler, "--target=x86_64-unknown-linux-gnu", "-" + opt,
                            "-Werror", "-ffreestanding", "-fno-lto", "-fno-builtin-mimalloc",
                            "-fPIC", "-mllvm", "-verify-machineinstrs", src]
                run(compiler + (["-S", "-emit-llvm", "-o", ir] if args.backend_llc
                                else ["-c", "-o", obj]))
            backend = args.llc or args.backend_llc
            if backend:
                run([backend, "-mtriple=x86_64-unknown-linux-gnu", "-mattr=" + features,
                     "-" + opt, "-verify-machineinstrs", "-relocation-model=pic",
                     "-filetype=obj", ir, "-o", obj])
            run([args.host_cc, "-shared", "-nostdlib", obj, reference, "-o", library])
            result = run([driver, library.resolve(), operation, str(control)])
            match = re.fullmatch(
                r"384 native boundaries: (\d+) faults, (\d+) normal completions\s*",
                result)
            assert match and int(match[1]) + int(match[2]) == 384, result
            observations.append({"operation": operation, "leaf": leaf,
                                 "optimization": opt, "faults": int(match[1]),
                                 "normal_completions": int(match[2])})
            count += 384
    (directory / "native-observations.json").write_text(
        json.dumps(observations, indent=2) + "\n")
    return count


def native_fault_oracle(args, directory):
    if args.explicit_state:
        return native_state_oracle(args, directory)
    if args.llc or platform.system() != "Linux" or platform.machine() != "x86_64":
        return 0
    count = 0
    for (operation, leaf), opt in itertools.product(leaf_cases(args), ("O0", "O2")):
        leaf = 4 if leaf is None else leaf
        smx = operation == "getsec"
        base = directory / f"fault-{operation}-{leaf}-{opt}"
        src, ir, obj, library = (base.with_suffix(suffix) for suffix in (".c", ".ll", ".o", ".so"))
        encoded = ", ".join(f"0x{byte:02x}" for byte in OPCODES[operation])
        suffix = operation + ("_bounded" if args.bounded_enclu or smx else "")
        attribute = "" if smx else '__attribute__((target("sgx"))) '
        flags = "" if smx else ", &flags"
        flag_declaration = "" if smx else "unsigned char flags;"
        if args.explicit_state:
            attribute = '__attribute__((target("' + ("sgx," if not smx else "") + 'sahf"))) '
            probe = f'''unsigned long long registers[4]; unsigned short flags;
  __builtin_ia32_{suffix}_state({leaf}, 0, 0, 0, 0xd501, registers, &flags);
  outputs[0]=registers[1]; outputs[1]=registers[2]; outputs[2]=registers[3];
  return registers[0];'''
        else:
            probe = f'''{flag_declaration}
  return __builtin_ia32_{suffix}({leaf}, 0, 0, 0, outputs{flags});'''
        src.write_text(f'''{attribute}unsigned long long probe(unsigned long long *outputs) {{
  {probe}
}}
unsigned long long original(unsigned long long *outputs) {{
  unsigned long long a={leaf},b=0,c=0,d=0;
  __asm__ volatile(".byte {encoded}" : "+a"(a), "+b"(b), "+c"(c), "+d"(d) : : "cc", "memory");
  outputs[0]=b; outputs[1]=c; outputs[2]=d;
  return a;
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
        outcomes = []
        for function in ("original", "probe"):
            child = subprocess.run([sys.executable, "-c",
                                    "import ctypes,json,signal,sys; "
                                    "assert ctypes.CDLL(None).prctl(4,0,0,0,0)==0; signal.alarm(5); "
                                    "f=getattr(ctypes.CDLL(sys.argv[1]),sys.argv[2]); "
                                    "f.argtypes=[ctypes.POINTER(ctypes.c_uint64)]; f.restype=ctypes.c_uint64; "
                                    "out=(ctypes.c_uint64*3)(); result=f(out); print(json.dumps([result,*out]))",
                                    str(library.resolve()), function], capture_output=True, text=True)
            expected = (-signal.SIGILL, -signal.SIGSEGV, -signal.SIGBUS) if args.bounded_enclu or smx else (-signal.SIGILL,)
            if smx:
                expected = (0, *expected)  # Queries may succeed when CR4.SMXE is already enabled.
            assert child.returncode in expected, f"{operation}/{opt}/{function}: unexpected native outcome {child.returncode}: {child.stderr}"
            outcomes.append((child.returncode, child.stdout if child.returncode == 0 else ""))
            count += 1
        assert outcomes[0] == outcomes[1], "native intrinsic and original bytes disagree on completion"
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=Path("build-neverc/bin/neverc"))
    backend = parser.add_mutually_exclusive_group()
    backend.add_argument("--llc", type=Path)
    backend.add_argument("--backend-llc", type=Path)
    parser.add_argument("--keep", type=Path)
    parser.add_argument("--host-cc", default="cc")
    bounded = parser.add_mutually_exclusive_group()
    bounded.add_argument("--bounded-enclu", action="store_true")
    bounded.add_argument("--bounded-getsec", action="store_true")
    parser.add_argument("--explicit-state", action="store_true")
    args = parser.parse_args()
    if args.explicit_state and not (args.bounded_enclu or args.bounded_getsec):
        parser.error("--explicit-state requires a bounded leaf family")
    for key in ("compiler", "llc", "backend_llc"):
        if getattr(args, key):
            setattr(args, key, getattr(args, key).resolve())
    count = 0
    with tempfile.TemporaryDirectory(prefix="sgx-supervisor-") as temporary:
        directory = args.keep or Path(temporary)
        directory.mkdir(parents=True, exist_ok=True)
        variants = tuple(v for v in VARIANTS if args.explicit_state or not args.bounded_getsec or v != "flags")
        if not args.llc and not args.bounded_getsec and not args.explicit_state:
            variants += ("standard", "dead_standard")
        cases = leaf_cases(args)
        for mode in ((32, 64) if args.llc else (64,)):
            for (operation, leaf), opt, variant in itertools.product(cases, ("O0", "O2"), variants):
                code = compile_case(args, directory, operation, variant, mode, opt, bounded_leaf=leaf)
                samples = itertools.product(VALUES, itertools.product((0,) if args.bounded_getsec else range(4), repeat=2 if variant == "twice" else 1))
                if args.explicit_state:
                    pack_flags = lambda value: sum(((value >> i) & 1) << bit
                                                   for i, bit in enumerate((8, 10, 12, 14, 15, 0)))
                    samples = []
                    for flag in range(64):
                        values = list(VALUES[flag % len(VALUES)])
                        # Reserved status bits must be discarded on entry.
                        values[0] = (values[0] & ~0xffff) | pack_flags(flag) | (0x2afe if flag & 1 else 0)
                        for delta in (0, 1, 0x15, 0x3f):
                            statuses = (pack_flags(flag ^ delta),)
                            if variant == "twice":
                                statuses += (pack_flags((flag + delta + 7) & 63),)
                            samples.append((values, statuses))
                for values, statuses in samples:
                    try:
                        execute(code, operation, variant, mode, values, statuses, bounded_leaf=leaf, explicit_state=args.explicit_state)
                    except Exception as error:
                        raise AssertionError(f"{operation}/{leaf}, {variant}, {mode}, {opt}, {values}, {statuses}: {error}") from error
                    count += 1
                execute(code, operation, variant, mode, (4, 0xdead0001, 0xdead0003, 0xdead0005), (0,) * (2 if variant == "twice" else 1), True, leaf, args.explicit_state)
                count += 1
                if variant in ("all", "standard", "realigned"):
                    for target in COMMON.TRIPLES[1:]:
                        code = compile_case(args, directory, operation, variant, mode, opt, target, leaf)
                        if variant == "realigned":
                            for values in VALUES:
                                execute(code, operation, variant, mode, values, (0,),
                                        bounded_leaf=leaf, explicit_state=args.explicit_state,
                                        windows=False)
                                count += 1
                        count += 1
        native = native_fault_oracle(args, directory)
    family = "bounded GETSEC" if args.bounded_getsec else ("bounded ENCLU" if args.bounded_enclu else "SGX supervisor")
    if args.explicit_state:
        family += " explicit state"
    print(f"PASS: {count} {family} encoding/completion checks; {native} real CPL3 boundaries")


if __name__ == "__main__":
    main()

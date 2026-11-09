"""Stop before independently decoded SKINIT; never emulate or execute launch."""

import argparse, hashlib, json, subprocess
from pathlib import Path
import capstone, lief, unicorn
from unicorn import x86_const as R

VALUES = (0, 1, 0xFFFF, 0x10000, 0x4000, 0x7FFFFFFF, 0x80000000, 0x8765CDEF, 0xFFFFFFFF)
TRIPLES = (
    "i386-unknown-linux-gnu",
    "x86_64-unknown-linux-gnu",
    "i686-pc-windows-msvc",
    "x86_64-pc-windows-msvc",
    "i386-apple-darwin",
    "x86_64-apple-darwin",
)
CODE = 0x100000
STACK = 0x700000
PAGE = 4096


def execute(path, triple, names):
    binary = lief.parse(str(path))
    sections = list(binary.sections)
    text_sections = [
        (n, s)
        for n, s in enumerate(sections)
        if s.name in (".text", "__text") and len(s.content)
    ]
    assert text_sections
    code = bytearray()
    section_offsets = {}
    for n, section in text_sections:
        code.extend(b"\x90" * ((-len(code)) % 16))
        section_offsets[n] = len(code)
        code.extend(bytes(section.content))
        if not isinstance(binary, lief.ELF.Binary):
            assert not list(section.relocations), (path, "text relocation")
    code = bytes(code)
    text = text_sections[0][1]
    if isinstance(binary, lief.ELF.Binary):
        assert not any(
            r.has_section and r.section.name == text.name for r in binary.relocations
        ), (path, "text relocation")
    mode64 = triple.startswith("x86_64")
    decoder = capstone.Cs(
        capstone.CS_ARCH_X86, capstone.CS_MODE_64 if mode64 else capstone.CS_MODE_32
    )
    decoded = list(decoder.disasm(code, 0))
    assert sum(i.size for i in decoded) == len(code), (path, "incomplete decode")
    native = {i.address for i in decoded if i.mnemonic == "skinit"}
    assert all(code[at : at + 3] == b"\x0f\x01\xde" for at in native)
    assert len(native) == len(names), (path, len(native), len(names))
    assert not any(i.mnemonic.startswith(("call", "ret")) for i in decoded), (
        path,
        "old continuation/helper",
    )
    symbols = {}
    for symbol in binary.symbols:
        name = symbol.name.lstrip("_")
        if name not in names:
            continue
        if isinstance(binary, lief.COFF.Binary):
            offset = section_offsets[symbol.section_idx - 1] + symbol.value
        else:
            assert len(text_sections) == 1, (path, "unsupported split non-COFF text")
            offset = symbol.value - text.virtual_address
        symbols[name] = offset
    assert set(symbols) == set(names), (path, symbols)
    offsets = sorted(symbols.values()) + [len(code)]
    patched = bytearray(code)
    for at in native:
        patched[at : at + 3] = b"\x90" * 3
    machine = unicorn.Uc(
        unicorn.UC_ARCH_X86, unicorn.UC_MODE_64 if mode64 else unicorn.UC_MODE_32
    )
    machine.mem_map(CODE, ((len(code) + PAGE - 1) // PAGE) * PAGE)
    machine.mem_write(CODE, bytes(patched))
    machine.mem_map(STACK, 16 * PAGE)
    sp = STACK + 14 * PAGE + (8 if mode64 else 12)
    pc = R.UC_X86_REG_RIP if mode64 else R.UC_X86_REG_EIP
    stack = R.UC_X86_REG_RSP if mode64 else R.UC_X86_REG_ESP
    registers = (
        (
            R.UC_X86_REG_RAX,
            R.UC_X86_REG_RBX,
            R.UC_X86_REG_RCX,
            R.UC_X86_REG_RDX,
            R.UC_X86_REG_RSI,
            R.UC_X86_REG_RDI,
            R.UC_X86_REG_RBP,
        )
        if mode64
        else (
            R.UC_X86_REG_EAX,
            R.UC_X86_REG_EBX,
            R.UC_X86_REG_ECX,
            R.UC_X86_REG_EDX,
            R.UC_X86_REG_ESI,
            R.UC_X86_REG_EDI,
            R.UC_X86_REG_EBP,
        )
    )
    seen = []

    def boundary(uc, address, size, unused):
        if address - CODE in native:
            seen.append(address - CODE)
            uc.emu_stop()

    machine.hook_add(unicorn.UC_HOOK_CODE, boundary)
    checks = 0
    for name, constant in names.items():
        begin = symbols[name]
        end = next(at for at in offsets if at > begin)
        expected_native = [at for at in native if begin <= at < end]
        assert len(expected_native) == 1, (path, name, expected_native)
        for incoming in VALUES:
            for n, reg in enumerate(registers):
                machine.reg_write(
                    reg,
                    (0x1234567887654321 + n * 0x11111111)
                    & ((1 << (64 if mode64 else 32)) - 1),
                )
            machine.reg_write(stack, sp)
            machine.reg_write(R.UC_X86_REG_EFLAGS, 0x246)
            if mode64:
                machine.reg_write(
                    R.UC_X86_REG_RCX if "windows" in triple else R.UC_X86_REG_RDI,
                    0xA5A5A5A500000000 | incoming,
                )
            else:
                machine.mem_write(sp + 4, incoming.to_bytes(4, "little"))
            seen.clear()
            machine.emu_start(CODE + begin, CODE + end, count=1000)
            expected = incoming if constant is None else constant
            assert (
                seen == expected_native
                and machine.reg_read(pc) == CODE + expected_native[0]
            ), (path, name, seen)
            assert machine.reg_read(R.UC_X86_REG_EAX) == expected, (
                path,
                name,
                incoming,
                hex(machine.reg_read(R.UC_X86_REG_EAX)),
                hex(expected),
            )
            checks += 1
    return checks


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--llc", required=True, type=Path)
    p.add_argument("--opt", required=True, type=Path)
    p.add_argument("--neverc", type=Path)
    p.add_argument("--keep", required=True, type=Path)
    p.add_argument("--only-neverc", action="store_true")
    a = p.parse_args()
    a.keep.mkdir(parents=True, exist_ok=True)
    commands = []
    receipts = []

    def run(args, stem):
        args = list(map(str, args))
        r = subprocess.run(args, capture_output=True, text=True)
        (a.keep / (stem + ".log")).write_text(r.stdout + r.stderr)
        commands.append({"argv": args, "status": r.returncode})
        (a.keep / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        assert r.returncode == 0, (args, r.stdout, r.stderr)

    ir = "declare void @llvm.x86.skinit(i32)\n"
    names = {"probe_dyn": None}
    ir += "define void @probe_dyn(i32 %physical) { call void @llvm.x86.skinit(i32 %physical)\n unreachable }\n"
    for n, v in enumerate(VALUES):
        name = f"probe_constant_{n}"
        names[name] = v
        ir += f"define void @{name}() {{ call void @llvm.x86.skinit(i32 {v})\n unreachable }}\n"
    source = a.keep / "launch.ll"
    source.write_text(ir)
    for triple in () if a.only_neverc else TRIPLES:
        for level in (0, 2):
            stem = f"llvm-{triple}-O{level}"
            inp = source
            if level == 2:
                inp = a.keep / (stem + ".ll")
                run(
                    [a.opt, "-S", "-passes=default<O2>", source, "-o", inp],
                    stem + "-opt",
                )
            obj = a.keep / (stem + ".o")
            run(
                [
                    a.llc,
                    f"-mtriple={triple}",
                    f"-O{level}",
                    "-filetype=obj",
                    "-verify-machineinstrs",
                    inp,
                    "-o",
                    obj,
                ],
                stem + "-build",
            )
            checks = execute(obj, triple, names)
            receipts.append(
                {
                    "object": str(obj),
                    "sha256": hashlib.sha256(obj.read_bytes()).hexdigest(),
                    "checks": checks,
                }
            )
    if a.neverc:
        for triple in TRIPLES:
            if not triple.startswith("x86_64"):
                continue
            c = "void __svm_skinit(int);\n"
            native_names = {}
            for api in ("portable", "microsoft"):
                call = "__builtin_ia32_skinit" if api == "portable" else "__svm_skinit"
                name = f"probe_{api}_dyn"
                native_names[name] = None
                c += f"[[noreturn]] void {name}(unsigned physical) {{ {call}(physical); }}\n"
                for n, v in enumerate(VALUES):
                    name = f"probe_{api}_constant_{n}"
                    native_names[name] = v
                    c += f"[[noreturn]] void {name}(void) {{ {call}({v}U); }}\n"
            src = a.keep / ("native-" + triple + ".c")
            src.write_text(c)
            for level in (0, 2):
                stem = f"neverc-{triple}-O{level}"
                obj = a.keep / (stem + ".o")
                run(
                    [
                        a.neverc,
                        f"--target={triple}",
                        f"-O{level}",
                        "-std=c23",
                        "-fms-extensions",
                        "-ffreestanding",
                        "-fno-lto",
                        "-fno-builtin-mimalloc",
                        "-mllvm",
                        "-verify-machineinstrs",
                        "-c",
                        src,
                        "-o",
                        obj,
                    ],
                    stem + "-build",
                )
                receipts.append(
                    {
                        "object": str(obj),
                        "sha256": hashlib.sha256(obj.read_bytes()).hexdigest(),
                        "checks": execute(obj, triple, native_names),
                    }
                )
    report = {
        "objects": receipts,
        "total_checks": sum(r["checks"] for r in receipts),
        "actual_skinit_execution": False,
        "successful_launch_qualified": False,
        "oracle": "Capstone byte boundaries, exact 0f01de opcode, Unicorn runs only preceding ordinary instructions; launch bytes replaced with NOPs and stopped at their first byte.",
    }
    (a.keep / "qualification.json").write_text(json.dumps(report, indent=2) + "\n")
    print(
        len(receipts),
        "objects;",
        report["total_checks"],
        "raw EAX boundary checks",
        flush=True,
    )


if __name__ == "__main__":
    main()

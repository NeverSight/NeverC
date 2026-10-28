#!/usr/bin/env python3
"""Maintain native system instruction register and memory effects.

The full LLVM fork owns these in X86InstrSystem.td. NeverC's pruned table
retains its opcode numbers and encodings; this generator owns the matching
implicit register sequences, register definitions and memory effects.
"""

import argparse
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TABLE = ROOT / "llvm/lib/Target/X86/X86GenInstrInfo.inc"
# Explicit operand count, implicit uses/definitions, memory effects.
EFFECTS = {
    "CLTS": (0, (), (), ()),
    "FNOP": (0, ("FPCW", "FPSW"), ("FPSW",),
             ("MayLoad", "MayStore", "MayRaiseFPException")),
    "FNSTCW16m": (5, ("FPCW",), ("FPSW",), ("MayStore",)),
    "FLDCW16m": (5, ("FPCW", "FPSW"), ("FPSW", "FPCW"),
                ("MayLoad", "MayRaiseFPException")),
    "VMLOAD64": (0, ("RAX",), (), ("MayLoad",)),
    "VMSAVE64": (0, ("RAX",), (), ("MayStore",)),
    "INVLPGA64": (0, ("RAX", "ECX"), (), ()),
    "VMFUNC": (0, ("EAX", "ECX"), (), ("MayLoad",)),
    "STGI": (0, (), (), ()),
    "CLGI": (0, (), (), ()),
    "SWAPGS": (0, (), (), ()),
    # AC is ambient state, distinct from arithmetic EFLAGS used by SETcc/Jcc.
    "CLAC": (0, (), (), ()),
    "STAC": (0, (), (), ()),
    "HLT": (0, (), (), ("MayLoad", "MayStore")),
    "SKINIT": (0, ("EAX",), (), ("MayLoad", "MayStore")),
    "HRESET": (1, ("EAX",), (), ()),
    "INVLPGB64": (0, ("RAX", "EDX", "ECX"), (), ()),
    "TLBSYNC": (0, (), (), ()),
    "LKGS16r": (1, (), (), ("MayLoad",)),
    "LKGS16m": (5, (), (), ("MayLoad",)),
    "WRMSRNS": (0, ("EAX", "ECX", "EDX"), (), ()),
    "RDMSRLIST": (0, ("RSI", "RDI", "RCX"), ("RCX",), ("MayLoad", "MayStore")),
    "WRMSRLIST": (0, ("RSI", "RDI", "RCX"), ("RCX",), ("MayLoad",)),
    # IF/VIF is ambient state. CLI/STI preserve the arithmetic flags represented
    # by the compiler's EFLAGS register, so they do not use or define that value.
    "CLI": (0, (), (), ()),
    "STI": (0, (), (), ()),
    "INVLPG": (5, (), (), ()),
    "SGDT64m": (5, (), (), ("MayStore",)),
    "SIDT64m": (5, (), (), ("MayStore",)),
    "LGDT64m": (5, (), (), ("MayLoad",)),
    "LIDT64m": (5, (), (), ("MayLoad",)),
    "CPUID": (0, ("EAX", "ECX"), ("EAX", "EBX", "ECX", "EDX"), ()),
    "IN8rr": (0, ("DX",), ("AL",), ()),
    "IN16rr": (0, ("DX",), ("AX",), ()),
    "IN32rr": (0, ("DX",), ("EAX",), ()),
    "OUT8rr": (0, ("DX", "AL"), (), ()),
    "OUT16rr": (0, ("DX", "AX"), (), ()),
    "OUT32rr": (0, ("DX", "EAX"), (), ()),
    # Intel defines the arithmetic flags as undefined after CR/DR MOVs.
    "MOV64rc": (2, (), ("EFLAGS",), ()),
    "MOV64cr": (2, (), ("EFLAGS",), ()),
    "MOV64rd": (2, (), ("EFLAGS",), ()),
    "MOV64dr": (2, (), ("EFLAGS",), ()),
    "REP_MOVSB_64": (0, ('RCX', 'RDI', 'RSI', 'DF'), ('RCX', 'RDI', 'RSI'), ('MayLoad', 'MayStore')),
    "REP_MOVSW_64": (0, ('RCX', 'RDI', 'RSI', 'DF'), ('RCX', 'RDI', 'RSI'), ('MayLoad', 'MayStore')),
    "REP_MOVSD_64": (0, ('RCX', 'RDI', 'RSI', 'DF'), ('RCX', 'RDI', 'RSI'), ('MayLoad', 'MayStore')),
    "REP_MOVSQ_64": (0, ('RCX', 'RDI', 'RSI', 'DF'), ('RCX', 'RDI', 'RSI'), ('MayLoad', 'MayStore')),
    "REP_STOSB_64": (0, ('AL', 'RCX', 'RDI', 'DF'), ('RCX', 'RDI'), ('MayStore',)),
    "REP_STOSW_64": (0, ('AX', 'RCX', 'RDI', 'DF'), ('RCX', 'RDI'), ('MayStore',)),
    "REP_STOSD_64": (0, ('EAX', 'RCX', 'RDI', 'DF'), ('RCX', 'RDI'), ('MayStore',)),
    "REP_STOSQ_64": (0, ('RAX', 'RCX', 'RDI', 'DF'), ('RCX', 'RDI'), ('MayStore',)),
    "REP_INSB_64": (0, ('DX', 'RCX', 'RDI', 'DF'), ('RCX', 'RDI'), ('MayStore',)),
    "REP_INSW_64": (0, ('DX', 'RCX', 'RDI', 'DF'), ('RCX', 'RDI'), ('MayStore',)),
    "REP_INSL_64": (0, ('DX', 'RCX', 'RDI', 'DF'), ('RCX', 'RDI'), ('MayStore',)),
    "REP_OUTSB_64": (0, ('DX', 'RCX', 'RSI', 'DF'), ('RCX', 'RSI'), ('MayLoad',)),
    "REP_OUTSW_64": (0, ('DX', 'RCX', 'RSI', 'DF'), ('RCX', 'RSI'), ('MayLoad',)),
    "REP_OUTSL_64": (0, ('DX', 'RCX', 'RSI', 'DF'), ('RCX', 'RSI'), ('MayLoad',)),

}

REGISTER_RESULTS = {f"{operation}{bits}r"
                    for operation in ("SLDT", "STR", "SMSW")
                    for bits in (16, 32, 64)}
for name in REGISTER_RESULTS:
    EFFECTS[name] = (1, (), (), ())
for operation in ("SLDT", "STR", "SMSW"):
    name = operation + ("m" if operation == "STR" else "16m")
    EFFECTS[name] = (5, (), (), ("MayStore",))
for operation in ("VERR", "VERW", "LLDT", "LTR", "LMSW"):
    for form in ("r", "m"):
        name = operation + ("16" if operation in ("LLDT", "LMSW") else "") + form
        # LTR reads and atomically sets the descriptor's busy bit even when
        # its selector comes from a register. LLDT and VERIFY read the GDT/LDT.
        memory = (("MayLoad",) if form == "m" or operation != "LMSW" else ())
        if operation == "LTR":
            memory += ("MayStore",)
        definitions = ("EFLAGS",) if operation in ("VERR", "VERW") else ()
        EFFECTS[name] = (5 if form == "m" else 1, (), definitions, memory)
EFFECTS["INVD"] = (0, (), (), ("MayLoad", "MayStore"))
EFFECTS["GETSEC"] = (0, ("RAX", "RBX", "RCX", "RDX"),
                       ("RAX", "RBX", "RCX", "RDX"),
                       ("MayLoad", "MayStore"))
for operation in ("ENCLS", "ENCLV", "ENCLU"):
    EFFECTS[operation] = (0, ("RAX", "RBX", "RCX", "RDX"),
                         ("RAX", "RBX", "RCX", "RDX", "EFLAGS"),
                         ("MayLoad", "MayStore"))


def implicit_sequence(text, sequence):
    start = text.index("    /* 0 */ X86::EFLAGS,")
    end = text.index("\n  }\n};", start)
    registers = re.findall(r"X86::(\w+)", text[start:end])
    declared = int(re.search(r"MCPhysReg ImplicitOps\[(\d+)\];", text).group(1))
    if len(registers) != declared:
        raise ValueError("implicit-register table length disagrees with its declaration")
    for index in range(len(registers) - len(sequence) + 1):
        if tuple(registers[index:index + len(sequence)]) == sequence:
            return text, index
    addition = f"\n    /* {declared} */ " + ", ".join("X86::" + r for r in sequence) + ","
    text = text[:end] + addition + text[end:]
    text = re.sub(r"MCPhysReg ImplicitOps\[\d+\];",
                  f"MCPhysReg ImplicitOps[{declared + len(sequence)}];", text, count=1)
    # Appending can create an earlier match across the old/new boundary.
    # Resolve that canonical offset now, so a second run produces no changes.
    return implicit_sequence(text, sequence)


def synchronize(text):
    for name, (explicit_operands, uses, definitions, memory_effects) in EFFECTS.items():
        text, offset = implicit_sequence(text, uses + definitions)
        pattern = re.compile(
            r"^(\s*\{ )([^\n]+?)( \},  // Inst #\d+ = " + name + r")$", re.MULTILINE)
        match = pattern.search(text)
        if not match:
            raise ValueError(f"missing instruction descriptor: {name}")
        fields = match.group(2).split(",\t")
        explicit_defs = int(name in REGISTER_RESULTS or
                            name in ("MOV64rc", "MOV64cr", "MOV64rd", "MOV64dr"))
        if len(fields) != 10 or fields[1:3] != [str(explicit_operands), str(explicit_defs)]:
            raise ValueError(f"unexpected explicit operands: {name}")
        fields[5:8] = [str(len(uses)), str(len(definitions)), f"X86ImpOpBase + {offset}"]
        flags, encoding = fields[9].split(", ", 1)
        if name == "HLT":
            # A returning interrupt resumes after HLT.
            flags = flags.replace("|(1ULL<<MCID::Terminator)", "")
        if name == "SKINIT":
            for flag in ("Terminator", "Barrier"):
                marker = f"|(1ULL<<MCID::{flag})"
                if marker not in flags:
                    flags += marker
        flags = re.sub(r"\|\(1ULL<<MCID::(?:MayLoad|MayStore|UnmodeledSideEffects|MayRaiseFPException)\)", "", flags)
        # MOVS/STOS have their complete ordinary memory/register effects.
        # Port I/O and the other system operations also have ambient effects.
        side_effects = () if name.startswith(("REP_MOVS", "REP_STOS")) else ("UnmodeledSideEffects",)
        flags += "".join(f"|(1ULL<<MCID::{flag})" for flag in
                         (*memory_effects, *side_effects))
        fields[9] = flags + ", " + encoding
        text = text[:match.start()] + match.group(1) + ",\t".join(fields) + \
            match.group(3) + text[match.end():]
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    original = TABLE.read_text()
    updated = synchronize(original)
    if args.write:
        TABLE.write_text(updated)
    elif original != updated:
        parser.exit(1, "system instruction effects are stale; run with --write\n")


if __name__ == "__main__":
    main()

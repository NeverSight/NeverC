#!/usr/bin/env python3
"""Compile and verify the generated AArch64 context-interpose stub."""

from __future__ import annotations

import argparse
from pathlib import Path
import struct
import subprocess
import tempfile

from elftools.elf.elffile import ELFFile


KERNEL_ROOT = Path(__file__).resolve().parents[1]
SOURCE = KERNEL_ROOT / "src" / "nvk_interpose.c"
SYMBOL_PREFIX = "_neverc_krt_ctx_stub_template"


def signed_imm19_target(words: tuple[int, ...], index: int) -> int:
    immediate = (words[index] >> 5) & 0x7FFFF
    if immediate & (1 << 18):
        immediate -= 1 << 19
    return index + immediate


def read_template(path: Path) -> tuple[int, ...]:
    with path.open("rb") as stream:
        elf = ELFFile(stream)
        symbols = elf.get_section_by_name(".symtab")
        if symbols is None:
            raise RuntimeError("compiled object has no symbol table")
        matches = [
            symbol
            for symbol in symbols.iter_symbols()
            if symbol.name.startswith(SYMBOL_PREFIX)
            and symbol["st_info"]["type"] == "STT_OBJECT"
            and symbol["st_shndx"] != "SHN_UNDEF"
        ]
        if len(matches) != 1:
            raise RuntimeError(
                f"expected one context-stub object, found {len(matches)}"
            )
        symbol = matches[0]
        section = elf.get_section(symbol["st_shndx"])
        start = symbol["st_value"] - section["sh_addr"]
        size = symbol["st_size"]
        raw = section.data()[start : start + size]
        if len(raw) != size or size % 4:
            raise RuntimeError("context-stub object has invalid bounds or alignment")
        return struct.unpack(f"<{size // 4}I", raw)


def sp_immediate_adjustment(word: int) -> int:
    """Return an ADD/SUB-immediate SP delta, or zero for other instructions."""

    opcode_and_regs = word & 0xFF0003FF
    if opcode_and_regs not in (0x910003FF, 0xD10003FF):
        return 0
    immediate = (word >> 10) & 0xFFF
    if word & (1 << 22):
        immediate <<= 12
    return immediate if opcode_and_regs == 0x910003FF else -immediate


def call_replacement_sp_delta(words: tuple[int, ...]) -> tuple[int, int]:
    """Interpret the v11 CALL path up to the owner replacement's BLR X17."""

    pc = 19  # The incoming SP is captured immediately before the ctx frame.
    sp_delta = 0
    for _ in range(len(words)):
        word = words[pc]
        sp_delta += sp_immediate_adjustment(word)
        if word == 0xD63F0220:  # BLR X17 -> owner replacement
            return sp_delta, pc

        # Model a successful guard entry and a handler requesting CALL mode.
        # The conditional branches at 65 and 129 are therefore taken, while
        # the no-token/no-force branches are not.
        if pc in (65, 129):
            pc = signed_imm19_target(words, pc)
        else:
            pc += 1
        if pc < 0 or pc >= len(words):
            break
    raise RuntimeError("CALL path does not reach the owner replacement")


def check_call_aapcs64_stack_boundary(words: tuple[int, ...]) -> None:
    """The replacement must observe the exact incoming AAPCS64 stack view."""

    sp_delta, call_slot = call_replacement_sp_delta(words)
    incoming_sp = 0x100000
    call_sp = incoming_sp + sp_delta
    poison = {
        incoming_sp + offset: f"poison[{offset:+d}]"
        for offset in range(-256, 257, 8)
    }
    cases = (
        (
            "fixed integer arguments 9/10",
            ((0, "arg9"), (8, "arg10")),
        ),
        (
            "16-byte stack aggregate",
            ((0, "aggregate.lo"), (8, "aggregate.hi")),
        ),
        (
            "variadic overflow boundary",
            ((0, "va.stack0"), (8, "va.stack1")),
        ),
    )
    failures: list[str] = []
    for name, stack_values in cases:
        memory = dict(poison)
        for offset, value in stack_values:
            memory[incoming_sp + offset] = value
        expected = tuple(value for _, value in stack_values)
        observed = tuple(memory.get(call_sp + offset) for offset, _ in stack_values)
        if observed != expected:
            failures.append(f"{name}: expected {expected}, observed {observed}")

    if call_sp & 15:
        failures.append(f"replacement SP 0x{call_sp:x} is not 16-byte aligned")
    if sp_delta != 0:
        failures.insert(
            0,
            f"replacement BLR slot {call_slot} changes incoming SP by {sp_delta}",
        )
    if failures:
        raise RuntimeError(
            "CALL wrapper violates the incoming AAPCS64 stack boundary:\n  "
            + "\n  ".join(failures)
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--compiler",
        default=str(KERNEL_ROOT.parents[2] / "build-neverc" / "bin" / "neverc"),
    )
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="neverc-interpose-stub-") as temp:
        output = Path(temp) / "nvk_interpose.o"
        subprocess.run(
            [
                args.compiler,
                "--target=aarch64-linux-android",
                "-fandroid-kernel-driver-mode",
                "-DNVK_KERNEL=612",
                f"-I{KERNEL_ROOT / 'include'}",
                f"-I{KERNEL_ROOT / 'src'}",
                "-O0",
                "-fno-lto",
                "-c",
                str(SOURCE),
                "-o",
                str(output),
            ],
            check=True,
        )
        words = read_template(output)

    if len(words) != 213:
        raise RuntimeError(f"context-stub word count is {len(words)}, expected 213")

    branch_targets = {
        10: 7,     # entry inflight LL/SC retry
        16: 206,   # disabled entry -> permanent passthrough
        55: 78,    # recursion/full guard set -> handler bypass
        63: 66,    # no force target -> release guard
        65: 106,   # CALL keeps guard through replacement
        77: 106,   # DIRECT continues after releasing guard
        129: 145,  # CALL force mode
        140: 137,  # DIRECT inflight LL/SC retry
        168: 173,  # CALL cleanup found current task slot
        171: 166,  # CALL cleanup scans the next persistent slot
        175: 172,  # missing retained caller LR is fail-stop
        201: 198,  # CALL cleanup inflight LL/SC retry
    }
    for slot, expected in branch_targets.items():
        actual = signed_imm19_target(words, slot)
        if actual != expected:
            raise RuntimeError(
                f"context-stub branch {slot} targets {actual}, expected {expected}"
            )

    expected_words = {
        0: 0xD50324DF,    # BTI JC entry
        19: 0xD104C3FF,  # SUB SP, SP, #304
        44: 0xF9407BE1,  # pass original caller LR to guard-enter
        53: 0xD63F0060,  # BLR guard-enter helper
        54: 0xF90093E0,  # save 1-based guard token
        61: 0xD63F0060,  # BLR business handler
        66: 0xF94093E1,  # non-CALL guard token
        75: 0xD63F0060,  # BLR non-CALL guard-leave helper
        100: 0x9104C3FF, # ADD SP, SP, #304
        105: 0xD61F0220, # normal path -> draining passthrough
        130: 0x9104C3FF, # DIRECT restores original SP
        144: 0xD65F0220, # DIRECT force tail
        145: 0x9104C3FF, # CALL restores exact original target SP
        146: 0xD63F0220, # CALL owner replacement
        147: 0xD50324DF, # permanent CALL cleanup landing
        148: 0xD10283FF, # save return registers in 160-byte cleanup frame
        164: 0xD538410A, # find retained slot by current task after save
        165: 0xD280020B, # scan exactly 16 persistent slots
        166: 0xC8DFFD2C, # acquire-load slot.task using x9-x13 scratch
        172: 0xD43BD5A0, # missing owned slot/LR is fail-stop
        174: 0xC8DFFDBE, # acquire-load retained original caller LR
        176: 0xF90001BF, # clear retained LR first
        177: 0xC89FFD3F, # then release-clear slot.task
        190: 0xF9404FFE, # restore retained original caller LR
        191: 0x910283FF, # restore cleanup frame
        205: 0xD65F03C0, # return to original caller after cleanup
        212: 0xD61F0220, # disabled path -> draining passthrough
    }
    for slot, expected in expected_words.items():
        if words[slot] != expected:
            raise RuntimeError(
                f"context-stub word {slot}=0x{words[slot]:08x}, "
                f"expected 0x{expected:08x}"
            )

    if any((word & ~0x1F) == 0xD5384100 for word in words[:45]):
        raise RuntimeError(
            "context stub reads SP_EL0 before saving the complete machine context"
        )

    if 0xD63F0060 in words[147:206]:
        raise RuntimeError("CALL cleanup must not invoke a C helper")

    check_call_aapcs64_stack_boundary(words)

    print("interpose context-stub machine-code test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

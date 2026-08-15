#!/usr/bin/env python3
"""Public single-instruction text patch boundary for kernel modules."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
HEADER = (ROOT / "include" / "nvk_mem.h").read_text()
SOURCE = (ROOT / "src" / "nvk_mem.c").read_text()


assert re.search(
    r"int\s+neverc_krt_mem_patch_instruction\s*\(\s*unsigned long\s+addr\s*,\s*u32\s+insn\s*\)\s*;",
    HEADER,
    re.S,
), "missing public single-instruction patch API"

match = re.search(
    r"int\s+neverc_krt_mem_patch_instruction\s*\([^)]*\)\s*\{",
    SOURCE,
)
assert match, "missing single-instruction patch implementation"
start = match.end()
depth = 1
index = start
while index < len(SOURCE) and depth:
    depth += SOURCE[index] == "{"
    depth -= SOURCE[index] == "}"
    index += 1
assert depth == 0
body = SOURCE[start : index - 1]

assert "addr & 3" in body or "addr % 4" in body, "unaligned instruction target is accepted"
assert "_neverc_krt_mem_write_via_insn_write" in body
assert "sizeof(insn)" in body

backend = SOURCE[SOURCE.index("_neverc_krt_mem_write_via_insn_write") :]
assert "_neverc_krt_insn_patch_text(addrs, insns" in backend
assert '"dsb ish"' in backend and '"isb"' in backend

print("NeverC single-instruction patch boundary: OK")

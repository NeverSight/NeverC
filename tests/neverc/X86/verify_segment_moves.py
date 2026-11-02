#!/usr/bin/env python3
"""Qualify native segment MOV interfaces against original bytes on Linux x64."""

from pathlib import Path

from native_qualification import run_qualification


def sources():
    ir = """declare i16 @llvm.x86.readseg(i32 immarg)
declare void @llvm.x86.readseg.mem(i32 immarg, ptr)
declare void @llvm.x86.writeseg(i32 immarg, i16)
declare void @llvm.x86.writeseg.mem(i32 immarg, ptr)
"""
    c = ""
    operations = ("readseg", "readseg_mem", "writeseg", "writeseg_mem")
    names = ("read", "readmem", "write", "writemem")
    for kind, operation in enumerate(operations):
        for number, segment in enumerate(("es", "cs", "ss", "ds", "fs", "gs")):
            if kind >= 2 and number == 1:
                continue
            name = "generated_" + names[kind] + "_" + segment
            args = str(number) + ("" if kind == 0 else ", value" if kind == 2 else ", p")
            c += (f"unsigned long long {name}(unsigned long long value, void *p, unsigned *marker) {{\n"
                  "  *marker = 17;\n")
            c += ("  unsigned short result = " if kind == 0 else "  ")
            c += f"__builtin_ia32_{operation}({args});\n  *marker = 23;\n"
            c += "  return " + ("result" if kind == 0 else "0") + ";\n}\n"
            ir += f"define i64 @{name}(i64 %value, ptr %p, ptr %marker) {{\n  store i32 17, ptr %marker\n"
            if kind == 2:
                ir += "  %selector = trunc i64 %value to i16\n"
            params = f"i32 {number}" + ("" if kind == 0 else ", i16 %selector" if kind == 2 else ", ptr %p")
            ir += ("  %v = call i16" if kind == 0 else "  call void")
            ir += f" @llvm.x86.{operation.replace('_', '.')}({params})\n  store i32 23, ptr %marker\n"
            if kind == 0:
                ir += "  %result = zext i16 %v to i64\n  ret i64 %result\n}\n"
            else:
                ir += "  ret i64 0\n}\n"
    return {"ir": ir, "c": c}


if __name__ == "__main__":
    run_qualification(
        __doc__, Path(__file__).with_name("segment_move_harness.c").read_text(),
        sources(), {"state_observations": 2304, "boundary_observations": 22,
                    "fault_observations": 124}, 2305,
        "Intel SDM revision 093, Volume 2B: MOV. "
        "https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html",
        __file__, exclusions=["other CPU models", "i386 execution", "code16",
                              "CPL0..2 and privileged descriptor-page faults",
                              "alignment-check and asynchronous event delivery",
                              "null selectors 1..3 normalized by Linux return",
                              "complete original GPR/flags restart state"])

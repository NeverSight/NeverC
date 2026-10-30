#!/usr/bin/env python3
"""Qualify native far-pointer loads against original bytes on Linux x64."""

from pathlib import Path

from native_qualification import run_qualification


def sources():
    ir, c = "", ""
    for operation in ("lfs", "lgs", "lss"):
        for bits in (16, 32, 64):
            name = f"{operation}{bits}"
            convert = f"%r = zext i{bits} %v to i64\n" if bits < 64 else ""
            result = "%r" if bits < 64 else "%v"
            ir += f"""
declare i{bits} @llvm.x86.{name}(ptr)
define i64 @generated_{name}(ptr %p, ptr %marker) {{
  store i32 17, ptr %marker
  %v = call i{bits} @llvm.x86.{name}(ptr %p)
  store i32 23, ptr %marker
  {convert}ret i64 {result}
}}
"""
            c += f"""
unsigned long long generated_{name}(const void *p, unsigned *marker) {{
  *marker = 17;
  unsigned long long result = __builtin_ia32_{name}(p);
  *marker = 23;
  return result;
}}
"""
    return {"ir": ir, "c": c}


if __name__ == "__main__":
    run_qualification(
        __doc__, Path(__file__).with_name("far_pointer_harness.c").read_text(),
        sources(), {"state_observations": 4320, "stopped_observations": 1440,
                    "boundary_observations": 18,
                    "fault_observations": 186}, 4321,
        "Intel SDM 325462, Vol. 2A: LDS/LES/LFS/LGS/LSS. "
        "https://cdrdv2-public.intel.com/835781/325462-sdm-vol-1-2abcd-3abcd-4.pdf",
        __file__, exclusions=["other CPU models", "i386 runtime", "code16",
                              "privileged descriptor-table accessed-bit faults",
                              "alignment-check exceptions", "asynchronous interrupt delivery",
                              "null selectors 1..3 after Linux interrupt/signal return",
                              "complete lifted machine-state continuation"])

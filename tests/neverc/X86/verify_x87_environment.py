#!/usr/bin/env python3
"""Qualify both native x87 environment formats against original instruction bytes."""

from pathlib import Path

from x87_native_qualification import run_qualification


OPERATIONS = tuple(f"{op}{bits}" for op in ("fnstenv", "fldenv", "fnsave", "frstor")
                   for bits in (16, 32))


def sources():
    ir = "declare void @llvm.x86.fninit()\n"
    ir += "declare <1 x i64> @llvm.x86.mmx.padd.b(<1 x i64>, <1 x i64>)\n"
    ir += "declare <1 x i64> @llvm.x86.mmx.pxor(<1 x i64>, <1 x i64>)\n"
    c = "#include <mmintrin.h>\n"
    for name in OPERATIONS:
        ir += f"""
declare void @llvm.x86.{name}(ptr)
define void @generated_{name}(ptr %p, ptr %marker) {{
  store i32 17, ptr %marker
  call void @llvm.x86.{name}(ptr %p)
  store i32 23, ptr %marker
  ret void
}}
define x86_fp80 @live_x87_{name}(ptr %state, ptr %in, ptr %other) {{
  %a = load volatile x86_fp80, ptr %in, align 1
  call void @llvm.x86.{name}(ptr %state)
  %b = load volatile x86_fp80, ptr %other, align 1
  %r = fadd x86_fp80 %a, %b
  ret x86_fp80 %r
}}
define i64 @live_mmx_{name}(ptr %state, i64 %a, i64 %b) #0 {{
  %x = bitcast i64 %a to <1 x i64>
  %y = bitcast i64 %b to <1 x i64>
  %sum = call <1 x i64> @llvm.x86.mmx.padd.b(<1 x i64> %x, <1 x i64> %y)
  call void @llvm.x86.{name}(ptr %state)
  %mixed = call <1 x i64> @llvm.x86.mmx.pxor(<1 x i64> %sum, <1 x i64> %x)
  %bits = bitcast <1 x i64> %mixed to i64
  call void @llvm.x86.fninit()
  ret i64 %bits
}}
"""
        c += f"""
#if !__has_builtin(__builtin_ia32_{name})
#error missing native environment builtin
#endif
void generated_{name}(void *p, unsigned *marker) {{
  *marker = 17; __builtin_ia32_{name}(p); *marker = 23;
}}
long double live_x87_{name}(void *state, volatile long double *in,
                            volatile long double *other) {{
  long double a = *in;
  __builtin_ia32_{name}(state);
  return a + *other;
}}
__attribute__((target("mmx")))
unsigned long long live_mmx_{name}(void *state, unsigned long long a, unsigned long long b) {{
  __m64 x = (__m64)a, y = (__m64)b;
  __m64 sum = _mm_add_pi8(x, y);
  __builtin_ia32_{name}(state);
  __m64 mixed = _mm_xor_si64(sum, x);
  unsigned long long bits = (unsigned long long)mixed;
  __builtin_ia32_fninit();
  return bits;
}}
"""
    for name in ("fldenv16", "fldenv32", "frstor16", "frstor32"):
        ir += f"""
define void @generated_defer_{name}(ptr %p, ptr %marker) {{
  store i32 17, ptr %marker
  call void @llvm.x86.{name}(ptr %p)
  store i32 19, ptr %marker
  call void @llvm.x86.fwait()
  store i32 23, ptr %marker
  ret void
}}
"""
        c += f"""
void generated_defer_{name}(void *p, unsigned *marker) {{
  *marker = 17; __builtin_ia32_{name}(p); *marker = 19;
  __builtin_ia32_fwait(); *marker = 23;
}}
"""
    ir += "declare void @llvm.x86.fwait()\n"
    return {"ir": ir + 'attributes #0 = { "target-features"="+mmx" }\n', "c": c}


if __name__ == "__main__":
    run_qualification(
        __doc__, Path(__file__).with_name("x87_environment_harness.c").read_text(),
        sources(), {"state_observations": 9216, "fault_observations": 80,
                    "fault_and_nonfault_cases": 104, "boundary_observations": 16,
                    "live_value_cases": 512}, 9849,
        "Intel SDM 325462, Vol. 1 section 8.7.1 and Vol. 2A: FLDENV, FRSTOR, "
        "FSAVE/FNSAVE, FSTENV/FNSTENV. https://cdrdv2-public.intel.com/835781/"
        "325462-sdm-vol-1-2abcd-3abcd-4.pdf", __file__)

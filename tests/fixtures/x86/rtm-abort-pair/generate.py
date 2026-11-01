"""Independent raw-byte reference for all 256 RTM abort immediates."""
from pathlib import Path


def generate(directory):
    directory = Path(directory)
    declaration = ("struct result *, unsigned long long *, unsigned long long, "
                   "unsigned long long, unsigned long long, unsigned long long, "
                   "double, double, double, double, double, double")
    (directory / "fixture.h").write_text(
        "struct result { unsigned long long status,i0,i1,i2; double fp; };\n"
        f"typedef void (*operation)({declaration});\n"
        "extern operation const candidate[256], reference[256];\n")
    assembly = [".text"]
    for code in range(256):
        assembly += [f".globl raw_{code}", f".type raw_{code},@function",
                     f"raw_{code}:", ".byte 0xc7,0xf8", ".long 3",
                     f".byte 0xc6,0xf8,{code}", "ret",
                     f".size raw_{code},.-raw_{code}"]
    assembly += ['.section .note.GNU-stack,"",@progbits']
    (directory / "original.S").write_text("\n".join(assembly) + "\n")
    for name in ("candidate", "reference"):
        lines = ['#include "fixture.h"']
        for code in range(256):
            if name == "reference":
                lines += [f"extern unsigned raw_{code}(void);"]
            operation = (f"__builtin_ia32_rtm_abort_pair({code})"
                         if name == "candidate" else f"raw_{code}()")
            lines += [f"void {name}_{code}(struct result *out,unsigned long long *m,",
                      "unsigned long long a,unsigned long long b,",
                      "unsigned long long c,unsigned long long d,",
                      "double x0,double x1,double x2,double x3,double x4,double x5){",
                      "unsigned long long old=*m;*m=a^b^c^d;",
                      f"unsigned status={operation};",
                      "out->status=status;out->i0=(a*17)^(b*31)^(c*41)^(d*71);",
                      "out->i1=old;out->i2=*m;",
                      "out->fp=x0+x1*2+x2*4+x3*8+x4*16+x5*32;}"]
        lines += [f"operation const {name}[256]={{",
                  ",\n".join(f"{name}_{code}" for code in range(256)), "};"]
        (directory / f"{name}.c").write_text("\n".join(lines) + "\n")

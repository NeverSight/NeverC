#!/usr/bin/env python3
"""Execute the Hyper-V register transport against an independent page oracle.

This checks the native CALL ABI, not Microsoft Hyper-V service semantics.
Every GPR and vector byte is compared, including the unmodified upper lanes.
"""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def run(command):
    subprocess.run([str(x) for x in command], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--backend-llc")
    parser.add_argument("--new-pm", action="store_true")
    parser.add_argument("--keep", type=Path, required=True)
    args = parser.parse_args()
    args.keep.mkdir(parents=True, exist_ok=True)
    source = args.keep / "candidate.c"
    source.write_text("#include <hypervcontextintrin.h>\n" + "\n".join(
        f"void {attr} run_{stride}{abi}(void *e, void *g, void *v) {{\n"
        "volatile unsigned long long frame[32];\n"
        "for(unsigned i=0;i<32;++i) frame[i]=i*71+9;\n"
        f"__builtin_ia32_hyperv_call(e,g,v,{stride});\n"
        "for(unsigned i=0;i<32;++i) if(frame[i]!=i*71+9) __builtin_trap();\n}"
        for stride in (16, 32, 64) for abi, attr in
        (("", ""), ("_ms", "__attribute__((ms_abi))"))))
    with source.open("a") as f:
        f.write("\n".join(
            f"double {attr} live{abi}(void *e,void *g,void *v,"
            "double a,double b,double c,double d,double f,double h) {"
            "__builtin_ia32_hyperv_call(e,g,v,64);return a+3*b+5*c+7*d+11*f+13*h;}"
            for abi,attr in (("",""),("_ms","__attribute__((ms_abi))"))))
    harness = args.keep / "oracle.c"
    declarations = "\n".join(
        f"extern void {attr} run_{stride}{abi}(void*,void*,void*);"
        for stride in (16, 32, 64) for abi, attr in
        (("", ""), ("_ms", "__attribute__((ms_abi))")))
    dispatch = "\n".join(
        f"case {i}: run_{stride}{abi}((void*)page,g,v);break;"
        for i, (stride, abi) in enumerate((s,a) for s in (16,32,64) for a in ("","_ms")))
    declarations += """
extern double live(void*,void*,void*,double,double,double,double,double,double);
extern double __attribute__((ms_abi)) live_ms(void*,void*,void*,double,double,double,double,double,double);
"""
    harness.write_text(r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern void page(void);
__asm__(".text\n.globl page\npage:\n"
        "test $8,%rsp\njz 9f\n"
        "lea (%rcx,%rdx),%rax\nadd %r8,%rax\n"
        "add $17,%rcx\nrol $7,%rdx\nneg %r8\n"
        "pxor mask(%rip),%xmm0\npxor mask(%rip),%xmm1\n"
        "pxor mask(%rip),%xmm2\npxor mask(%rip),%xmm3\n"
        "pxor mask(%rip),%xmm4\npxor mask(%rip),%xmm5\n"
        "ret\n9: ud2\n.balign 16\nmask:\n"
        ".quad 0x0123456789abcdef,0xfedcba9876543210\n");
''' + declarations + r'''
int main(void) {
  static const uint64_t mask[2]={0x0123456789abcdefULL,0xfedcba9876543210ULL};
  for(unsigned which=0;which<6;++which)for(unsigned seed=0;seed<256;++seed) {
    unsigned stride=16u<<(which/2);
    unsigned char gs[144],vs[416],wantgs[144],wantvs[416];
    for(unsigned i=0;i<sizeof(gs);++i)gs[i]=(unsigned char)(seed+i*31);
    for(unsigned i=0;i<sizeof(vs);++i)vs[i]=(unsigned char)(seed*13+i*7);
    memcpy(wantgs,gs,sizeof(gs));memcpy(wantvs,vs,sizeof(vs));
    // Deliberately unaligned banks and surrounding guard bytes.
    void *g=gs+3,*v=vs+5;uint64_t values[16];memcpy(values,g,128);
    values[0]=values[1]+values[2]+values[8];values[1]+=17;
    values[2]=(values[2]<<7)|(values[2]>>57);values[8]=-values[8];
    memcpy(wantgs+3,values,128);
    for(unsigned i=0;i<6;++i)for(unsigned j=0;j<16;++j)
      wantvs[5+i*stride+j]^=((const unsigned char*)mask)[j];
    switch(which) {
''' + dispatch + r'''
    }
    if(memcmp(gs,wantgs,sizeof(gs))||memcmp(vs,wantvs,sizeof(vs)))abort();
  }
  for(unsigned i=0;i<256;++i) {
    uint64_t g[16]={0};unsigned char v[384]={0};double a=i+0.5;
    double expected=a+3*(a+1)+5*(a+2)+7*(a+3)+11*(a+4)+13*(a+5);
    if(live((void*)page,g,v,a,a+1,a+2,a+3,a+4,a+5)!=expected)abort();
    if(live_ms((void*)page,g,v,a,a+1,a+2,a+3,a+4,a+5)!=expected)abort();
  }
  puts("1536 full-bank native page comparisons passed");
  return 0;
}
''')
    records = []
    for opt in ("0", "2"):
        obj = args.keep / f"candidate-O{opt}.o"
        command = [args.compiler, "-std=c23", "-ffreestanding", "-fno-lto",
                   "-fno-builtin-mimalloc", f"-O{opt}", "-fno-stack-protector"]
        if args.backend_llc:
            ir = obj.with_suffix(".ll")
            run(command + ["-S", "-emit-llvm", source, "-o", ir])
            run([args.backend_llc, f"-O{opt}", "-filetype=obj",
                 "-verify-machineinstrs", *(["-enable-new-pm"] if args.new_pm else []),
                 ir, "-o", obj])
        else:
            run(command + ["-mllvm", "-verify-machineinstrs", "-c", source, "-o", obj])
        binary = args.keep / f"test-O{opt}"
        run(["clang", "-O2", "-fsanitize=undefined", harness, obj, "-o", binary])
        run([binary])
        records.append({"optimization": opt, "comparisons": 1536, "live_fp_cases": 512,
                        "object_sha256": hashlib.sha256(obj.read_bytes()).hexdigest()})
    (args.keep / "result.json").write_text(json.dumps({"contract": "hyperv-page-register-transport",
        "service_semantics_qualified": False, "records": records}, indent=2)+"\n")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Cross-call NeverC and Clang using actual ELF32, COFF32 and Mach-O32 objects.

Unicorn runs complete caller/callee code, stack cleanup and preserved registers.
No platform OS service, SEH, hardware-intrinsic or integrated-linker claim.
Requires lief and unicorn, and the full NeverSight LLVM codegen protocol.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

import lief
import unicorn
import unicorn.x86_const as reg

CODE, PEER, STACK, DATA, STOP = 0x100000, 0x200000, 0x300000, 0x400000, 0x500000
PAGE, REGION, LIMIT = 4096, 0x80000, 100000
TRIPLES = {"linux": "i386-unknown-linux-gnu", "windows": "i386-pc-windows-msvc",
           "darwin": "i386-apple-macosx10.13"}
SEEDS = (0, 1, 0x800001ff, 0xffffffff)
SAVED = (reg.UC_X86_REG_EBX, reg.UC_X86_REG_ESI, reg.UC_X86_REG_EDI, reg.UC_X86_REG_EBP)
# Each mask names defined bytes, never aggregate/x87 padding.
CASES = [
    ("bitfields", "struct { unsigned a:3; unsigned short b:9; unsigned c:13; }", "BITFIELDS", True),
    ("bitunion", "union { unsigned a:3; unsigned short b:9; unsigned c:13; }", "BITUNION", True),
    ("zero_bitfield", "struct { unsigned a:3; unsigned :0; unsigned b:9; unsigned c:13; }", "BITFIELDS", True),
    ("u8", "unsigned char", "ALL", False),
    ("i16", "short", "ALL", False),
    ("u32", "unsigned", "ALL", False),
    ("u64", "unsigned long long", "ALL", True),
    ("float", "float", "ALL", False),
    ("double", "double", "ALL", True),
    ("extended", "long double", "EXTENDED", True),
    ("bits17", "unsigned _BitInt(17)", "BITS17", False),
    ("bits33", "unsigned _BitInt(33)", "BITS33", False),
    ("bits65", "unsigned _BitInt(65)", "BITS65", True),
    ("bits65_signed", "_BitInt(65)", "BITS65", True),
    ("bits97", "unsigned _BitInt(97)", "BITS97", True),
    ("record1", "struct { char x; }", "ALL", False),
    ("record2", "struct { short x; }", "ALL", False),
    ("record4", "struct { unsigned x; }", "ALL", False),
    ("record8", "struct { unsigned a, b; }", "ALL", True),
    ("record12", "struct { unsigned a, b, c; }", "ALL", True),
    ("record_float", "struct { float x; }", "ALL", False),
    ("record_double", "struct { double x; }", "ALL", True),
    ("pointer", "struct { void *x; }", "ALL", False),
    ("union8", "union { unsigned long long x; unsigned a[2]; }", "ALL", True),
    ("packed", "struct __attribute__((packed)) { char x; unsigned y; }", "ALL", True),
    ("aligned", "struct __attribute__((aligned(32))) { unsigned x; }", "FIELD(x)", True),
    ("mixed", "struct { char x; double y; short z; }", "FIELD(x); FIELD(y); FIELD(z)", True),
    ("v1", "unsigned __attribute__((vector_size(4)))", "ALL", False),
    ("v2", "unsigned __attribute__((vector_size(8)))", "ALL", False),
    ("v1q", "unsigned long long __attribute__((vector_size(8)))", "ALL", False),
    ("v4", "unsigned __attribute__((vector_size(16)))", "ALL", True),
    ("record_v2", "struct { v2 x; }", "ALL", True),
    ("record_v4", "struct { v4 x; }", "ALL", True),
]


def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def sources():
    common = "typedef unsigned v2 __attribute__((vector_size(8)));\n"
    common += "typedef unsigned v4 __attribute__((vector_size(16)));\n"
    for name, ty, _, _ in CASES:
        common += f"typedef {ty} T_{name};\n"
    producer, consumer, profiles = [common], [common], []
    for name, _, mask, variadic in CASES:
        t = f"T_{name}"
        mark = mask.replace("ALL", f"for(unsigned i=0;i<sizeof({t});++i) m[i]=255;")
        mark = mark.replace("EXTENDED", "for(unsigned i=0;i<(sizeof(long double)==8?8:10);++i) m[i]=255;")
        if mask.startswith("BITS"):
            bits = int(mask[4:])
            mark = f"for(unsigned i=0;i<{bits//8};++i) m[i]=255; m[{bits//8}]={(1<<(bits%8))-1};"
        if mask in ("BITFIELDS", "BITUNION"):
            assignments = "tmp.a=7;tmp.b=511;tmp.c=8191;"
            if mask == "BITUNION":
                assignments = "tmp.c=8191;"
            mark = f"{t} tmp; volatile unsigned char *raw=(volatile unsigned char*)&tmp; for(unsigned i=0;i<sizeof(tmp);++i)raw[i]=0;{assignments}for(unsigned i=0;i<sizeof(tmp);++i)m[i]=raw[i];"
        mark = re.sub(r"FIELD\((\w+)\)", lambda m:
            f"for(unsigned i=0;i<sizeof((({t}*)0)->{m[1]});++i) m[__builtin_offsetof({t},{m[1]})+i]=255;", mark)
        if mask.startswith("BITS"):
            bits = int(mask[4:])
            high = f"((unsigned _BitInt({bits}))1<<{bits-1})"
            producer.append(f"static const {t} global_{name}[4]={{3,({t})-1,({t}){high},({t})({high}/2+5)}};\n"
                            f"void constants_{name}({t} *out){{for(unsigned i=0;i<4;++i)out[i]=global_{name}[i];}}\n")
        producer.append(f"void layout_{name}(unsigned *p, volatile unsigned char *m) {{"
                        f"p[0]=sizeof({t});p[1]=_Alignof({t});{mark}}}\n")
        for cc in ("cdecl", "stdcall", "regparm", "fastcall", "variadic"):
            if cc == "variadic" and not variadic:
                continue
            key = name + "_" + cc
            attr = "__attribute__((regparm(3)))" if cc == "regparm" else \
                   "" if cc == "variadic" else f"__attribute__(({cc}))"
            guard = "#ifdef _WIN32\n" if cc == "fastcall" else ""
            # The Linux/Darwin register-parameter projection is also accepted
            # on Windows, with its own aggregate and vector rules.
            params = f"{t} a, {t} b, {t} c, {t} d, unsigned tag, {t} *out, unsigned *seen"
            read = ""
            if cc == "variadic":
                params = "unsigned tag, ..."
                read = f"__builtin_va_list ap; __builtin_va_start(ap, tag);"
                for v in "abcd":
                    read += f"{t} {v}=__builtin_va_arg(ap,{t});"
                read += f"{t} *out=__builtin_va_arg(ap,{t}*);unsigned *seen=__builtin_va_arg(ap,unsigned*);__builtin_va_end(ap);"
            producer.append(guard + f"{t} {attr} callee_{key}({params}){{{read}"
                            "seen[0]=tag;out[0]=a;out[1]=b;out[2]=c;return d;}\n" + ("#endif\n" if guard else ""))
            fparams = "unsigned, ..." if cc == "variadic" else f"{t},{t},{t},{t},unsigned,{t}*,unsigned*"
            callargs = "tag,in[0],in[1],in[2],in[3],out,seen" if cc == "variadic" else "in[0],in[1],in[2],in[3],tag,out,seen"
            consumer.append(guard + f"unsigned long long caller_{key}(void *raw, {t} *in, {t} *out, unsigned tag, unsigned *seen){{"
                            f"typedef {t} ({attr} *Fn)({fparams});Fn fn=(Fn)raw;out[3]=fn({callargs});"
                            "return ((unsigned long long)tag<<32)|(tag^0xa5a5a5a5u); }\n" + ("#endif\n" if guard else ""))
            profiles.append((name, cc, key))
    return "".join(producer), "".join(consumer), profiles


def object_code(path, base):
    image = lief.parse(str(path))
    sections = list(image.sections)
    elf, coff = isinstance(image, lief.ELF.Binary), isinstance(image, lief.COFF.Binary)
    blob = bytearray(REGION)
    offsets = {}
    cursor = 0
    for index, section in enumerate(sections):
        if elf and not (int(section.flags) & 2):
            continue
        if coff and section.name.startswith((".debug", "/")):
            continue
        if not section.size and not section.content:
            continue
        offset = cursor if elf or coff else section.virtual_address
        offsets[index if elf else index+1] = offset
        blob[offset:offset+len(section.content)] = bytes(section.content)
        cursor = (offset+max(section.size,len(section.content))+PAGE-1)&-PAGE
    assert cursor < REGION-PAGE
    # Independent cdecl byte-copy helper for compiler-emitted memcpy calls.
    memcpy = REGION-PAGE
    helper = bytes.fromhex("56578b44240c89c78b7424108b4c2414fcf3a45f5ec3")
    blob[memcpy:memcpy+len(helper)] = helper

    def symbol_offset(symbol):
        index = symbol.shndx if elf else symbol.section_idx if coff else symbol.numberof_sections
        if index:
            return offsets[index]+symbol.value if elf or coff else symbol.value
        assert symbol.name.lstrip("_") == "memcpy", (path,"unbound symbol",symbol.name)
        return memcpy

    symbols = {}
    for symbol in image.symbols:
        name = re.sub(r"^[_@]|@\d+$", "", symbol.name)
        if name.startswith(("callee_", "caller_", "layout_", "constants_")):
            symbols[name] = symbol_offset(symbol)

    for index, section in enumerate(sections):
        slot=index if elf else index+1
        if slot not in offsets:
            continue
        relocs = [r for r in image.relocations if r.has_section and r.section == section] if elf else section.relocations
        for relocation in relocs:
            offset=offsets[slot]+relocation.address
            if not elf and not coff:
                offset=offsets[slot]+relocation.address-section.offset
            value=struct.unpack_from("<I",blob,offset)[0]
            if elf:
                target=base+symbol_offset(relocation.symbol)
                if relocation.type==lief.ELF.Relocation.TYPE.X86_32:value+=target
                elif relocation.type in (lief.ELF.Relocation.TYPE.X86_PC32,lief.ELF.Relocation.TYPE.X86_PLT32):value+=target-base-offset
                else:raise AssertionError((path,"unbound ELF relocation",relocation))
            elif coff:
                target=base+symbol_offset(relocation.symbol)
                if relocation.type==lief.COFF.Relocation.TYPE.I386_DIR32:value+=target
                elif relocation.type==lief.COFF.Relocation.TYPE.I386_REL32:value+=target-base-offset-4
                else:raise AssertionError((path,"unbound COFF relocation",relocation))
            else:
                assert int(relocation.type)==0 and relocation.size==32,(path,relocation)
                if relocation.is_scattered:
                    # All Mach-O sections retain their original relative VAs.
                    # Scattered VANILLA references therefore move by the same
                    # load bias; an intra-image PC-relative delta is unchanged.
                    assert 0 <= relocation.value < REGION
                    if not relocation.pc_relative:value+=base
                elif relocation.has_symbol:
                    value+=base+symbol_offset(relocation.symbol)
                    if relocation.pc_relative:value-=base
                elif not relocation.pc_relative:value+=base
            struct.pack_into("<I",blob,offset,value&0xffffffff)
    return bytes(blob), symbols


def machine(left, right=None):
    u = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
    for address in (CODE, PEER, STACK, DATA, STOP):
        u.mem_map(address, REGION)
    u.mem_write(CODE, left[0])
    if right:
        u.mem_write(PEER, right[0])
    u.reg_write(reg.UC_X86_REG_EFLAGS, 2)
    # ABI entry has an empty x87 bank and masked exceptions.
    u.reg_write(reg.UC_X86_REG_FPCW, 0x37f)
    u.reg_write(reg.UC_X86_REG_FPTAG, 0xffff)
    return u


def call(u, entry, args, seed=0):
    sp = STACK + REGION - 260
    u.mem_write(sp, struct.pack("<"+"I"*(len(args)+1), STOP, *args))
    u.reg_write(reg.UC_X86_REG_ESP, sp)
    saved = [(0x8a12bbcc ^ seed ^ (i * 0x10101)) & 0xffffffff for i in range(len(SAVED))]
    for r, value in zip(SAVED, saved):
        u.reg_write(r, value)
    u.emu_start(entry, STOP, count=LIMIT)
    assert u.reg_read(reg.UC_X86_REG_EIP) == STOP, "did not return"
    assert u.reg_read(reg.UC_X86_REG_ESP) == sp + 4, "stack cleanup"
    assert [u.reg_read(r) for r in SAVED] == saved, "callee-saved registers"
    return u.reg_read(reg.UC_X86_REG_EAX) | (u.reg_read(reg.UC_X86_REG_EDX) << 32)


def layout(obj, name):
    u = machine((b"", {}), obj)
    call(u, PEER + obj[1]["layout_"+name], [DATA, DATA+256])
    size, align = struct.unpack("<II", u.mem_read(DATA, 8))
    assert 0 < size <= 128 and align <= 64
    return size, align, bytes(u.mem_read(DATA+256, size))


def replay(caller, callee, key, name, size, mask, seed, corrupt=None):
    u = machine(caller, callee)
    raw = bytearray(((seed >> ((i%4)*8)) ^ (i*13+7)) & 0x3f for i in range(size*4))
    if name == "extended" and size != 8:
        for i in range(4):
            raw[i*size:i*size+10] = struct.pack("<QH", 0x8000000000000000 | seed, 0x3fff+i)
    u.mem_write(DATA, bytes(raw))
    out, seen = DATA+4096, DATA+8192
    result = call(u, CODE+caller[1]["caller_"+key],
                  [PEER+callee[1]["callee_"+key], DATA, out, seed, seen], seed)
    got = bytes(u.mem_read(out, size*4))
    observed = struct.unpack("<I", u.mem_read(seen, 4))[0]
    if corrupt == "argument":
        got = bytes([got[0]^1])+got[1:]
    if corrupt == "return-high":
        result ^= 1<<32
    if corrupt == "trailing":
        observed ^= 0x80000000
    assert bytes(b&m for b,m in zip(got, mask*4)) == bytes(b&m for b,m in zip(raw, mask*4)), "value transport"
    assert observed == seed, "trailing argument"
    assert result == ((seed<<32)|(seed^0xa5a5a5a5)), "complete integer return"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for x in ("neverc", "clang", "llc", "output"):
        ap.add_argument("--"+x, required=True, type=Path)
    ap.add_argument("--platform", choices=tuple(TRIPLES), action="append")
    ap.add_argument("--reference-only", action="store_true")
    ap.add_argument("--case", action="append")
    a=ap.parse_args(); a.output.mkdir(parents=True, exist_ok=True)
    sources_text = sources(); records=[]; results=[]; controls=0
    for role, text in zip(("producer", "consumer"), sources_text[:2]):
        (a.output/(role+".c")).write_text(text)
    def run(cmd, stem):
        p=subprocess.run(list(map(str,cmd)),capture_output=True,text=True,timeout=120)
        (a.output/(stem+".log")).write_text(p.stdout+p.stderr)
        records.append(dict(command=list(map(str,cmd)),exit_code=p.returncode))
        assert p.returncode == 0, (stem,p.stderr)
    for platform in a.platform or TRIPLES:
        triple=TRIPLES[platform]
        for opt in ("O0","O2"):
            objects={}
            for compiler in (("clang",) if a.reference_only else ("clang","neverc")):
                common=[getattr(a,compiler),"--target="+triple,"-"+opt,"-std=gnu23","-ffreestanding","-fno-lto","-fno-stack-protector","-fno-pic","-fno-pie","-msse2","-fno-vectorize","-fno-slp-vectorize"]
                if compiler=="neverc":common += ["-fno-builtin-mimalloc","-fllvm-backend="+str(a.llc)]
                for role in ("producer","consumer"):
                    stem=f"{platform}-{compiler}-{role}-{opt}"
                    path=a.output/(stem+".o")
                    if compiler == "clang":
                        ir = path.with_suffix(".ll")
                        run(common+["-S","-emit-llvm",a.output/(role+".c"),"-o",ir],stem+"-frontend")
                        run([a.llc,"--neversight-codegen","-"+opt,"-verify-machineinstrs","-relocation-model=static","-filetype=obj",ir,"-o",path],stem)
                    else:
                        run(common+["-c",a.output/(role+".c"),"-o",path],stem)
                    objects[compiler,role]=object_code(path, PEER if role=="producer" else CODE)
            for name, _, mask_kind, _ in CASES:
                if not mask_kind.startswith("BITS") or (a.case and name not in a.case):
                    continue
                bits = int(mask_kind[4:])
                values = (3, (1<<bits)-1, 1<<(bits-1), (1<<(bits-2))+5)
                for compiler in (("clang",) if a.reference_only else ("clang", "neverc")):
                    obj = objects[compiler,"producer"]
                    size, align, mask = layout(obj,name)
                    u = machine((b"",{}),obj)
                    call(u, PEER+obj[1]["constants_"+name], [DATA])
                    got = bytes(u.mem_read(DATA,size*4))
                    expected = b"".join(v.to_bytes(size,"little") for v in values)
                    masked = lambda b: bytes(v&m for v,m in zip(b,mask*4))
                    assert masked(got)==masked(expected),(platform,opt,name,compiler,"constant array storage")
                    assert masked(bytes([got[0]^1])+got[1:])!=masked(expected)
                    controls += 1
                    results.append(dict(platform=platform,optimization=opt,case="constants_"+name,
                                        caller=compiler,callee=compiler,size=size,align=align,comparisons=4))
            for name,cc,key in sources_text[2]:
                if a.case and name not in a.case:continue
                if cc=="fastcall" and platform!="windows":continue
                expected_layout=layout(objects["clang","producer"],name)
                if not a.reference_only:
                    assert layout(objects["neverc","producer"],name)==expected_layout,(platform,name,"layout mismatch")
                size,align,mask=expected_layout
                for caller,callee in ((("clang","clang"),) if a.reference_only else
                                      (("clang","clang"),("clang","neverc"),("neverc","clang"),("neverc","neverc"))):
                    left,right=objects[caller,"consumer"],objects[callee,"producer"]
                    for seed in SEEDS:
                        try: replay(left,right,key,name,size,mask,seed)
                        except Exception as e:raise AssertionError((platform,opt,key,caller,callee,hex(seed),str(e))) from e
                    for bad in ("argument","return-high","trailing"):
                        try:replay(left,right,key,name,size,mask,SEEDS[-1],bad)
                        except AssertionError:controls+=1
                        else:raise AssertionError("corruption accepted: "+bad)
                    results.append(dict(platform=platform,optimization=opt,case=key,caller=caller,callee=callee,size=size,align=align,comparisons=len(SEEDS)))
            print(platform,opt,"passed",flush=True)
    report=dict(schema_version=1,profiles=results,comparisons=sum(p["comparisons"] for p in results),corruption_controls=controls,commands=records,
                tools={str(p):digest(p) for p in (a.neverc,a.clang,a.llc)},
                artifacts={p.name:digest(p) for p in a.output.iterdir() if p.is_file()},
                boundary=__doc__)
    (a.output/"qualification.json").write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps({k:report[k] for k in ("comparisons","corruption_controls")}))

if __name__ == "__main__":main()

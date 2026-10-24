#!/usr/bin/env python3
"""Qualify native AMX address, encoding and row-register controls.

Decode instruction bytes independently; exercise all physical tiles, address
widths, segment prefixes and VEX/EVEX controls in ELF/COFF/MachO at O0/O2.
Controlled completions check scalar addressing and memory order, not hardware
AMX arithmetic, faults or restart. No inline assembly is used by the APIs.
"""
import argparse
import importlib.util
import itertools
import struct
import tempfile
from pathlib import Path

import capstone
import unicorn
import unicorn.x86_const as regs


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


N = module("amx_native", "verify-amx-native-completion.py")
R = module("amx_rows", "verify-amx-row-completion.py")
run, text_section = N.run, N.text_section
MEMORY = {"ldtilecfg": (0x49, 0), "sttilecfg": (0x49, 1),
          "tileloadd64": (0x4b, 3), "tileloaddt164": (0x4b, 1),
          "tileloaddrs64": (0x4a, 3), "tileloaddrst164": (0x4a, 1),
          "tilestored64": (0x4b, 2)}
PREFIXES = (0, 0x26, 0x2e, 0x36, 0x3e, 0x64, 0x65)
FEATURES = "amx-tile,amx-movrs,amx-avx512,avx10.2,egpr"


def rex2_length(code, offset):
    # REX2 extends register fields; instruction length is otherwise the legacy
    # map selected by M0. Decode that map independently of extended registers.
    payload = code[offset + 1]
    legacy = bytes([0x40 | (payload & 15)]) + (b"\x0f" if payload & 128 else b"") + code[offset + 2:]
    cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    insn = next(cs.disasm(legacy, offset, count=1), None)
    assert insn, f"unknown REX2 setup {code[offset:offset + 16].hex()}"
    return insn.size + 1 - int(bool(payload & 128))


def memory_decode(code):
    cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    result, offset = {}, 0
    while offset < len(code):
        if code[offset] == 0xd5:
            offset += rex2_length(code, offset)
            continue
        start, p = offset, offset
        prefixes = []
        while code[p] in (*PREFIXES[1:], 0x67):
            prefixes.append(code[p]); p += 1
        if code[p] in (0xc4, 0x62):
            evex = code[p] == 0x62
            p0, p1 = code[p + 1:p + 3]
            op_at = p + (4 if evex else 3)
            op, modrm = code[op_at:op_at + 2]
            name = next((n for n, shape in MEMORY.items() if shape == (op, p1 & 3)), None)
            if name and p0 & (7 if evex else 31) == 2 and modrm >> 6 != 3:
                assert p0 == (0xf2 if evex else 0xe2) and p1 & (0xfc if not evex else 0xf8) == 0x78
                if evex:
                    assert code[p + 3] == 8 and p1 & 4
                tile = (modrm >> 3) & 7
                end = op_at + 2
                config = name in ("ldtilecfg", "sttilecfg")
                assert modrm & 0xc7 == (3 if config else 4)
                if not config:
                    assert code[end] == 0x0b, "base must be BX, unscaled stride CX"
                    end += 1
                else:
                    assert tile == 0
                segment = next((v for v in prefixes if v != 0x67), 0)
                assert len(prefixes) == bool(segment) + (0x67 in prefixes)
                result[start] = (name, tile, 32 if 0x67 in prefixes else 64, segment, int(evex), end)
                offset = end
                continue
        ordinary = next(cs.disasm(code[offset:], offset, count=1), None)
        assert ordinary, f"unknown scalar bytes {offset}: {code[offset:offset + 16].hex()}"
        offset += ordinary.size
    return result


def build(args, directory, stem, cases, opt, triple, rows=False, live=False):
    src, ir, obj = [directory / (stem + suffix) for suffix in (".c", ".ll", ".o")]
    declarations, body, pending = set(), [], []
    if live:
        body.append("store i64 %seed, ptr %out" if args.llc else "*out = seed;")
    for i, case in enumerate(cases):
        name = case[0]
        if rows:
            _, tile, extended = case
            ctype, vtype, *_ = R.ROWS[name]
            api, cargs = name + ".reg", f"{tile}, row, {extended}"
            types, values = ["i8", "i32", "i32"], [str(tile), "%row", str(extended)]
            declarations.add(f"typedef {ctype} V_{name} __attribute__((vector_size(64), aligned(1)));") if not args.llc else None
        else:
            _, tile, width, prefix, evex = case
            vtype, api = "void", name + ".address"
            types, values = ["i64", "i32", "i32", "i32"], ["%base", str(width), str(prefix), str(evex)]
            cargs = f"base, {width}, {prefix}, {evex}"
            if name not in ("ldtilecfg", "sttilecfg"):
                types, values = ["i8", "i64", "i64", "i32", "i32", "i32"], [str(tile), "%base", "%stride", str(width), str(prefix), str(evex)]
                cargs = f"{tile}, base, stride, {width}, {prefix}, {evex}"
        if args.llc:
            declarations.add(f"declare {vtype} @llvm.x86.{api}({', '.join(types)})")
            call = f"call {vtype} @llvm.x86.{api}({', '.join(t+' '+v for t,v in zip(types, values))})"
            body.append(f"%v{i} = {call}" if rows else call)
            if rows and live:
                (pending if live == "pressure" else body).extend([f"%p{i} = getelementptr i8, ptr %out, i64 {64 + i*64}", f"store {vtype} %v{i}, ptr %p{i}, align 1"])
        else:
            call = f"__builtin_ia32_{api.replace('.', '_')}({cargs})"
            if rows and live == "pressure":
                body.append(f"V_{name} v{i} = {call};")
                pending.append(f"*(V_{name} *)((char *)out + {64 + i*64}) = v{i};")
            else:
                body.append(f"*(V_{name} *)((char *)out + {64 + i*64}) = {call};" if rows and live else f"(void){call};")
    body += pending
    if live:
        if args.llc:
            body += ["%m = load i64, ptr %out", "%a = xor i64 %m, %seed"]
            if rows:
                body += ["%r = zext i32 %row to i64", "%b = xor i64 %a, %r", "ret i64 %b"]
            else:
                body += ["%b = xor i64 %a, %base", "%c = xor i64 %b, %stride", "ret i64 %c"]
        else:
            body.append("return *out ^ seed ^ " + ("row;" if rows else "base ^ stride;"))
    else:
        body.append("ret void" if args.llc else "return;")
    if args.llc:
        params = "ptr %out, i32 %row, i64 %seed" if rows else "i64 %base, i64 %stride, ptr %out, i64 %seed"
        ir.write_text('\n'.join(sorted(declarations)) + f'\ndefine {"i64" if live else "void"} @probe({params}) #0 {{\n'+ '\n'.join(body) + '\n}\nattributes #0 = { "target-features"="' + ','.join('+'+f for f in FEATURES.split(',')) + '" }\n')
    else:
        params = "unsigned long long *out, unsigned row, unsigned long long seed" if rows else "unsigned long long base, unsigned long long stride, unsigned long long *out, unsigned long long seed"
        src.write_text('\n'.join(sorted(declarations)) + f'\n__attribute__((target("{FEATURES}"))) {"unsigned long long" if live else "void"} probe({params}) {{\n' + '\n'.join(body) + '\n}\n')
        compiler = [args.compiler, '--target='+triple, '-'+opt, '-Werror', '-ffreestanding', '-fno-lto', '-fno-builtin-mimalloc', '-fno-stack-protector', '-mllvm', '-verify-machineinstrs', src]
        run(compiler + ['-S', '-emit-llvm', '-o', ir])
        assert ' asm ' not in ir.read_text()
        if not args.backend_llc:
            run(compiler + ['-c', '-o', obj])
    backend = args.llc or args.backend_llc
    if backend:
        command = [backend, '-mtriple='+triple, '-'+opt, '-verify-machineinstrs', ir]
        run(command + ['-filetype=obj', '-o', obj])
    if not live:
        for dialect in ('att', 'intel'):
            asm = directory / (stem+'-'+dialect+'.s')
            roundtrip = asm.with_suffix('.o')
            if backend:
                run(command + ['-filetype=asm', '-x86-asm-syntax='+dialect, '-o', asm])
                run([backend.parent/'llvm-mc', '-triple='+triple, '-filetype=obj', asm, '-o', roundtrip])
            else:
                run(compiler + ['-S', '-masm='+dialect, '-o', asm])
                run([args.compiler, '--target='+triple, '-c', asm, '-o', roundtrip])
            assert text_section(obj) == text_section(roundtrip), 'assembly roundtrip changed encoding'
    return text_section(obj)


def complete_memory(code, cases, base, stride):
    native = memory_decode(code)
    machine = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_64)
    for address in (N.CODE, N.STACK, N.DATA):
        machine.mem_map(address, N.PAGE)
    machine.mem_write(N.CODE, code)
    out, seed = N.DATA+256, 0x81234567fedcba98
    for name, value in zip(('RCX','RDX','R8','R9'), (base,stride,out,seed)):
        machine.reg_write(getattr(regs, 'UC_X86_REG_'+name),value)
    saved = {name:N.MARKER+i for i,name in enumerate(('RBX','RBP','RSI','RDI','R12','R13','R14','R15'))}
    for name,value in saved.items():
        machine.reg_write(getattr(regs,'UC_X86_REG_'+name),value)
    rsp=N.STACK+N.PAGE-72
    machine.reg_write(regs.UC_X86_REG_RSP,rsp)
    machine.mem_write(rsp,struct.pack('<Q',N.STOP))
    machine.hook_add(unicorn.UC_HOOK_CODE,lambda uc,address,size,_: uc.emu_stop() if address-N.CODE in native else None)
    pc,count=N.CODE,0
    while pc!=N.STOP:
        try:
            machine.emu_start(pc,N.STOP,count=10000)
        except unicorn.UcError:
            assert machine.reg_read(regs.UC_X86_REG_RIP)-N.CODE in native
        pc=machine.reg_read(regs.UC_X86_REG_RIP)
        if pc==N.STOP:break
        actual=native[pc-N.CODE]
        assert actual[:-1]==cases[count]
        _,_,width,_,_,end=actual
        mask=(1<<width)-1
        assert machine.reg_read(regs.UC_X86_REG_RBX)==base&mask,'base truncation changed'
        if actual[0] not in ('ldtilecfg','sttilecfg'):
            assert machine.reg_read(regs.UC_X86_REG_RCX)==stride&mask,'stride truncation changed'
        if not count:
            assert struct.unpack('<Q',machine.mem_read(out,8))[0]==seed,'ordinary store reordered'
        memory=N.MARKER^count
        machine.mem_write(out,struct.pack('<Q',memory))
        pc=N.CODE+end
        machine.reg_write(regs.UC_X86_REG_RIP,pc)
        count+=1
    assert count==len(cases),'effect removed or merged'
    assert machine.reg_read(regs.UC_X86_REG_RAX)==memory^seed^base^stride,'ordinary load reordered or input lost'
    for name,value in saved.items():
        assert machine.reg_read(getattr(regs,'UC_X86_REG_'+name))==value,'callee-saved '+name


def row_execution_decode(code):
    rows, ordinary, rex2 = decode_rows(code), {}, {}
    cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    cs.detail = True
    offset = 0
    while offset < len(code):
        if offset in rows:
            offset = rows[offset][-1]
            continue
        if code[offset] == 0xd5:
            payload = code[offset + 1]
            legacy = bytes([0x40 | (payload & 15)]) + (b"\x0f" if payload & 128 else b"") + code[offset + 2:]
            insn = next(cs.disasm(legacy, offset, count=1), None)
            assert insn
            end = offset + rex2_length(code, offset)
            rex2[offset] = (payload, insn, end)
            offset = end
            continue
        insn = next(cs.disasm(code[offset:], offset, count=1), None)
        assert insn
        ordinary[offset] = insn
        offset += insn.size
    return rows, ordinary, rex2


def complete_rows(code, calls, row_value):
    rows, ordinary, rex2 = row_execution_decode(code)
    extended = {}
    machine = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_64)
    for address in (N.CODE, N.STACK, N.DATA):
        machine.mem_map(address, N.PAGE)
    machine.mem_write(N.CODE, code)
    seed, out = 0x81234567fedcba98, N.DATA + 64
    for name, value in zip(("RCX", "RDX", "R8"), (out, row_value, seed)):
        machine.reg_write(getattr(regs, "UC_X86_REG_" + name), value)
    saved = {name: N.MARKER + i for i, name in enumerate(("RBX", "RBP", "RSI", "RDI", "R12", "R13", "R14", "R15"))}
    for name, value in saved.items():
        machine.reg_write(getattr(regs, "UC_X86_REG_" + name), value)
    rsp = N.STACK + N.PAGE - 72
    machine.reg_write(regs.UC_X86_REG_RSP, rsp)
    machine.mem_write(rsp, struct.pack("<Q", N.STOP))
    saved_xmm = {i: ((N.MARKER + i) << 64) | (seed ^ i) for i in range(6, 16)}
    bank = {i: value.to_bytes(16, "little") + bytes(48) for i, value in saved_xmm.items()}
    for i, value in saved_xmm.items():
        machine.reg_write(getattr(regs, "UC_X86_REG_XMM" + str(i)), value)
    vectors = {offset: insn for offset, insn in ordinary.items()
               if insn.mnemonic.startswith("vmov") or insn.mnemonic == "vzeroupper" or
               (insn.mnemonic.startswith("mov") and any(
                   op.type == capstone.x86.X86_OP_REG and insn.reg_name(op.reg).startswith("xmm")
                   for op in insn.operands))}
    boundaries = rows.keys() | vectors.keys() | rex2.keys()
    machine.hook_add(unicorn.UC_HOOK_CODE, lambda uc, address, size, _: uc.emu_stop()
                     if address - N.CODE in boundaries else None)

    def address(insn, operand):
        mem = operand.mem
        value = lambda reg: machine.reg_read(getattr(regs, "UC_X86_REG_" + insn.reg_name(reg).upper())) if reg else 0
        return (value(mem.base) + value(mem.index) * mem.scale + mem.disp) & N.MASK

    def read_vector(insn, operand):
        if operand.type == capstone.x86.X86_OP_MEM:
            return bytes(machine.mem_read(address(insn, operand), operand.size))
        name = insn.reg_name(operand.reg)
        assert name.startswith(("xmm", "ymm", "zmm")), name
        index = int(name[3:])
        if index in bank:
            return bank[index][:operand.size]
        assert index < 16, "uninitialized extended vector register"
        return (machine.reg_read(getattr(regs, "UC_X86_REG_XMM" + str(index))).to_bytes(16, "little") + bytes(48))[:operand.size]

    def write_vector(index, value):
        bank[index] = value.ljust(64, b"\0")
        if index < 16:
            machine.reg_write(getattr(regs, "UC_X86_REG_XMM" + str(index)), int.from_bytes(value[:16], "little"))

    def read_gpr(index):
        if index >= 16:
            assert index in extended, "uninitialized extended GPR"
            return extended[index]
        return machine.reg_read(getattr(regs, "UC_X86_REG_" + N.GPRS[index]))

    def write_gpr(index, value):
        if index >= 16:
            extended[index] = value
        else:
            machine.reg_write(getattr(regs, "UC_X86_REG_" + N.GPRS[index]), value)

    def rex2_move(payload, instruction, raw):
        assert not payload & 128 and raw[2] in (0x89, 0x8b), raw.hex()
        modrm = raw[3]
        reg = ((modrm >> 3) & 7) | ((payload & 4) << 1) | ((payload & 64) >> 2)
        rm = (modrm & 7) | ((payload & 1) << 3) | (payload & 16)
        size = 8 if payload & 8 else 4
        if modrm >> 6 == 3:
            value = read_gpr(reg if raw[2] == 0x89 else rm) & ((1 << (size * 8)) - 1)
            write_gpr(rm if raw[2] == 0x89 else reg, value)
            return
        memory = next(op.mem for op in instruction.operands if op.type == capstone.x86.X86_OP_MEM)
        def memory_gpr(register, high_bit):
            if not register:
                return 0
            name = instruction.reg_name(register).upper()
            index = N.GPRS.index(name)
            return read_gpr(index + (16 if high_bit else 0))
        addr = (memory_gpr(memory.base, payload & 16) + memory_gpr(memory.index, payload & 32) * memory.scale + memory.disp) & N.MASK
        if raw[2] == 0x89:
            machine.mem_write(addr, (read_gpr(reg) & ((1 << (size * 8)) - 1)).to_bytes(size, "little"))
        else:
            write_gpr(reg, int.from_bytes(machine.mem_read(addr, size), "little"))

    pc, count, expected = N.CODE, 0, []
    while pc != N.STOP:
        try:
            machine.emu_start(pc, N.STOP, count=10000)
        except unicorn.UcError:
            assert machine.reg_read(regs.UC_X86_REG_RIP) - N.CODE in boundaries
        pc = machine.reg_read(regs.UC_X86_REG_RIP)
        if pc == N.STOP:
            break
        offset = pc - N.CODE
        if offset in rex2:
            payload, instruction, end = rex2[offset]
            rex2_move(payload, instruction, code[offset:end])
        elif offset in rows:
            api, tile, row, zmm, end = rows[offset]
            want_api, want_tile, want_extended = calls[count]
            assert (api, tile, int(row >= 16)) == (want_api, want_tile, want_extended)
            actual_row = read_gpr(row) & 0xffffffff
            assert actual_row == row_value, "dynamic row index was truncated"
            if not count:
                assert struct.unpack("<Q", machine.mem_read(out, 8))[0] == seed
            payload = bytes((i * 37 + tile * 13 + count * 71 + actual_row) & 255 for i in range(64))
            write_vector(zmm, payload)
            expected.append(payload)
            # Expose illegal hoisting of ordinary memory across ambient AMX state.
            memory = N.MARKER ^ count
            machine.mem_write(out, struct.pack("<Q", memory))
            count += 1
        else:
            insn = vectors[offset]
            end = offset + insn.size
            if insn.mnemonic != "vzeroupper":
                assert len(insn.operands) == 2 and insn.operands[0].size in (16, 32, 64)
                dest, src = insn.operands
                payload = read_vector(insn, src)
                if dest.type == capstone.x86.X86_OP_MEM:
                    machine.mem_write(address(insn, dest), payload)
                else:
                    name = insn.reg_name(dest.reg)
                    assert name.startswith(("xmm", "ymm", "zmm")), name
                    write_vector(int(name[3:]), payload)
        pc = N.CODE + end
        machine.reg_write(regs.UC_X86_REG_RIP, pc)
    assert count == len(calls), "native row reads were removed or merged"
    for i, payload in enumerate(expected):
        assert bytes(machine.mem_read(out + 64 + i * 64, 64)) == payload, "512-bit row result was corrupted"
    assert machine.reg_read(regs.UC_X86_REG_RAX) == memory ^ seed ^ row_value
    for name, value in saved.items():
        assert machine.reg_read(getattr(regs, "UC_X86_REG_" + name)) == value, name
    for index, value in saved_xmm.items():
        assert int.from_bytes(bank[index][:16], "little") == value, "Win64 nonvolatile XMM corrupted"


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler',type=Path,default=Path('build-neverc/bin/neverc'))
    b=p.add_mutually_exclusive_group();b.add_argument('--llc',type=Path);b.add_argument('--backend-llc',type=Path)
    p.add_argument('--keep',type=Path)
    args=p.parse_args()
    for key in ('compiler','llc','backend_llc'):
        if getattr(args,key):setattr(args,key,getattr(args,key).resolve())
    with tempfile.TemporaryDirectory(prefix='amx-controls-') as temp:
        directory=args.keep or Path(temp);directory.mkdir(parents=True,exist_ok=True)
        count=0
        matrix=[(n,t,w,p,e) for n in MEMORY for t in (range(1) if n in ('ldtilecfg','sttilecfg') else range(8)) for w,p,e in itertools.product((32,64),PREFIXES,(0,1))]
        row_matrix=[(n,t,e) for n in R.ROWS for t,e in itertools.product(range(8),(0,1))]
        for opt,triple in itertools.product(('O0','O2'),N.TRIPLES):
            stem=opt+'-'+str(N.TRIPLES.index(triple))
            code=build(args,directory,'memory-'+stem,matrix,opt,triple)
            assert [v[:-1] for v in memory_decode(code).values()]==matrix
            count+=len(matrix)
            code=build(args,directory,'rows-'+stem,row_matrix,opt,triple,rows=True)
            decoded=decode_rows(code)
            assert [(a,t,int(r>=16)) for a,t,r,_,_ in decoded.values()]==row_matrix
            count+=len(row_matrix)
        for opt,width,evex in itertools.product(('O0','O2'),(32,64),(0,1)):
            cases=[(n,7 if n not in ('ldtilecfg','sttilecfg') else 0,width,p,evex) for n in MEMORY for p in PREFIXES]*2
            code=build(args,directory,f'live-{opt}-{width}-{evex}',cases,opt,N.TRIPLES[0],live=True)
            for base,stride in ((0x80000000fff00123,0x9876543280000037),(0xffffffffffffffff,0xffffffffffffffff),(N.DATA,64)):
                complete_memory(code,cases,base,stride);count+=len(cases)
        for opt, name, extended in itertools.product(('O0','O2'), R.ROWS, (0,1)):
            cases = [(name, tile, extended) for tile in range(8)] * 2
            code = build(args, directory, f'row-live-{opt}-{name}-{extended}', cases, opt, N.TRIPLES[0], rows=True, live='pressure')
            for row in (0, 0x80000010, 0xffffffff):
                complete_rows(code, cases, row)
                count += len(cases)
        print(f'PASS {count} AMX explicit control checks (controlled completion, not physical hardware)')


def decode_rows(code):
    # The row field itself is independent of the scalar REX2 setup. Capstone
    # versions without APX can still skip the exact register-register MOV form.
    rows={};offset=0
    cs=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
    while offset<len(code):
        if code[offset]==0xd5:
            offset += rex2_length(code, offset);continue
        if code[offset]==0x62:
            p0,p1,p2,opcode,modrm=code[offset+1:offset+6]
            name=next((n for n,(_,_,op,pp,_,_) in R.ROWS.items() if (opcode,p1&3)==(op,pp)),None)
            if name and p0&7==2 and modrm>>6==3:
                assert p0&0x68==0x60 and p1&0x84==4 and p2&0xf7==0x40
                tile=modrm&7;row=((~p1>>3)&15)|((~p2&8)<<1)
                zmm=((modrm>>3)&7)|((~p0&0x80)>>4)|(~p0&0x10)
                rows[offset]=(name,tile,row,zmm,offset+6);offset+=6;continue
        insn=next(cs.disasm(code[offset:],offset,count=1),None)
        assert insn,f'unknown scalar row bytes {offset}: {code[offset:offset+12].hex()}'
        offset+=insn.size
    return rows


if __name__=='__main__':main()

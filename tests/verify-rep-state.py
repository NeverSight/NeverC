#!/usr/bin/env python3
"""Check explicit REP state against hardware and a sequential memory oracle.

Linux x86-64 native mode tests memory operations, FS/GS offsets, full registers,
flags, and SIGSEGV restart. Port tests use controlled signal-context completion;
no host port I/O is performed. The host compiler builds the independent raw REP
reference and signal adapter. Requires capstone and lief.
"""
import argparse
import ctypes as ct
from dataclasses import dataclass
import itertools
import platform
import subprocess
import tempfile
from pathlib import Path

import capstone
import lief

PAGE = 4096
FLAG_MASK = 0xD501
U64 = ct.c_uint64
SENTINEL = 0xBADC0FFEE0DDF00D
SOURCES = ('movs', 'lods', 'cmps', 'outs')
COMPARES = ('cmps', 'scas')
OPS = ('movs', 'stos', 'lods', 'cmps', 'scas', 'ins', 'outs')


@dataclass(frozen=True)
class Form:
    op: str
    width: int
    address: int
    segment: int
    repeat: int

    @property
    def name(self):
        return f'rep_{self.op}_{self.width}_{self.address}_{self.segment}_{self.repeat}'

    @property
    def words(self):
        return 3 if self.op in ('movs', 'lods', 'cmps') else 2

    @property
    def compare(self):
        return self.op in COMPARES

    @property
    def port(self):
        return self.op in ('ins', 'outs')

    @property
    def controls(self):
        return [self.width, self.address] + ([self.segment] if self.op in SOURCES else []) + \
               ([self.repeat] if self.compare else [])


def forms(bits):
    for op in OPS:
        for width, address, segment, repeat in itertools.product(
                (1, 2, 4) if bits == 32 or op in ('ins', 'outs') else (1, 2, 4, 8),
                (32, 64) if bits == 64 else (16, 32),
                (0, 0x26, 0x2e, 0x36, 0x3e, 0x64, 0x65) if op in SOURCES else (0,),
                (0, 1) if op in COMPARES else (0,)):
            yield Form(op, width, address, segment, repeat)


def run(command):
    result = subprocess.run(list(map(str, command)), text=True, capture_output=True)
    if result.returncode:
        raise AssertionError(f'{command}:\n{result.stdout}\n{result.stderr}')
    return result


def wrapper_ir(form, bits):
    word = f'i{bits}'
    types = [word, word, 'i16' if form.port else word, 'i8'] + (['i16'] if form.compare else [])
    results = [word] * form.words + (['i16'] if form.compare else [])
    result_type = '{ ' + ', '.join(results) + ' }'
    api = f'llvm.x86.rep.{form.op}.state' + ('32' if bits == 32 else '')
    declaration = f'declare {result_type} @{api}(' + ', '.join(types + ['i32'] * len(form.controls)) + ')'
    body, arguments = [], []
    for i, ty in enumerate(types):
        body += [f'%p{i} = getelementptr {word}, ptr %input, i32 {i}',
                 f'%v{i} = load {word}, ptr %p{i}']
        if ty != word:
            body += [f'%a{i} = trunc {word} %v{i} to {ty}']
        arguments.append(f'{ty} %' + ('v' if ty == word else 'a') + str(i))
    arguments += [f'i32 {value}' for value in form.controls]
    body += [f'%r = call {result_type} @{api}(' + ', '.join(arguments) + ')']
    for i, ty in enumerate(results):
        index = 3 if ty == 'i16' else i
        body += [f'%r{i} = extractvalue {result_type} %r, {i}']
        if ty != word:
            body += [f'%w{i} = zext {ty} %r{i} to {word}']
        body += [f'%o{i} = getelementptr {word}, ptr %output, i32 {index}',
                 f'store {word} %' + ('r' if ty == word else 'w') + str(i) + f', ptr %o{i}']
    return declaration, f'define void @{form.name}(ptr %input, ptr %output) {{\n  ' + '\n  '.join(body) + '\n  ret void\n}\n'


def wrapper_c(form):
    args = ['in[0]', 'in[1]', 'in[2]', 'in[3]'] + (['in[4]'] if form.compare else [])
    args += list(map(str, form.controls)) + ['out'] + (['&flags'] if form.compare else [])
    return '__attribute__((target("sahf"))) void ' + form.name + \
        '(const unsigned long long *in, unsigned long long *out) {\n' + \
        ('unsigned short flags;\n' if form.compare else '') + \
        f'__builtin_ia32_rep_{form.op}_state(' + ', '.join(args) + ');\n' + \
        ('out[3] = flags;\n' if form.compare else '') + '}\n'


def reference_c(form):
    if form.port:
        return ''
    registers = ('si', 'di', 'cx') if form.op in ('movs', 'cmps') else \
                ('si', 'cx', 'ax') if form.op == 'lods' else ('di', 'cx', 'ax')
    constraints = {'si': 'S', 'di': 'D', 'cx': 'c', 'ax': 'a'}
    setup = ', '.join(f'{name} = in[{i}]' for i, name in enumerate(registers))
    outputs = ', '.join(f'"+&{constraints[name]}"({name})' for name in registers)
    # Encode the independent hardware reference directly. GNU as rejects
    # standalone ES/SS prefixes in long mode even though the CPU accepts them.
    opcode = {'movs': 0xa4, 'stos': 0xaa, 'lods': 0xac,
              'cmps': 0xa6, 'scas': 0xae}[form.op] + (form.width != 1)
    encoding = ([0x67] if form.address == 32 else []) + \
        ([form.segment] if form.segment else []) + [0xf2 if form.repeat else 0xf3] + \
        ([0x66] if form.width == 2 else [0x48] if form.width == 8 else []) + [opcode]
    instruction = '.byte ' + ', '.join(hex(byte) for byte in encoding)
    result = '\n'.join(f'out[{i}] = {name};' for i, name in enumerate(registers[:form.words]))
    if form.compare:
        result += '\nout[3] = ((after & 0xd5) << 8) | ((after >> 11) & 1);'
    return f'void ref_{form.name}(const unsigned long long *in, unsigned long long *out) {{\n' + \
        f'unsigned long long {setup}, after;\n' + \
        'unsigned long long before = ((in[4] >> 8) & 0xd5) | ((in[4] & 1) << 11) | (in[3] ? 0x400 : 0) | 0x202;\n' + \
        f'__asm__ volatile("pushq %[before]; popfq; {instruction}; pushfq; popq %[after]; cld"\n' + \
        f': {outputs}, [after] "=&r"(after) : [before] "r"(before) : "cc", "memory");\n{result}\n}}\n'


def compile_forms(args, directory, matrix, bits, opt):
    stem = directory / f'rep_{bits}_{opt}'
    ir, obj = stem.with_suffix('.ll'), stem.with_suffix('.o')
    if args.llc:
        pieces = [wrapper_ir(form, bits) for form in matrix]
        ir.write_text('\n'.join(sorted({item[0] for item in pieces})) + '\n' + '\n'.join(item[1] for item in pieces))
    else:
        source = stem.with_suffix('.c')
        source.write_text('\n'.join(wrapper_c(form) for form in matrix))
        command = [args.compiler, '--target=x86_64-unknown-linux-gnu', '-'+opt,
                   '-ffreestanding', '-fno-builtin-mimalloc', '-fno-lto', '-fPIC',
                   '-fno-stack-protector', '-mno-relax-all', '-mllvm',
                   '-verify-machineinstrs', source]
        run(command + ['-S', '-emit-llvm', '-o', ir])
        text = ir.read_text()
        assert ' asm ' not in text
        for op in OPS:
            assert f'@llvm.x86.rep.{op}.state(' in text
        if not args.backend_llc:
            run(command + ['-c', '-o', obj])
    backend = args.llc or args.backend_llc
    triple = ('x86_64' if bits == 64 else 'i386') + '-unknown-linux-gnu'
    if backend:
        backend_command = [backend, '-mtriple=' + triple, '-mattr=+sahf', '-'+opt,
                           '-relocation-model=pic', '-verify-machineinstrs', ir]
        run(backend_command + ['-filetype=obj', '-o', obj])
    binary = lief.parse(str(obj))
    text = binary.get_section('.text')
    for dialect in ('att', 'intel'):
        assembly = directory / f'rep_{bits}_{opt}_{dialect}.s'
        assembled = assembly.with_suffix('.o')
        if backend:
            run(backend_command + ['-filetype=asm', '-x86-asm-syntax=' + dialect,
                                   '-o', assembly])
            run([Path(backend).parent / 'llvm-mc', '-triple=' + triple,
                 '-filetype=obj', assembly, '-o', assembled])
        else:
            run(command + ['-S', '-masm=' + dialect, '-o', assembly])
            # Match the explicit relaxation policy used for C compilation;
            # its O0 default differs from assembly-only input.
            run([args.compiler, '--target=' + triple, '-c', '-fno-lto',
                 '-mno-relax-all',
                 '-fno-builtin-mimalloc', assembly, '-o', assembled])
        reassembled = lief.parse(str(assembled))
        assembled_text = reassembled.get_section('.text')
        # CPU-specific inter-function NOP padding is outside each operation's
        # contract. Compare complete function bodies, including flag setup.
        for form in matrix:
            before = binary.get_symbol(form.name)
            after = reassembled.get_symbol(form.name)
            assert bytes(text.content[before.value:before.value + before.size]) == \
                bytes(assembled_text.content[after.value:after.value + after.size]), \
                (bits, opt, dialect, form, 'assembly round trip changed function bytes')
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64 if bits == 64 else capstone.CS_MODE_32)
    decoder.detail = True
    native = {}
    for form in matrix:
        symbol = binary.get_symbol(form.name)
        code = bytes(text.content[symbol.value:symbol.value + symbol.size])
        instructions = list(decoder.disasm(code, 0))
        rep = [i for i in instructions if i.mnemonic.startswith(('rep ', 'repe ', 'repne ')) and form.op in i.mnemonic]
        assert len(rep) == 1, (form, [(i.mnemonic, i.op_str) for i in instructions])
        inst = rep[0]
        assert inst.addr_size * 8 == form.address, form
        assert bytes(inst.bytes).count(0xf2 if form.repeat else 0xf3) == 1, form
        if form.segment and form.segment != 0x3e:
            assert form.segment in inst.bytes, form
        memory = [operand for operand in inst.operands if operand.type == capstone.x86.X86_OP_MEM]
        assert memory and all(operand.size == form.width for operand in memory), form
        native[form] = (code, inst.address, inst.size)
    return obj, native


def flags_sub(left, right, width):
    mask = (1 << (8 * width)) - 1
    sign = 1 << (8 * width - 1)
    result = (left - right) & mask
    return ((int(left < right) | (int((result & 255).bit_count() % 2 == 0) << 2) |
             (int(bool((left ^ right ^ result) & 16)) << 4) | (int(result == 0) << 6) |
             (int(bool(result & sign)) << 7)) << 8) | int(bool((left ^ right) & (left ^ result) & sign))


def memory_oracle(form, memory, source, destination, count, accumulator, backward, flags):
    step, done = (-form.width if backward else form.width), 0
    for index in range(count):
        si, di = source + index * step, destination + index * step
        if form.op in SOURCES:
            left = int.from_bytes(memory[si:si + form.width], 'little')
        if form.op in COMPARES:
            right = int.from_bytes(memory[di:di + form.width], 'little')
        if form.op == 'movs':
            memory[di:di + form.width] = memory[si:si + form.width]
        elif form.op == 'stos':
            memory[di:di + form.width] = (accumulator & ((1 << (form.width * 8)) - 1)).to_bytes(form.width, 'little')
        elif form.op == 'lods':
            accumulator = left if form.width >= 4 else (accumulator & ~((1 << (form.width * 8)) - 1)) | left
        elif form.compare:
            flags = flags_sub(left if form.op == 'cmps' else accumulator & ((1 << (form.width * 8)) - 1), right, form.width)
        done += 1
        if form.compare and bool(flags & 0x4000) == bool(form.repeat):
            break
    return done, accumulator, flags & FLAG_MASK


class Fault(ct.Structure):
    _fields_ = [(name, U64) for name in
                'pc size page page_size controlled si di cx ax flags dx faults final_si final_di final_cx'.split()]


def native_suite(args, directory, matrix, obj, code):
    reference = directory / (obj.stem + '_reference.c')
    reference.write_text('\n'.join(reference_c(form) for form in matrix))
    library = directory / (obj.stem + '.so')
    run([args.host_cc, '-shared', '-fPIC', '-O2', '-mno-red-zone', '-fno-stack-protector',
         '-Wl,-z,noexecstack', reference, Path(__file__).parent / 'Inputs/X86/RepStateHost.c', obj, '-o', library])
    lib = ct.CDLL(str(library))
    lib.rep_invoke.argtypes = [ct.c_void_p, ct.POINTER(U64), ct.POINTER(U64), ct.c_uint, U64]
    lib.rep_invoke.restype = U64
    lib.rep_fault_invoke.argtypes = [ct.c_void_p, ct.POINTER(U64), ct.POINTER(U64), ct.POINTER(Fault)]
    lib.rep_fault_invoke.restype = U64
    libc = ct.CDLL(None, use_errno=True)
    libc.mmap.argtypes = [ct.c_void_p, ct.c_size_t, ct.c_int, ct.c_int, ct.c_int, ct.c_long]
    libc.mmap.restype = ct.c_void_p
    libc.mprotect.argtypes = [ct.c_void_p, ct.c_size_t, ct.c_int]
    libc.munmap.argtypes = [ct.c_void_p, ct.c_size_t]
    base = libc.mmap(None, 4 * PAGE, 3, 0x62, -1, 0)
    assert base not in (None, ct.c_void_p(-1).value) and base + 4 * PAGE < 1 << 32
    initial = bytes((i * 37 + 19) & 255 for i in range(4 * PAGE))
    cases = restarts = ports = 0
    try:
        for form in matrix:
            function = getattr(lib, form.name)
            if form.port:
                # CPL3 raises #GP before real port I/O. The handler supplies
                # distinctive natural-width completion registers and resumes.
                for backward in (0, 1):
                    a, count = base + 128, 3
                    values = (U64 * 5)(a, count, 0xf123, backward, 0)
                    output = (U64 * 4)(*[SENTINEL] * 4)
                    fault = Fault(pc=ct.cast(function, ct.c_void_p).value + code[form][1],
                                  size=code[form][2], controlled=1 if form.op == 'ins' else 2,
                                  final_si=0x123456789ABCDEF0, final_di=0x876543210ABCDEF0,
                                  final_cx=0x0123456789ABCDEF)
                    returned = lib.rep_fault_invoke(function, values, output, ct.byref(fault))
                    assert returned != (1 << 64) - 1 and not returned & 0x400, form
                    assert fault.faults == 1 and fault.cx == count and fault.dx & 0xffff == 0xf123, form
                    assert (fault.di if form.op == 'ins' else fault.si) == a, form
                    assert bool(fault.flags & 0x400) == bool(backward), form
                    assert list(output) == [fault.final_di if form.op == 'ins' else fault.final_si,
                                            fault.final_cx, SENTINEL, SENTINEL], (form, list(output))
                    ports += 1
                continue
            reference_function = getattr(lib, 'ref_' + form.name)
            for count, backward, pattern in itertools.product((0, 1, 7), (0, 1), ('unequal', 'equal', 'late', 'overlap', 'unmapped')):
                if pattern == 'unmapped' and count:
                    continue
                source, destination = PAGE + 128, 2 * PAGE + 128
                if pattern == 'overlap':
                    destination = source + 1
                data = bytearray(initial)
                accumulator = 0xD7E6F5A493827160
                step = -form.width if backward else form.width
                if form.compare and pattern in ('equal', 'late'):
                    for index in range(count):
                        si, di = source + index * step, destination + index * step
                        value = data[si:si + form.width] if form.op == 'cmps' else \
                                (accumulator & ((1 << (8 * form.width)) - 1)).to_bytes(form.width, 'little')
                        data[di:di + form.width] = value
                    if pattern == 'late' and count:
                        data[destination + (count - 1) * step] ^= 0x80
                segment_base = base + PAGE if form.segment in (0x64, 0x65) else 0
                si, di = base + source - segment_base, base + destination
                counter = count
                if form.address == 32:
                    si |= 0x1234567800000000
                    di |= 0x2345678900000000
                    counter |= 0x3456789A00000000
                if pattern == 'unmapped':
                    si, di = 0xDEADBEEF11112222, 0xFEDCBA9822223333
                inputs = [si, di, counter] if form.op in ('movs', 'cmps') else \
                         [si, counter, accumulator] if form.op == 'lods' else [di, counter, accumulator]
                # Exhaust all 64 arithmetic-flag combinations at zero count.
                old_flags = [sum((bit >> i & 1) << pos for i, pos in enumerate((0, 8, 10, 12, 14, 15)))
                             for bit in range(64)] if form.compare and count == 0 and pattern == 'unequal' else [FLAG_MASK]
                for flags in old_flags:
                    values = (U64 * 5)(*inputs, backward * 0xff, flags | 0x2afe)
                    expected_memory = bytearray(data)
                    done, ax, expected_flags = memory_oracle(form, expected_memory, source, destination,
                                                             count, accumulator, backward, values[4])
                    actuals = []
                    for target in (reference_function, function):
                        ct.memmove(base, bytes(data), len(data))
                        output = (U64 * 4)(*[SENTINEL] * 4)
                        returned = lib.rep_invoke(target, values, output,
                                                  form.segment if segment_base else 0, segment_base)
                        assert returned != (1 << 64) - 1 and not returned & 0x400, (form, 'DF leaked')
                        actuals.append((list(output), ct.string_at(base, len(data))))
                    assert actuals[0] == actuals[1], (form, count, backward, pattern, flags, actuals[0][0], actuals[1][0])
                    output, memory = actuals[1]
                    assert memory == bytes(expected_memory), (form, count, backward, pattern, 'memory')
                    remaining = output[2 if form.op in ('movs', 'cmps') else 1]
                    assert remaining & ((1 << form.address) - 1) == count - done, (form, count, backward, pattern, 'count', remaining, done)
                    if form.op == 'lods':
                        assert output[2] == ax, (form, count, backward, 'accumulator')
                    if form.compare:
                        assert output[3] == expected_flags, (form, count, backward, pattern, 'flags', output[3], expected_flags)
                    cases += 1
            # Fault after reaching the next page; compare flags must revert to
            # the input flags, and resuming the same PC completes the operation.
            if form.segment or form.address != 64:
                continue
            for backward in (0, 1):
                data = bytearray([0x4d] * (4 * PAGE))
                first = 2 * PAGE if backward else PAGE - form.width
                other = 3 * PAGE + 128
                source = first if form.op in SOURCES else other
                destination = other if form.op in SOURCES else first
                accumulator = int.from_bytes(bytes([0x4d]) * 8, 'little')
                if form.compare and form.repeat:
                    for index in range(len(data)):
                        if index >= 3 * PAGE:
                            data[index] ^= 1
                    if form.op == 'scas':
                        accumulator ^= 0x0101010101010101
                inputs = [base + source, base + destination, 2] if form.op in ('movs', 'cmps') else \
                         [base + source, 2, accumulator] if form.op == 'lods' else [base + destination, 2, accumulator]
                values = (U64 * 5)(*inputs, backward, FLAG_MASK)
                expected_memory = bytearray(data)
                done, ax, expected_flags = memory_oracle(form, expected_memory, source, destination,
                                                         2, accumulator, backward, FLAG_MASK)
                assert done == 2
                ct.memmove(base, bytes(data), len(data))
                assert libc.mprotect(base + PAGE, PAGE, 0) == 0
                fault = Fault(pc=ct.cast(function, ct.c_void_p).value + code[form][1],
                              size=code[form][2], page=base + PAGE, page_size=PAGE)
                output = (U64 * 4)(*[SENTINEL] * 4)
                returned = lib.rep_fault_invoke(function, values, output, ct.byref(fault))
                assert returned != (1 << 64) - 1 and not returned & 0x400, form
                assert fault.faults == 1 and fault.cx in (1, 2), (form, 'restart count', fault.cx)
                step = -form.width if backward else form.width
                completed = 2 - fault.cx
                if form.op in SOURCES:
                    assert fault.si == base + source + completed * step, form
                if form.op != 'lods':
                    assert fault.di == base + destination + completed * step, form
                if form.compare:
                    assert (((fault.flags & 0xd5) << 8) | ((fault.flags >> 11) & 1)) == FLAG_MASK, (form, 'fault flags', hex(fault.flags))
                    assert output[3] == expected_flags, form
                assert bool(fault.flags & 0x400) == bool(backward), form
                if form.op == 'lods':
                    assert output[2] == ax, form
                assert output[2 if form.op in ('movs', 'cmps') else 1] == 0, form
                assert ct.string_at(base, len(data)) == bytes(expected_memory), (form, 'restarted memory')
                restarts += 1
    finally:
        libc.munmap(base, 4 * PAGE)
    return cases, restarts, ports


def emulated32_suite(matrix, code):
    import struct
    import unicorn
    import unicorn.x86_const as regs

    code_base, stack, state, gdt = 0x100000, 0x180000, 0x40000, 0x1b0000
    data_base, size = 0x2000, 4 * PAGE
    stop = code_base + 0xfff
    sentinel = SENTINEL & 0xffffffff
    pack = lambda values: struct.pack('<' + 'I' * len(values), *values)
    def descriptor(base, access):
        return struct.pack('<Q', 0xffff | ((base & 0xffffff) << 16) |
                           (access << 40) | (0xf << 48) | (0xc << 52) | ((base >> 24) << 56))
    cases = ports = 0
    for form in matrix:
        raw, offset, length = code[form]
        for count, backward, pattern in itertools.product((0, 1, 7), (0, 1), ('unequal', 'equal', 'late', 'overlap')):
            machine = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
            for address, length_map in ((code_base, PAGE), (stack, PAGE), (state, PAGE),
                                        (gdt, PAGE), (data_base, size)):
                machine.mem_map(address, length_map)
            machine.mem_write(code_base, raw)
            machine.mem_write(gdt, bytes(8) + descriptor(0, 0x9b) + descriptor(0, 0x93) +
                              descriptor(data_base, 0x93) + descriptor(data_base, 0x93))
            machine.reg_write(regs.UC_X86_REG_GDTR, (0, gdt, 39, 0))
            for reg, selector in ((regs.UC_X86_REG_CS, 8), (regs.UC_X86_REG_DS, 16),
                                   (regs.UC_X86_REG_ES, 16), (regs.UC_X86_REG_SS, 16),
                                   (regs.UC_X86_REG_FS, 24), (regs.UC_X86_REG_GS, 32)):
                machine.reg_write(reg, selector)
            source, destination = PAGE + 128, 2 * PAGE + 128
            if pattern == 'overlap':
                destination = source + 1
            memory = bytearray((i * 37 + 19) & 255 for i in range(size))
            accumulator = 0x93827160
            step = -form.width if backward else form.width
            if form.compare and pattern in ('equal', 'late'):
                for index in range(count):
                    si, di = source + index * step, destination + index * step
                    value = memory[si:si + form.width] if form.op == 'cmps' else \
                            (accumulator & ((1 << (form.width * 8)) - 1)).to_bytes(form.width, 'little')
                    memory[di:di + form.width] = value
                if pattern == 'late' and count:
                    memory[destination + (count - 1) * step] ^= 0x80
            machine.mem_write(data_base, bytes(memory))
            segment_base = data_base if form.segment in (0x64, 0x65) else 0
            si, di, cx = data_base + source - segment_base, data_base + destination, count
            if form.address == 16:
                si |= 0x12340000
                di |= 0x23450000
                cx |= 0x34560000
            inputs = [si, di, cx] if form.op in ('movs', 'cmps') else \
                     [si, cx, accumulator] if form.op == 'lods' else \
                     [si if form.op == 'outs' else di, cx, 0xf123 if form.port else accumulator]
            values = inputs + [255 if backward else 0, FLAG_MASK | 0x2afe]
            machine.mem_write(state, pack(values))
            machine.mem_write(state + 128, pack([sentinel] * 4))
            sp = stack + PAGE - 32
            machine.reg_write(regs.UC_X86_REG_ESP, sp)
            machine.mem_write(sp, pack([stop, state, state + 128]))
            saved = {reg: 0x87654321 + i for i, reg in enumerate((regs.UC_X86_REG_EBX,
                      regs.UC_X86_REG_EBP, regs.UC_X86_REG_ESI, regs.UC_X86_REG_EDI))}
            for reg, value in saved.items():
                machine.reg_write(reg, value)
            machine.emu_start(code_base, code_base + offset, count=1000)
            assert machine.reg_read(regs.UC_X86_REG_EIP) == code_base + offset, form
            assert machine.reg_read(regs.UC_X86_REG_ECX) == cx, (form, 'input count')
            if form.op in SOURCES:
                assert machine.reg_read(regs.UC_X86_REG_ESI) == si, (form, 'input source')
            if form.op not in ('lods', 'outs'):
                assert machine.reg_read(regs.UC_X86_REG_EDI) == di, (form, 'input destination')
            assert bool(machine.reg_read(regs.UC_X86_REG_EFLAGS) & 0x400) == bool(backward), form
            if form.compare:
                flags = machine.reg_read(regs.UC_X86_REG_EFLAGS)
                assert (((flags & 0xd5) << 8) | ((flags >> 11) & 1)) == FLAG_MASK, form
            if form.port:
                assert machine.reg_read(regs.UC_X86_REG_DX) == 0xf123, form
                machine.reg_write(regs.UC_X86_REG_EDI if form.op == 'ins' else regs.UC_X86_REG_ESI, 0xa1b2c3d4)
                machine.reg_write(regs.UC_X86_REG_ECX, 0x98765432)
                machine.reg_write(regs.UC_X86_REG_EIP, code_base + offset + length)
                expected = [0xa1b2c3d4, 0x98765432, sentinel, sentinel]
                ports += 1
            else:
                done, ax, flags = memory_oracle(form, memory, source, destination, count,
                                                 accumulator, backward, FLAG_MASK)
                mask = (1 << form.address) - 1
                update = lambda value, delta: (value & ~mask) | ((value + delta) & mask)
                final_si, final_di, final_cx = update(si, done * step), update(di, done * step), update(cx, -done)
                expected = [final_si, final_di, final_cx] if form.op in ('movs', 'cmps') else \
                           [final_si, final_cx, ax] if form.op == 'lods' else [final_di, final_cx, sentinel]
                expected += [flags if form.compare else sentinel]
                cases += 1
            machine.emu_start(machine.reg_read(regs.UC_X86_REG_EIP), stop, count=10000)
            assert machine.reg_read(regs.UC_X86_REG_EIP) == stop, form
            actual = list(struct.unpack('<4I', machine.mem_read(state + 128, 16)))
            assert actual == expected, (form, count, backward, pattern, actual, expected)
            assert not machine.reg_read(regs.UC_X86_REG_EFLAGS) & 0x400, form
            for reg, value in saved.items():
                assert machine.reg_read(reg) == value, (form, 'callee saved', reg)
            assert bytes(machine.mem_read(data_base, size)) == bytes(memory), (form, 'memory')
    return cases, ports


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, default=Path('build-neverc/bin/neverc'))
    parser.add_argument('--llc', type=Path)
    parser.add_argument('--backend-llc', type=Path)
    parser.add_argument('--host-cc', default='cc')
    parser.add_argument('--emulate-32', action='store_true', help='full LLVM 32-bit code in Unicorn, including addr16')
    args = parser.parse_args()
    if not args.emulate_32 and (platform.system() != 'Linux' or platform.machine() not in ('x86_64', 'AMD64')):
        raise SystemExit('native REP oracle requires Linux x86-64')
    totals = [0, 0, 0]
    with tempfile.TemporaryDirectory(prefix='rep-state-') as temporary:
        directory = Path(temporary)
        for opt in ('O0', 'O2'):
            if args.emulate_32:
                if not args.llc:
                    parser.error('--emulate-32 requires --llc')
                matrix = list(forms(32))
                obj, code = compile_forms(args, directory, matrix, 32, opt)
                counts = emulated32_suite(matrix, code)
                print(f'{opt}: {counts[0]} emulated 32-bit memory cases, {counts[1]} controlled port cases', flush=True)
                continue
            matrix = list(forms(64))
            obj, code = compile_forms(args, directory, matrix, 64, opt)
            counts = native_suite(args, directory, matrix, obj, code)
            totals = [a + b for a, b in zip(totals, counts)]
            print(f'{opt}: {counts[0]} memory/flags cases, {counts[1]} hardware fault restarts, '
                  f'{counts[2]} controlled port completions', flush=True)
    if args.emulate_32:
        print('PASS: 32-bit emulation; not hardware restart qualification')
        return
    print(f'PASS: {totals[0]} memory/flags cases, {totals[1]} hardware fault restarts, '
          f'{totals[2]} controlled port completions; no host port I/O')


if __name__ == '__main__':
    main()

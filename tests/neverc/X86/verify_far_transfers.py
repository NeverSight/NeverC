#!/usr/bin/env python3
"""Qualify native far CALL/JMP builtins, including the returning compiler ABI."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent
IMAGE = 0x9000
PROFILES = tuple(f'{compiler}-O{level}' for compiler in
                 ('llvm', 'neverc', 'neverc-ir') for level in (0, 2))


def sources(mode):
    ir, c = '', ''
    word, cword = f'i{64 if mode == 64 else 32}', 'unsigned long long' if mode == 64 else 'unsigned'
    operations = {}
    for op in ('lcall', 'ljmp'):
        for immediate in (False, True):
            if immediate and mode == 64:
                continue
            for bits in ((16, 32, 64) if mode == 64 else (16, 32)):
                name = f'{op}{bits}' + ('_imm' if immediate else '')
                operations[name] = (bits, int(op == 'lcall'), int(immediate))
                intrinsic = name.replace('_', '.')
                result = f'i{bits}' if op == 'lcall' else 'void'
                signature = f'i{bits}, i16' if immediate else 'ptr'
                operands = f'i{bits} 6144, i16 48' if immediate else 'ptr %target'
                arguments = '6144, 48' if immediate else 'target'
                if op == 'ljmp':
                    signature += ', ptr'; operands += ', ptr %state'; arguments += ', state'
                ir += f'declare {result} @llvm.x86.{intrinsic}({signature})\n'
                variants = ['fixed', 'dynamic'] + (['local'] if op == 'ljmp' and not immediate else [])
                for variant in variants:
                    dynamic = variant == 'dynamic'
                    function = f'generated_{name}_{variant}'
                    return_type = word if op == 'lcall' else 'void'
                    ir += f'define {return_type} @{function}(ptr %target, ptr %state, {word} %size) {{\n'
                    c += f'{cword if op == "lcall" else "void"} {function}(const void *target, {cword} *state, {cword} size) {{\n'
                    ir += f'  %n = add {word} %size, 32\n  %local = alloca i8, {word} '+('%n' if dynamic else '512')+', align 32\n  store volatile i8 17, ptr %local\n'
                    c += '  volatile unsigned char local['+('size + 32' if dynamic else '512')+'] __attribute__((aligned(32))); local[0] = 17;\n'
                    if op == 'lcall':
                        for i in range(8):
                            ir += f'  %p{i} = getelementptr {word}, ptr %state, i32 {i}\n  %v{i} = load {word}, ptr %p{i}\n'
                            c += f'  {cword} v{i} = state[{i}];\n'
                        if mode == 64:
                            ir += '  %dp = getelementptr i8, ptr %state, i32 128\n  %fp = load double, ptr %dp\n'
                            c += '  double fp = *(const double *)((const char *)state + 128);\n'
                        ir += f'  %result = call {result} @llvm.x86.{intrinsic}({operands})\n'
                        c += f'  {cword} result = __builtin_ia32_{name}({arguments});\n'
                        value = '%result'
                        if bits != (64 if mode == 64 else 32):
                            ir += f'  %extended = zext {result} %result to {word}\n'; value = '%extended'
                        for i in range(8):
                            ir += f'  %sum{i} = add {word} {value}, %v{i}\n'; value = f'%sum{i}'
                        if mode == 64:
                            ir += '  %scaled = fmul double %fp, 2.0\n  %converted = fptoui double %scaled to i64\n'
                            ir += f'  %fpadded = add i64 {value}, %converted\n'
                            value = '%fpadded'
                        ir += f'  %answer = add {word} {value}, %size\n  ret {word} %answer\n'
                        c += '  return result + '+' + '.join(f'v{i}' for i in range(8))+' + size'+(' + (unsigned long long)(fp * 2.0)' if mode == 64 else '')+';\n'
                    else:
                        jump_operands, jump_arguments = operands, arguments
                        if variant == 'local':
                            ir += (f'  %target_copy = alloca [{bits//8+2} x i8], align 1\n'
                                   f'  %offset = load i{bits}, ptr %target, align 1\n'
                                   f'  %selector_ptr = getelementptr i8, ptr %target, i32 {bits//8}\n'
                                   '  %selector = load i16, ptr %selector_ptr, align 1\n'
                                   f'  store i{bits} %offset, ptr %target_copy, align 1\n'
                                   f'  %copy_selector = getelementptr i8, ptr %target_copy, i32 {bits//8}\n'
                                   '  store i16 %selector, ptr %copy_selector, align 1\n')
                            ty = {16:'unsigned short', 32:'unsigned', 64:'unsigned long long'}[bits]
                            c += (f'  struct __attribute__((packed)) Far {{ {ty} offset; unsigned short selector; }};\n'
                                  '  struct Far target_copy = *(const struct Far *)target;\n')
                            jump_operands = operands.replace('%target', '%target_copy')
                            jump_arguments = arguments.replace('target', '&target_copy')
                        ir += f'  call void @llvm.x86.{intrinsic}({jump_operands})\n  unreachable\n'
                        c += f'  __builtin_ia32_{name}({jump_arguments});\n'
                    ir += '}\n'; c += '}\n'
    return ir, c, operations


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--llvm-bin', type=Path, required=True)
    parser.add_argument('--neverc', type=Path, required=True)
    parser.add_argument('--mode', type=int, choices=(16, 32, 64), required=True)
    parser.add_argument('--x32', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--profile', choices=PROFILES, action='append')
    args = parser.parse_args()
    if args.x32 and args.mode != 64: parser.error('--x32 requires --mode=64')
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    records = []
    tools = {name: shutil.which(name) for name in ('clang', 'ld.lld', 'objcopy')}
    tools.update({name: str((args.llvm_bin/name).resolve())
                  for name in ('llc', 'opt', 'llvm-objdump')})
    tools['neverc'] = str(args.neverc.resolve())
    if not all(tools.values()): parser.error('clang, ld.lld, objcopy are required')

    def run(command, stem, expected=0):
        command = list(map(str, command))
        r = subprocess.run(command, capture_output=True, text=True, timeout=240,
                           env=dict(os.environ, TMPDIR=str(out)))
        (out/f'{stem}.stdout').write_text(r.stdout)
        (out/f'{stem}.stderr').write_text(r.stderr)
        records.append(dict(command=command, exit_code=r.returncode,
                            stdout=f'{stem}.stdout', stderr=f'{stem}.stderr'))
        (out/'commands.json').write_text(json.dumps(records, indent=2)+'\n')
        if r.returncode != expected:
            raise RuntimeError(f'{stem}: exit {r.returncode}\n{r.stderr}')
        return r.stdout

    ir, c, operations = sources(args.mode)
    (out/'input.ll').write_text(ir); (out/'input.c').write_text(c)
    (out/'guest.ld').write_text('ENTRY(generated_lcall16_fixed)\nSECTIONS {\n'
        f'. = {IMAGE}; .text : {{ *(.text*) }} .rodata : {{ *(.rodata*) }} '
        '.data : { *(.data*) } .bss : { *(.bss*) }\n'
        '/DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) } }\n')
    runner = out/'runner'
    run([tools['clang'], '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
         ROOT/'far_transfer_kvm.c', '-o', runner], 'host-build')
    triple = ('x86_64-unknown-linux-gnux32' if args.x32 else
              'x86_64-unknown-linux-gnu' if args.mode == 64 else
              'i386-unknown-linux-code16' if args.mode == 16 else 'i386-unknown-linux-gnu')
    results = []
    profiles = args.profile or (PROFILES[:2] if args.x32 or args.mode == 16 else PROFILES)
    if (args.x32 or args.mode == 16) and any(not p.startswith('llvm-') for p in profiles):
        parser.error('code16 and x32 use the full LLVM profiles')
    for profile in profiles:
        compiler, level = profile.rsplit('-', 1)
        source, obj = out/'input.ll', out/f'{profile}.o'
        options = [tools['neverc'], f'--target={triple}', f'-{level}', '-std=c23',
                   '-ffreestanding', '-fno-lto', '-fno-builtin-mimalloc',
                   '-fno-stack-protector', '-fno-pic', '-Werror']
        # The checked i386 backend owns its verifier flags and rejects -mllvm.
        if not (compiler == 'neverc' and args.mode == 32):
            options += ['-mllvm', '-verify-machineinstrs']
        if compiler.startswith('neverc'):
            if compiler == 'neverc':
                if args.mode == 32:
                    options.append('-fllvm-backend=' + tools['llc'])
                run([*options, '-c', out/'input.c', '-o', obj], profile+'-compile')
            else:
                source = out/f'{profile}.ll'
                run([*options, '-S', '-emit-llvm', out/'input.c', '-o', source], profile+'-ir')
        if compiler != 'neverc':
            if level == 'O2':
                optimized = out/f'{profile}-opt.ll'
                run([tools['opt'], '-S', '-passes=default<O2>', source, '-o', optimized], profile+'-opt')
                source = optimized
            run([tools['llc'], f'-mtriple={triple}', f'-{level}', '-verify-machineinstrs',
                 '-filetype=obj', source, '-o', obj], profile+'-compile')
        elf, binary = out/f'{profile}.elf', out/f'{profile}.bin'
        run([tools['ld.lld'], '-m', 'elf32_x86_64' if args.x32 else 'elf_i386' if args.mode != 64 else 'elf_x86_64',
             '-T', out/'guest.ld', obj, '-o', elf], profile+'-link')
        run([tools['objcopy'], '-O', 'binary', elf, binary], profile+'-binary')
        disassembly = run([tools['llvm-objdump'], '-d', *(['--mattr=+16bit-mode,-32bit-mode'] if args.mode == 16 else []), elf], profile+'-disassembly')
        functions = re.findall(r'^([0-9a-f]+) <(generated_\w+)>:\n(.*?)(?=\n[0-9a-f]+ <|\Z)',
                               disassembly, re.M | re.S)
        assert len(functions) == len(operations)*2 + (3 if args.mode == 64 else 2), disassembly
        manifest = []
        negative_site = None
        stack_site = None
        for address, name, body in functions:
            operation = name.removeprefix('generated_').rsplit('_', 1)[0]
            bits, call, immediate = operations[operation]
            instructions = re.findall(r'^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2}[ \t]+)+)\s*([^\n]+)', body, re.M)
            native = [(pc, raw, text) for pc, raw, text in instructions
                      if re.match(r'l(?:call|jmp)[wlq]\b', text)]
            assert len(native) == 1, body
            pc, raw, text = native[0]
            assert text.split()[0] == ('lcall' if call else 'ljmp')+{16:'w',32:'l',64:'q'}[bits], text
            base = 0
            address_bits = 64 if args.mode == 64 else 32
            if not immediate:
                operand = re.search(r'\*\(%(\w+)\)', text)
                assert operand, text
                reg = operand.group(1)
                bank = ['rax','rcx','rdx','rbx','rsp','rbp','rsi','rdi', *[f'r{i}' for i in range(8,16)]]
                if reg.startswith('e') or reg.endswith('d'):
                    bank = ['e'+r[1:] for r in bank[:8]] + [f'r{i}d' for i in range(8,16)]
                    address_bits = 32
                base = bank.index(reg)
            index = next(i for i, row in enumerate(instructions) if row[0] == pc)
            if name == 'generated_lcall16_fixed':
                next_pc, next_raw, _ = instructions[index + 1]
                negative_site = (int(next_pc, 16) - IMAGE, len(bytes.fromhex(next_raw)))
            if name == 'generated_ljmp16_fixed':
                stack_pc, stack_raw, _ = instructions[index - 1]
                stack_site = (int(stack_pc, 16) - IMAGE, len(bytes.fromhex(stack_raw)))
            if not call:
                assert re.fullmatch(r'mov[lq]\s+%\w+, %[er]sp', instructions[index-1][2]), body
                assert not any(re.match(r'ret[lq]?\b', text) for _, _, text in instructions[index+1:]), body
            manifest.append(f'{name} {address} {pc} {bits} {call} {immediate} {base} {len(bytes.fromhex(raw))} {address_bits}')
        path = out/f'{profile}.manifest'; path.write_text('\n'.join(manifest)+'\n')
        native = json.loads(run([runner, binary, path, args.mode], profile+'-native'))
        # Corrupt one whole continuation instruction, retaining the native
        # CALL and valid instruction boundaries. A wrong returned value, rather
        # than malformed code or a missing transfer, must trigger the failure.
        damaged = bytearray(binary.read_bytes())
        offset, size = negative_site
        assert size >= 2
        damaged[offset:offset+size] = bytes.fromhex('31 c0') + b'\x90'*(size-2)
        corrupt = out/f'{profile}-corrupt.bin'; corrupt.write_bytes(damaged)
        run([runner, corrupt, path, args.mode], profile+'-negative', expected=1)
        assert 'expected_result' in (out/f'{profile}-negative.stderr').read_text()
        damaged = bytearray(binary.read_bytes())
        offset, size = stack_site
        wrong_stack = bytes.fromhex('66 31 e4' if args.mode == 16 else
                                   '48 31 e4' if args.mode == 64 else '31 e4')
        assert size == len(wrong_stack)
        damaged[offset:offset+size] = wrong_stack
        corrupt_stack = out/f'{profile}-wrong-stack.bin'
        corrupt_stack.write_bytes(damaged)
        run([runner, corrupt_stack, path, args.mode], profile+'-negative-stack', expected=1)
        assert 'boundary.rsp == NEW_STACK' in (out/f'{profile}-negative-stack.stderr').read_text()
        results.append(dict(profile=profile, **native, binary_sha256=digest(binary),
                            negative_control='intact far CALL with incorrect returned value rejected',
                            negative_stack='intact far JMP with wrong stack rejected'))
        print(f'{profile}: {native}', flush=True)
    (out/'evidence.json').write_text(json.dumps(dict(code_bits=args.mode, x32=args.x32,
        host=platform.platform(), profiles=results,
        source_sha256={p.name:digest(p) for p in (Path(__file__), ROOT/'far_transfer_kvm.c', ROOT/'KvmTransferFixture.h')},
        tools={name:dict(path=path,sha256=digest(path)) for name,path in tools.items()
               if name != 'neverc' or any(p.startswith('neverc') for p in profiles)},
        scope='Native target/stack transport, direct and protected call-gate transitions, zero-argument returning far ABI',
        exclusions=['nonzero call-gate parameters', 'task switching', 'CET', 'fault recovery', 'asynchronous unwind', 'complete original-image register restoration']),indent=2)+'\n')


if __name__ == '__main__': main()

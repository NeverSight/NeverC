#!/usr/bin/env python3
"""Validate the full-LLVM i386 driver transaction for all admitted object formats."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

TRIPLES = ('i386-unknown-linux-gnu', 'i386-pc-windows-msvc', 'i386-apple-macosx10.13')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('neverc', 'llvm-bin', 'output'):
        p.add_argument('--' + name, type=Path, required=True)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    source = a.output / 'source with spaces.c'
    source.write_text('''
struct R { unsigned a, b, c; };
unsigned __attribute__((stdcall)) probe(unsigned x) { return x ^ 0x800001ffu; }
struct R record(struct R r) { return r; }
void native(unsigned x, void *bounds) {
  __builtin_ia32_vmfunc_eptp_switch(x);
  __builtin_ia32_tlbsync_control32(x);
  __builtin_ia32_bound32(x, bounds);
}
''')
    llc, mc, readobj = (a.llvm_bin / n for n in ('llc', 'llvm-mc', 'llvm-readobj'))
    commands, profiles, negatives = [], [], []

    def run(cmd, name, fail=None):
        r = subprocess.run(list(map(str, cmd)), capture_output=True, timeout=120)
        (a.output / (name + '.stdout')).write_bytes(r.stdout)
        (a.output / (name + '.stderr')).write_bytes(r.stderr)
        commands.append(dict(command=list(map(str, cmd)), exit_code=r.returncode))
        if fail:
            assert r.returncode > 0 and fail in r.stderr.decode(), (name, r.stderr)
        else:
            assert not r.returncode, (name, r.stderr)
        return r.stdout

    aliases = []
    for arch in ('i386', 'i486', 'i586', 'i686'):
        triple = arch + '-apple-darwin'
        obj = a.output / (arch + '-darwin.o')
        run([a.neverc, '--target=' + triple, '-ffreestanding',
             '-fno-builtin-mimalloc', '-fllvm-backend=' + str(llc), '-c',
             source, '-o', obj], arch + '-darwin')
        info = run([readobj, '--file-headers', obj], arch + '-darwin-headers')
        assert b'Arch: i386' in info and b'Format: Mach-O 32-bit i386' in info
        aliases.append(triple)

    for i, triple in enumerate(TRIPLES):
        base = [a.neverc, '--target=' + triple, '-std=gnu23', '-ffreestanding',
                '-fno-builtin-mimalloc', '-fno-lto', '-fllvm-backend=' + str(llc)]
        for opt in ('O0', 'O2'):
            name = f'{i}-{opt}'
            obj = a.output / (name + '.o')
            run(base + ['-' + opt, '-c', source, '-o', obj], name)
            info = run([readobj, '--file-headers', '--symbols', obj], name + '-headers').decode()
            assert ('Arch: i386' in info and 'AddressSize: 32bit' in info), info
            if i == 1:
                assert '_probe@4' in info
            for dialect in ('att', 'intel'):
                asm = a.output / (name + '-' + dialect + '.s')
                assembled = asm.with_suffix('.o')
                run(base + ['-' + opt, '-masm=' + dialect, '-S', source, '-o', asm], name + '-' + dialect)
                run([mc, '-triple=' + triple, '-filetype=obj', asm, '-o', assembled], name + '-' + dialect + '-assemble')
                text = run([readobj, '--file-headers', assembled], name + '-' + dialect + '-headers').decode()
                assert 'Arch: i386' in text and 'AddressSize: 32bit' in text
            ir = a.output / (name + '.ll')
            run(base + ['-' + opt, '-S', '-emit-llvm', source, '-o', ir], name + '-ir')
            assert ' asm ' not in ir.read_text()
            for api in ('vmfunc.eptp.switch', 'tlbsync.control32', 'bound32'):
                assert 'call void @llvm.x86.' + api + '(' in ir.read_text()
            bc = a.output / (name + '.bc')
            run(base + ['-' + opt, '-c', '-emit-llvm', source, '-o', bc], name + '-bitcode')
            run(base + ['-' + opt, '-c', bc, '-o', a.output / (name + '-from-bc.o')], name + '-read-bitcode')
            profiles.append(dict(triple=triple, optimization=opt))

    for i, triple in enumerate(TRIPLES):
        base = [a.neverc, '--target=' + triple, '-ffreestanding', '-fno-builtin-mimalloc']
        for flag, message in (('-c', 'full LLVM fork'), ('-S', 'full LLVM fork')):
            obj = a.output / f'no-backend-{i}-{flag}.o'
            run(base + [flag, source, '-o', obj], f'no-backend-{i}-{flag}', message)
            assert not obj.exists()
            negatives.append(f'no-backend-{i}-{flag}')
        for j in range(len(TRIPLES)):
            if i == j:
                continue
            fake = a.output / f'wrong-format-{i}-{j}'
            fake.write_text(f'''#!{sys.executable}
import sys, shutil
if '--neversight-codegen-protocol' in sys.argv:
    print('neversight-codegen-v1 x86')
else:
    shutil.copyfile({str(a.output / (str(j)+'-O0.o'))!r}, sys.argv[sys.argv.index('-o')+1])
''')
            fake.chmod(0o755)
            obj = a.output / f'rejected-{i}-{j}.o'
            run(base + ['-c', source, '-fllvm-backend=' + str(fake), '-o', obj],
                f'wrong-format-{i}-{j}', 'not an i386 relocatable object')
            assert not obj.exists(), 'wrong object committed'
            negatives.append(f'wrong-format-{i}-{j}')
        obj = a.output / f'link-{i}'
        run(base + [source, '-fllvm-backend=' + str(llc), '-o', obj],
            f'link-{i}', 'requires -c or -S')
        assert not obj.exists()
        negatives.append(f'link-{i}')
    report = dict(profiles=profiles, aliases=aliases, negative_controls=negatives, commands=commands,
                  artifacts={f.name: hashlib.sha256(f.read_bytes()).hexdigest()
                             for f in a.output.iterdir() if f.is_file()},
                  tools={str(f): hashlib.sha256(f.read_bytes()).hexdigest()
                         for f in (a.neverc, llc, mc, readobj)})
    (a.output / 'qualification.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(dict(profiles=len(profiles), negative_controls=len(negatives))))


if __name__ == '__main__':
    main()

"""Translate and run the stock optional fixtures on one native Windows runner."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import time

PLAN = 'utils/ci/cpp-optional-win64-native-plan.json'
FRONTEND = 'neverc/lib/Translate/Cpp/Frontend'
SDK = 'neverc/lib/Translate/Cpp/SDK'
TARGETS = {'x64': ('x86_64', 0x8664), 'arm64': ('aarch64', 0xAA64)}
OPTIMIZATIONS = ('-O0', '-O2')
CHECK_ONLY = 'CoreV2FunctionTraitVectorOptionalElements'


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def pe_image(path, machine):
    with path.open('rb') as stream:
        dos = stream.read(64)
        assert len(dos) == 64 and dos[:2] == b'MZ'
        offset = int.from_bytes(dos[60:64], 'little')
        assert 64 <= offset <= 1024 * 1024
        stream.seek(offset)
        header = stream.read(26)
    assert len(header) == 26 and header[:4] == b'PE\0\0'
    assert int.from_bytes(header[4:6], 'little') == machine
    assert header[24:26] == b'\x0b\x02'
    return dict(sha256=digest(path), bytes=path.stat().st_size, pe_header_hex=header.hex())


def execute(argv, directory, logs, name, timeout):
    output_path = logs / (name + '.log')
    started = time.monotonic()
    try:
        with output_path.open('xb') as stream:
            process = subprocess.run([str(arg) for arg in argv], cwd=directory, stdin=subprocess.DEVNULL,
                                     stdout=stream, stderr=subprocess.STDOUT, timeout=timeout, check=False)
        status = process.returncode
    except subprocess.TimeoutExpired:
        status = None
    assert output_path.stat().st_size <= 1024**2
    command = dict(argv=[str(arg) for arg in argv], returncode=status, timeout_seconds=timeout,
        elapsed_ms=round(1000*(time.monotonic()-started)), log='logs/' + output_path.name,
        log_sha256=digest(output_path))
    return command, output_path.read_text(encoding='utf-8', errors='replace')


def run(args):
    root = Path.cwd().resolve()
    output = args.output.resolve()
    output.mkdir()
    logs = output / 'logs'
    logs.mkdir()
    work = output / 'work'
    work.mkdir()
    compiler = args.compiler.resolve()
    assert sys.platform == 'win32' and compiler.is_file() and compiler.name == 'neverc.exe'
    target_arch, machine = TARGETS[args.arch]
    assert platform.machine().lower() in (('amd64','x86_64') if args.arch=='x64' else ('arm64','aarch64'))
    revision = subprocess.check_output(['git','rev-parse','HEAD'], cwd=root,
                                       text=True, timeout=30).strip()
    assert revision == os.environ['GITHUB_SHA'] and os.environ['GITHUB_RUN_ATTEMPT'] == '1'
    plan = json.loads((root/PLAN).read_bytes())
    assert plan['targets'][args.arch] == f'{target_arch}-pc-windows-msvc'
    for path,key,expected in ((FRONTEND, 'exact_private_input_sha256',19), (SDK,'sdk_input_sha256',695)):
        identities = plan[key]
        assert len(identities) == expected
        for name,sha in identities.items():
            assert digest(root/path/name) == sha
    commands = []
    for option,result_name in (('--version','compiler-version'),('-dumpmachine','native-target')):
        command,value = execute([compiler,option], work, logs, result_name, 30)
        commands.append(command)
        assert command['returncode'] == 0
    target = value.strip()
    assert target.startswith(target_arch+'-') and 'windows-msvc' in target
    report = dict(revision=revision, run=int(os.environ['GITHUB_RUN_ID']), attempt=1, arch=args.arch,
        native_target=target, compiler=pe_image(compiler,machine), exact_private_inputs_verified=19,
        sdk_inputs_verified=695, plan_sha256=digest(root/PLAN), commands=commands,
        completed=False, passed=False, cases=[])
    try:
        assert len(plan['cases']) == 18
        for row in plan['cases']:
            name = row['name']
            case_dir = work / name
            case_dir.mkdir()
            source, generated = case_dir/'fixture.cpp', case_dir/'generated.nc'
            source.write_bytes(row['source'].encode())
            assert digest(source) == row['source_sha256']
            should_run = row['expected']=='accept' and name!=CHECK_ONLY
            case = dict(name=name, source_sha256=digest(source), expected=row['expected'],
                        commands=[], products=[])
            translate = [compiler,'translate','--from','cpp',source,'--profile','cpp-core-v2']
            translate += ['-o',generated] if should_run else ['--check']
            translate += ['--','-std=c++17']
            command, diagnostic = execute(translate,case_dir,logs,name+'-translate',180)
            case['commands'].append(command)
            passed = command['returncode'] == 0
            if row['expected'] == 'reject':
                passed = (command['returncode'] == 1 and ('TR0201' in diagnostic or 'TR0203' in diagnostic)
                          and not generated.exists())
            if passed and should_run:
                case['generated_c23_sha256'] = digest(generated)
                for mode in OPTIMIZATIONS:
                    executable = case_dir/('generated'+mode+'.exe')
                    argv = [compiler,generated,mode,'-fno-lto','-fno-builtin-mimalloc','-o',executable]
                    built,_ = execute(argv,case_dir,logs,name+mode+'-compile',180)
                    case['commands'].append(built)
                    if built['returncode'] != 0:
                        passed = False
                        continue
                    case['products'].append(dict(optimization=mode, **pe_image(executable,machine)))
                    executed,_ = execute([executable],case_dir,logs,name+mode+'-run',30)
                    case['commands'].append(executed)
                    passed = passed and executed['returncode'] == 0
            case['passed'] = passed
            report['cases'].append(case)
            print(name, 'passed' if passed else 'failed', flush=True)
        report['completed'] = True
        report['passed'] = all(row['passed'] for row in report['cases'])
    finally:
        (output/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--arch', required=True, choices=TARGETS)
    parser.add_argument('--compiler', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    raise SystemExit(run(parser.parse_args()))

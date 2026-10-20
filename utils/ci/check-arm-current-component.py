"""Controlled MSVC ARM64 production-component replay, entirely on Actions."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import struct
import subprocess
import sys
import time
import traceback


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def optional_layout(response):
    # An independent layout calculation over actual protocol records. This is
    # explicitly not execution of Translate's C++ record verifier.
    carrier = response['target']['carrier_layout']
    records = {record['id']: record for record in response['records']}

    def storage(kind):
        if kind in carrier:
            result = carrier[kind]
        elif kind.startswith(('ptr:', 'cptr:', 'fnptr:')) or kind == 'nullptr':
            result = carrier['default-pointer']
        elif kind in records:
            result = records[kind]['layout']
        elif kind.startswith('arr:'):
            _, count, element = kind.split(':', 2)
            size, align = storage(element)
            return size * int(count), align
        else:
            raise AssertionError(kind)
        return result['size_bits'], result['abi_align_bits']

    def natural(fields):
        end, align, offsets = (8 if not fields else 0), 8, []
        for field in fields:
            size, alignment = storage(field['type'])
            align = max(align, alignment)
            start = (end + alignment - 1) // alignment * alignment
            offsets.append(start)
            end = start + size
        return {'size_bits': (end + align - 1) // align * align,
                'abi_align_bits': align, 'field_offsets_bits': offsets}

    values = [record for record in records.values() if len(record['fields']) in [2, 3]
              and record['fields'][1]['type'] == 'bool']
    assert len(values) == 1
    record = values[0]
    wrapper = records[record['fields'][0]['type']]
    assert natural(wrapper['fields']) == wrapper['layout']
    actual, emitted = record['layout'], natural(record['fields'])
    return {'actual_sdk_layout': actual, 'emitted_fields_layout': emitted,
            'tail_carrier': len(record['fields']) == 3, 'matches': actual == emitted}


def main():
    parser = argparse.ArgumentParser()
    for name in ['repo', 'output', 'build', 'plan']:
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    root, out, build = args.repo.resolve(), args.output.resolve(), args.build.resolve()
    out.mkdir(parents=True, exist_ok=True)
    build.mkdir(parents=True)
    plan = json.loads(args.plan.read_text())
    frontend = root / 'neverc/lib/Translate/Cpp/Frontend'
    lowering = frontend / 'Lowering.cpp'
    current = lowering.read_bytes()
    report = {'completed': False, 'passed': False, 'commands': [], 'stages': {},
              'requests': [], 'neverc_host_compiler_rebuilt': False,
              'production_private_component_rebuilt': True,
              'frontend_execution_timeout_seconds': 60,
              'debugger_used': False, 'production_options_bypassed': False,
              'actual_translate_verifier_executed': False, 'native_products_executed': 0,
              'formal_xml': 0, 'full_ci_passed': False, 'published_to_dev': False,
              'goal_complete': False, 'all_binaries_remote_only': True}
    environment = dict(os.environ, NEVERC_CPP_PROTOCOL_EXECUTION_DIR=str(out / 'invocations'))

    def save():
        (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')

    def run(argv, label, timeout=60, allow_failure=False):
        argv = list(map(str, argv))
        log = out / (label + '.log')
        log.parent.mkdir(parents=True, exist_ok=True)
        start = time.monotonic()
        with log.open('wb') as stream:
            proc = subprocess.Popen(argv, cwd=root, env=environment, stdout=stream,
                                    stderr=subprocess.STDOUT,
                                    creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
            try:
                code = proc.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                subprocess.run(['taskkill', '/PID', str(proc.pid), '/T', '/F'],
                               stdout=stream, stderr=subprocess.STDOUT, timeout=15)
                proc.wait(timeout=15)
                stream.write(b'\nDIAGNOSTIC PROCESS GROUP TIMED OUT\n')
                code = -1000
        row = {'command': argv, 'pid': proc.pid, 'cwd': str(root),
               'returncode': code, 'timeout_seconds': timeout,
               'log': log.relative_to(out).as_posix(), 'log_sha256': sha(log),
               'elapsed_seconds': time.monotonic() - start}
        report['commands'].append(row)
        save()
        print(label, code, flush=True)
        if not allow_failure:
            assert code == 0, (label, code)
        return row, log

    def input_hashes():
        return {name: sha(frontend / name) for name in plan['current_private_input_sha256']}

    def pe(binary, stage):
        with binary.open('rb') as stream:
            data = stream.read(4096)
        offset = struct.unpack_from('<I', data, 60)[0]
        assert data[:2] == b'MZ' and data[offset:offset + 6] == b'PE\0\0\x64\xaa'
        header = out / stage / 'caller-pe-header.bin'
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_bytes(data[:min(4096, offset + 256)])
        return {'sha256': sha(binary), 'bytes': binary.stat().st_size,
                'header': header.relative_to(out).as_posix(), 'header_sha256': sha(header)}

    def capture_stage(stage, private):
        generated = [path for path in private.rglob('BuildID.h')
                     if '#define NEVERC_CPP_BUILD_ID ' in path.read_text()]
        assert len(generated) == 1
        archive = private / 'lib/Release/nevercCppFrontend.lib'
        assert archive.is_file()
        version = re.search(r'#define NEVERC_CPP_BUILD_ID "([^"]+)"',
                            generated[0].read_text()).group(1)
        identities = input_hashes()
        assert identities == plan[stage + '_private_input_sha256']
        assert version == plan[stage + '_build_id']
        row, log = run(['ninja', '-C', private, '-t', 'compdb'], stage + '/actual-compdb')
        all_commands = json.loads(log.read_text())
        expected = {str((frontend / name).resolve()).lower() for name in
                    ['Frontend.cpp', 'Lowering.cpp', 'Project.cpp', 'MathSDK.cpp', 'RandomSDK.cpp']}
        commands = [entry for entry in all_commands
                    if str(Path(entry['file']).resolve()).lower() in expected]
        assert len(commands) == 5 and {str(Path(x['file']).resolve()).lower() for x in commands} == expected
        objects = []
        for entry in commands:
            obj = Path(entry['output'])
            if not obj.is_absolute():
                obj = Path(entry['directory']) / obj
            obj = obj.resolve()
            assert obj.is_relative_to(private) and obj.is_file()
            objects.append({'source': str(Path(entry['file']).resolve()),
                            'path': str(obj), 'sha256': sha(obj)})
        closure = list(private.rglob('archives-Release.txt'))
        assert len(closure) == 1
        libraries = [Path(line) for line in closure[0].read_text().splitlines() if line]
        dependencies = {str(path.relative_to(private)): sha(path) for path in libraries
                        if path.name != 'nevercCppFrontendBridge.lib'}
        destination = build / 'retained' / stage
        destination.mkdir(parents=True)
        shutil.copyfile(archive, destination / archive.name)
        for name in ['BuildID.h', 'BuiltinCppSdkData.h']:
            shutil.copyfile(generated[0].parent / name, destination / name)
        shutil.copyfile(private / 'NeverCCppPrivatePrefix.h', destination / 'NeverCCppPrivatePrefix.h')
        caller = destination / 'actual-private-frontend.exe'
        run(['cl', '/nologo', '/std:c++17', '/EHsc', '/MD', '/O2',
             '/I' + str(frontend), root / 'utils/ci/ArmCurrentFrontendDriver.cpp',
             '/Fo' + str(destination / 'caller.obj'), '/Fe' + str(caller), archive,
             '/link', 'psapi.lib', 'shell32.lib', 'ole32.lib', 'uuid.lib',
             'advapi32.lib', 'ws2_32.lib', 'ntdll.lib', 'version.lib'], stage + '/link-real-C-entry')
        executable = pe(caller, stage)
        _, version_log = run([caller, '__neverc_cpp_frontend', '--version'], stage + '/version')
        expected_version = 'neverc-cpp-frontend 20.1.8 protocol 1 ' + version
        assert version_log.read_text().strip() == expected_version
        info = {'private_input_sha256': identities, 'build_id': version,
                'archive_sha256': sha(archive), 'bridge_compile_commands': commands,
                'actual_bridge_objects': objects, 'dependency_archive_sha256': dependencies,
                'actual_executable': executable,
                'generated_header_sha256': {name: sha(destination / name) for name in
                    ['BuildID.h', 'BuiltinCppSdkData.h', 'NeverCCppPrivatePrefix.h']},
                'archive_and_binary_remote_only': True}
        report['stages'][stage] = info
        (destination / 'producer.json').write_text(json.dumps(info, indent=2) + '\n')
        save()
        return caller, info

    def request(binary, stage, target, case, iteration=0):
        folder = out / stage / 'requests' / target / (case['name'] + '-' + str(iteration))
        folder.mkdir(parents=True)
        source, req, response = folder / 'input.cpp', folder / 'request.json', folder / 'response.json'
        source.write_bytes(case['source'].encode())
        assert sha(source) == case['source_sha256']
        req.write_text(json.dumps({'protocol': 1, 'profile': 'cpp-core-v2',
            'root': str(folder), 'source': str(source), 'target': target,
            'arguments': ['-std=c++17'], 'sdk': {'distribution_id': catalog['distribution_id'],
                'catalog_sha256': plan['sdk_catalog_sha256']}}) + '\n')
        cmd, _ = run([binary, '__neverc_cpp_frontend', '--request', req, '--output', response],
                     stage + '/execution/' + target + '-' + case['name'] + '-' + str(iteration), allow_failure=True)
        actual = json.loads(response.read_text()) if response.exists() else None
        codes = [x['code'] for x in actual['diagnostics']] if actual else []
        if stage == 'baseline':
            passed = cmd['returncode'] in [-1073741819, 3221225477] and not response.exists()
        elif case['expected'] == 'accept':
            passed = cmd['returncode'] == 0 and actual is not None and not codes
        else:
            passed = cmd['returncode'] != 0 and actual is not None and bool(codes) and all(
                code in ['TR0201', 'TR0203'] for code in codes)
        row = {'stage': stage, 'target': target, 'name': case['name'], 'iteration': iteration,
               'expected': 'access-violation' if stage == 'baseline' else case['expected'],
               'source_sha256': sha(source), 'request': req.relative_to(out).as_posix(),
               'request_sha256': sha(req), 'response': response.relative_to(out).as_posix() if actual else None,
               'response_sha256': sha(response) if actual else None, 'returncode': cmd['returncode'],
               'pid': cmd['pid'], 'diagnostic_codes': codes, 'passed': passed}
        if stage == 'current' and case['name'] == plan['optional_case']['name'] and passed:
            row['actual_record_layout'] = optional_layout(actual)
            row['passed'] = row['actual_record_layout']['matches'] and (
                row['actual_record_layout']['tail_carrier'] == target.endswith('-pc-windows-msvc'))
        report['requests'].append(row)
        invocation = out / 'invocations' / str(cmd['pid'])
        assert invocation.is_dir() and sha(invocation / 'request.json') == sha(req)
        (invocation / 'parent-observed-returncode.txt').write_text(str(cmd['returncode']) + '\n')
        save()

    save()
    try:
        assert os.environ.get('GITHUB_ACTIONS') == 'true' and platform.system() == 'Windows'
        assert platform.machine().lower() in ['arm64', 'aarch64'] and platform.python_version() == '3.12.10'
        assert os.environ['VSCMD_ARG_TGT_ARCH'].lower() == 'arm64'
        report['revision'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
        report['source_sha256'] = {name: sha(root / name) for name in plan['source_paths']}
        assert input_hashes() == plan['current_private_input_sha256']
        sdk = root / 'neverc/lib/Translate/Cpp/SDK'
        assert sha(sdk / 'catalog.json') == plan['sdk_catalog_sha256']
        catalog = json.loads((sdk / 'catalog.json').read_text())
        records = catalog['headers'] + catalog['metadata']
        assert len(records) == 665 and all(sha(sdk / x['root'] / x['path']) == x['sha256'] for x in records)
        report['sdk_files_verified'] = 665
        _, compiler_log = run(['cl'], 'environment/msvc-version', allow_failure=True)
        assert re.search(r'Version 19\.51\.', compiler_log.read_text(errors='replace'))
        change = plan['controlled_lowering_change']
        begin = change['offset']
        assert current[begin:begin + len(change['repaired_text'].encode())] == change['repaired_text'].encode()
        old = current[:begin] + change['baseline_text'].encode() + current[begin + len(change['repaired_text'].encode()):]
        assert hashlib.sha256(old).hexdigest() == plan['baseline_private_input_sha256']['Lowering.cpp']
        lowering.write_bytes(old)
        run(['cmake', '-S', root / 'llvm', '-B', build / 'owner', '-G', 'Ninja',
             '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_COMPILER=cl', '-DCMAKE_CXX_COMPILER=cl',
             '-DCMAKE_SYSTEM_PROCESSOR=ARM64', '-DLLVM_ENABLE_PROJECTS=neverc',
             '-DLLVM_ENABLE_ASSERTIONS=OFF', '-DLLVM_TARGETS_TO_BUILD=X86;AArch64',
             '-DPython3_EXECUTABLE=' + sys.executable, '-DNEVERC_CPP_BUILD_JOBS=4',
             '-DNEVERC_INCLUDE_TESTS=OFF', '-DNEVERC_ENABLE_PYTHON_PLUGINS=OFF',
             '-DNEVERC_BUNDLE_PYTHON_RUNTIME=OFF', '-DLLVM_INCLUDE_TESTS=OFF',
             '-DLLVM_INCLUDE_EXAMPLES=OFF', '-DLLVM_INCLUDE_BENCHMARKS=OFF',
             '-DLLVM_ENABLE_ZLIB=OFF', '-DLLVM_ENABLE_ZSTD=OFF', '-DLLVM_ENABLE_LIBXML2=OFF',
             '-DLLVM_ENABLE_CURL=OFF', '-DLLVM_ENABLE_TERMINFO=OFF'], 'build/configure-production-owner', 300)
        run(['cmake', '--build', build / 'owner', '--target', 'nevercCppFrontendBuild',
             '--parallel', '4'], 'baseline/build-production-component', 14400)
        private = build / 'owner/_deps/neverc-cpp-build'
        baseline, before = capture_stage('baseline', private)
        string = plan['string_case']
        for i in range(3):
            request(baseline, 'baseline', 'aarch64-pc-windows-msvc', string, i)
        # Return the exact candidate source, regenerate its real BuildID and
        # rebuild every bridge object through the unchanged production owner.
        lowering.write_bytes(current)
        for obj in before['actual_bridge_objects']:
            path = Path(obj['path'])
            assert sha(path) == obj['sha256']
            path.unlink()
        run(['cmake', '--build', build / 'owner', '--target', 'nevercCppFrontendBuild',
             '--parallel', '4'], 'current/build-production-component', 2400)
        caller, after = capture_stage('current', private)
        assert before['dependency_archive_sha256'] == after['dependency_archive_sha256']
        assert before['build_id'] != after['build_id']
        report['dependency_archives_byte_unchanged'] = True
        for i in range(3):
            request(caller, 'current', 'aarch64-pc-windows-msvc', string, i)
        for target in plan['target_abis']:
            for case in plan['pair_cases'] + [plan['optional_case']]:
                request(caller, 'current', target, case)
        project, _ = run([sys.executable, root / 'utils/translate-frontends/cpp/tests/project.py',
            '--neverc', caller, '--target', 'aarch64-pc-windows-msvc', '--output-dir', out / 'project'],
            'current/original-project-suite', 1800, allow_failure=True)
        report['original_project_suite_passed'] = project['returncode'] == 0
        report['original_project_script_sha256'] = sha(root / 'utils/translate-frontends/cpp/tests/project.py')
        assert report['original_project_script_sha256'] == plan['original_project_script_sha256']
        report['baseline_reproduced'] = all(x['passed'] for x in report['requests'] if x['stage'] == 'baseline')
        report['actual_current_requests'] = sum(x['stage'] == 'current' for x in report['requests'])
        assert report['actual_current_requests'] == 115 and len(report['requests']) == 118
        report['passed'] = report['baseline_reproduced'] and report['original_project_suite_passed'] and all(
            x['passed'] for x in report['requests'])
        report['completed'] = True
        save()
        return 0 if report['passed'] else 1
    except BaseException:
        report['failure'] = traceback.format_exc()
        save()
        print(report['failure'], flush=True)
        return 1
    finally:
        lowering.write_bytes(current)
        report['candidate_lowering_restored'] = sha(lowering) == plan['current_private_input_sha256']['Lowering.cpp']
        save()


if __name__ == '__main__':
    raise SystemExit(main())

"""Actions-only member C23/LLVM IR check, using the real current Translate owners."""
from collections import Counter
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import signal
import subprocess
import time
import traceback

TARGETS = ['x86_64-unknown-linux-gnu', 'aarch64-unknown-linux-gnu',
           'x86_64-apple-darwin', 'arm64-apple-darwin',
           'x86_64-pc-windows-msvc', 'aarch64-pc-windows-msvc']
PROBE = '\n_Static_assert(sizeof(void *) == 8, "member IR requires 64 bit pointers");\nconst unsigned nct_member_ir_pointer_bits = sizeof(void *) * 8;\n'


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def ir_driver_hook(original, old_driver, old_build, old_trace, new_driver, new_build, new_trace):
    statements = []; current = []; depth = 0
    for line in original.splitlines():
        current.append(line); depth += line.count('(') - line.count(')')
        assert depth >= 0
        if depth == 0:
            statement = '\n'.join(current) + '\n'; current = []
            if re.match(r'(?:add_executable|set_target_properties|target_include_directories|target_compile_options|target_compile_definitions|target_link_libraries)\(cpp-private-runtime-driver\b|add_custom_command\(TARGET cpp-private-runtime-driver\b', statement):
                statements.append(statement)
    assert not current and depth == 0 and len(statements) == 7
    selected = ''.join(statements); text = selected
    pairs = [('cpp-private-runtime-driver', 'cpp-private-member-target-ir-driver'),
             ('OUTPUT_NAME "' + old_driver.name + '"', 'OUTPUT_NAME "' + new_driver.name + '"'),
             ('RUNTIME_OUTPUT_DIRECTORY "' + old_build.as_posix() + '"', 'RUNTIME_OUTPUT_DIRECTORY "' + new_build.as_posix() + '"'),
             (old_trace.as_posix(), new_trace.as_posix())]
    for before, after in pairs:
        assert before in text and after not in text
        text = text.replace(before, after)
    inverse = text
    for before, after in reversed(pairs):
        inverse = inverse.replace(after, before)
    assert inverse == selected
    assert 'AuditArchive.py' in text and '--host-lib-dir' in text and '--host-nm' in text
    return original + '\n' + text, selected, text


def check_ir(text, target):
    triple, = re.findall(r'^target triple = "([^"]+)"$', text, re.M)
    layout, = re.findall(r'^target datalayout = "([^"]+)"$', text, re.M)
    assert layout.startswith('e-')
    arch = triple.split('-')[0]
    assert arch in (['x86_64'] if target.startswith('x86_64-') else ['aarch64', 'arm64'])
    if 'linux' in target:
        assert '-linux-' in triple
    elif 'windows' in target:
        assert '-windows-msvc' in triple
    else:
        assert '-apple-' in triple and ('darwin' in triple or 'macosx' in triple)
    assert re.search(r'^@nct_member_ir_pointer_bits = [^\n]*constant i32 64,', text, re.M)
    assert re.search(r'^define [^\n]*@replacement\(', text, re.M)
    return dict(triple=triple, data_layout=layout, pointer_bits=64)


def main():
    assert os.environ.get('GITHUB_ACTIONS') == 'true' and os.environ.get('GITHUB_REPOSITORY') == 'NeverSight/NeverC'
    parser = argparse.ArgumentParser()
    for name in ['repo', 'runtime-report', 'component-report', 'archive', 'private-prefix', 'plan', 'output', 'build']:
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args(); root = args.repo.resolve(); out = args.output.resolve(); build = args.build.resolve()
    assert not out.exists() and not build.exists() and not build.is_relative_to(out)
    out.mkdir(parents=True); build.mkdir(parents=True)
    r = dict(passed=False, completed=False, commands=[], requests=[], ir_products=[],
             fixture_group='member-target-ir-six64bit', full_stl_complete=False,
             original_full221_accounting_unchanged=True, actual_generated_program_executions=0)
    def save():
        (out / 'target-ir-results.json').write_text(json.dumps(r, indent=2) + '\n')
    def run(argv, label, timeout=60):
        argv = list(map(str, argv)); log = out / (label + '.log'); log.parent.mkdir(parents=True, exist_ok=True)
        started = time.monotonic()
        with log.open('w') as stream:
            proc = subprocess.Popen(argv, cwd=root, start_new_session=True, stdout=stream, stderr=subprocess.STDOUT)
            try:
                code = proc.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL); proc.wait(); code = -1000
        r['commands'].append(dict(command=argv, pid=proc.pid, cwd=str(root), log=str(log.relative_to(out)),
            log_sha256=sha(log), returncode=code, timeout_seconds=timeout, elapsed_seconds=time.monotonic() - started))
        save(); print(label, code, flush=True); return code
    save()
    try:
        plan = json.loads(args.plan.read_bytes()); runtime = json.loads(args.runtime_report.read_bytes())
        component = json.loads(args.component_report.read_bytes())
        assert not plan['preparation_only'] and plan['execution_binding_required']
        assert plan['target_abis'] == TARGETS and len(plan['programs']) == 7
        assert runtime['passed'] and runtime['completed'] and runtime['actual_formal_tests'] == 221
        assert runtime['actual_frontend_request_count'] == 832 and len(runtime['actual_stl_generated_products']) == 182
        assert runtime['sdk_files_verified'] == 687 and runtime['actual_gtest_returncode'] == 0
        assert not runtime['actual_formal_failed'] and not runtime['actual_formal_skipped']
        assert sha(args.component_report) == runtime['component_report_sha256'] and component['completed'] and component['passed']
        assert component['revision'] == runtime['component_producer_revision'] and len(component['requests']) == 750
        assert runtime['exact_private_input_sha256'] == component['exact_private_input_sha256'] == plan['exact_private_input_sha256']
        assert sha(__file__) == plan['runner_sha256'] and len(plan['exact_private_input_sha256']) == 19
        assert all(sha(root / ('neverc/lib/Translate/Cpp/Frontend/' + name)) == digest for name, digest in plan['exact_private_input_sha256'].items())
        sdk = root / 'neverc/lib/Translate/Cpp/SDK'; catalog = json.loads((sdk / 'catalog.json').read_bytes())
        assert sha(sdk / 'catalog.json') == plan['sdk_catalog_sha256'] == 'b4a7b597150882cd93285481cc060d2eab6949b5c8d50b7ce234579cc814bd77'
        sdk_rows = catalog['headers'] + catalog['metadata']; assert len(sdk_rows) == 687
        assert all(sha(sdk / row['root'] / row['path']) == row['sha256'] for row in sdk_rows)
        current_sdk = {p.relative_to(sdk).as_posix():sha(p) for p in sdk.rglob('*') if p.is_file()}
        assert current_sdk==plan['sdk_input_sha256']==runtime['exact_sdk_input_sha256']==component['exact_sdk_input_sha256']
        assert len(current_sdk)==695
        r['exact_sdk_input_sha256']=current_sdk
        owner = runtime['actual_cmake_owners']; old_hook = Path(owner['hook']); old_driver = Path(owner['driver'])
        assert sha(old_hook) == owner['hook_sha256'] and sha(old_driver) == runtime['driver_elf']['sha256']
        archive = args.archive.resolve(); backend = Path(runtime['baseline_provenance']['compiler']).resolve()
        assert sha(archive) == runtime['actual_private_archive_sha256'] == component['replayed_archive_sha256']
        assert sha(backend) == runtime['baseline_provenance']['compiler_sha256'] == 'fc803587bb5ad6997f9921a05da06c79816f510e8cb9a3185f21cd0a8eb1ad84'
        assert sha(args.private_prefix) == runtime['private_prefix_sha256']
        driver_source = 'utils/ci/PrivateFrontendRuntimeDriver.cpp'
        assert sha(root / driver_source) == runtime['source_sha256'][driver_source] == plan['runtime_driver_source_sha256']
        old_report_sha = sha(args.runtime_report); driver = build / 'private-member-target-ir-driver'
        hook_text, before, after = ir_driver_hook(old_hook.read_text(), old_driver, old_driver.parent,
            args.runtime_report.resolve().parent / 'invocations', driver, build, out / 'invocations')
        hook = build / 'member-target-ir-owner.cmake'; hook.write_text(hook_text)
        (out / 'member-target-ir-owner.cmake.txt').write_text(hook_text)
        configure, = [row['command'] for row in runtime['commands'] if row['log'] == 'runtime-build/configure-real-owners.log']
        configure = list(configure); option = '-DCMAKE_PROJECT_LLVM_INCLUDE=' + str(old_hook)
        assert configure.count(option) == 1; configure[configure.index(option)] = '-DCMAKE_PROJECT_LLVM_INCLUDE=' + str(hook)
        assert run(configure, 'build/configure-current-owners', 180) == 0
        assert run(['cmake', '--build', owner['cmake_owner'], '--target', 'cpp-private-member-target-ir-driver',
                    '--verbose', '--parallel', '4'], 'build/current-ir-driver', 1800) == 0
        audit = (out / 'build/current-ir-driver.log').read_text()
        assert all(token in audit for token in ['AuditArchive.py', '--host-lib-dir', '--host-nm',
            'Builtin C++ frontend: defined and undefined symbols use the private LLVM ABI'])
        assert driver.is_file()
        with driver.open('rb') as stream:
            driver_header = stream.read(64)
        assert len(driver_header) == 64 and driver_header[:6] == b'\x7fELF\x02\x01' and driver_header[18:20] == b'\x3e\x00'
        assert run([driver, '__neverc_cpp_frontend', '--version'], 'driver-version') == 0
        assert (out / 'driver-version.log').read_text().strip() == 'neverc-cpp-frontend 20.1.8 protocol 1 ' + runtime['frontend_build_id']
        r.update(revision=runtime['revision'], execution_plan_sha256=sha(args.plan), runtime_report_sha256=old_report_sha,
            output=str(out), build=str(build), driver=str(driver), driver_first64_hex=driver_header.hex(),
            exact_private_input_sha256=plan['exact_private_input_sha256'], sdk_files_verified=687,
            original_full221_frontend_requests=832, original_full221_formal_tests=221,
            actual_private_archive_sha256=sha(archive), backend_sha256=sha(backend), driver_sha256=sha(driver),
            original_hook_sha256=sha(old_hook), new_hook_sha256=sha(hook), selected_driver_statements_sha256=hashlib.sha256(before.encode()).hexdigest(),
            adapted_driver_statements_sha256=hashlib.sha256(after.encode()).hexdigest(), frontend_build_id=runtime['frontend_build_id'])
        for target in TARGETS:
            for case in plan['programs']:
                assert re.fullmatch('[a-z0-9-]+', case['name']) and hashlib.sha256(case['source'].encode()).hexdigest() == case['source_sha256']
                directory = build / target / case['name']; directory.mkdir(parents=True)
                evidence = out / 'cases' / target / case['name']; evidence.mkdir(parents=True)
                source = directory / 'input.cpp'; source.write_text(case['source'])
                shutil.copyfile(source, evidence / 'input.cpp')
                generated = directory / 'generated.nc'; prior = set((out / 'invocations/frontend').glob('*'))
                command = [driver, 'translate', '--from', 'cpp', source, '--profile', 'cpp-core-v2', '--target', target,
                           '-o', generated, '--', '-std=c++17']
                assert run(command, 'cases/' + target + '/' + case['name'] + '/translate') == 0 and generated.is_file()
                fresh = set((out / 'invocations/frontend').glob('*')) - prior; assert len(fresh) == 1
                record, = fresh; assert record.name.isdigit() and int(record.name) > 0
                assert {p.name for p in record.iterdir()} == {'request.json', 'response.json', 'argv.txt', 'environment-before.tsv', 'returncode.txt'}
                request = json.loads((record / 'request.json').read_bytes()); response = json.loads((record / 'response.json').read_bytes())
                assert Path(request['source']) == source and request['profile'] == 'cpp-core-v2' and request['arguments'] == ['-std=c++17']
                assert response['frontend']['build'] == runtime['frontend_build_id'] and int((record / 'returncode.txt').read_text()) == 0
                assert response['target']['pointer_bits'] == 64 and response['target']['little_endian'] is True
                assert request['target'] == target
                expected, = [row for row in plan['actual_member_native_layouts'] if row['target'] == target and row['name'] == case['name']]
                binders = [row for row in response['records'] if any(field['name'] == 'nct_bind_callable' for field in row['fields'])]
                assert len(binders) == len(expected['binders']) and binders
                actual_layouts = Counter(json.dumps([row['fields'][-2]['type'], row['layout']], sort_keys=True) for row in binders)
                expected_layouts = Counter(json.dumps([row['opaque_storage'], row['layout']], sort_keys=True) for row in expected['binders'])
                assert actual_layouts == expected_layouts
                shutil.copyfile(generated, evidence / 'generated.nc.txt')
                probe = directory / 'probe.nc'; probe.write_bytes(generated.read_bytes() + PROBE.encode())
                shutil.copyfile(probe, evidence / 'probe.nc.txt')
                r['requests'].append(dict(name=case['name'], target=target, source_sha256=sha(source), generated_c23_sha256=sha(generated),
                    probe_sha256=sha(probe), frontend_pid=int(record.name), request_sha256=sha(record / 'request.json'), response_sha256=sha(record / 'response.json')))
                for optimization in ['-O0', '-O2']:
                    ir = directory / (optimization + '.ll')
                    assert run([driver, '--no-default-config', '--target=' + target, '-std=c23', '-fno-lto', '-fno-builtin-mimalloc',
                        '-S', '-emit-llvm', optimization, probe, '-o', ir], 'cases/' + target + '/' + case['name'] + '/' + optimization) == 0
                    text = ir.read_text(); model = check_ir(text, target)
                    destination = evidence / (optimization + '.ll.txt'); shutil.copyfile(ir, destination)
                    r['ir_products'].append(dict(name=case['name'], target=target, optimization=optimization, path=str(destination.relative_to(out)), sha256=sha(destination), **model))
        frontend = list((out / 'invocations/frontend').iterdir()); backend_records = list((out / 'invocations/backend').iterdir())
        assert len(frontend) == 42 and len(backend_records) == 168
        backend_argv = [(path / 'argv.txt').read_text().splitlines() for path in backend_records]
        assert all(row[0] == str(backend) for row in backend_argv)
        assert sum('-fsyntax-only' in row for row in backend_argv) == 42
        assert sum('-c' in row for row in backend_argv) == 42
        assert sum('-S' in row and '-emit-llvm' in row for row in backend_argv) == 84
        assert len(r['requests']) == 42 and len(r['ir_products']) == 84 and len(r['commands']) == 129
        assert all(row['returncode'] == 0 for row in r['commands'])
        assert sha(args.runtime_report) == old_report_sha and sha(old_hook) == owner['hook_sha256']
        assert sha(old_driver) == runtime['driver_elf']['sha256'] and sha(archive) == r['actual_private_archive_sha256'] and sha(backend) == r['backend_sha256']
        r['actual_invocation_inventory'] = [dict(path=str(path.relative_to(out)), sha256=sha(path))
            for path in sorted((out / 'invocations').rglob('*')) if path.is_file()]
        assert len(r['actual_invocation_inventory']) == 378
        r.update(passed=True, completed=True, actual_frontend_requests=42, actual_c23_ir_compilations=84,
                 actual_backend_invocations=168, actual_pid_files=378)
    except Exception as error:
        r['failure'] = type(error).__name__ + ': ' + str(error); r['traceback'] = traceback.format_exc(); save(); raise
    save()


if __name__ == '__main__':
    main()

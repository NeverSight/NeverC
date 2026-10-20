"""Build the real isolated frontend component and replay actual protocol calls."""
from pathlib import Path
import argparse
import ast
import hashlib
import json
import os
import re
import shutil
import shlex
import subprocess
import time
import traceback


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=True)
    args = parser.parse_args()
    root, out, build = args.repo.resolve(), args.output.resolve(), args.build.resolve()
    out.mkdir(parents=True, exist_ok=True)
    report_path = out / 'results.json'
    report = {'passed': False, 'completed': False, 'commands': [], 'requests': [],
              'private_frontend_rebuilt': True, 'neverc_host_compiler_rebuilt': False,
              'frontend_execution_timeout_seconds': 60, 'original_test_scripts_modified': False,
              'private_library_stubs_used': False, 'apt_llvm_library_abi_mixed': False,
              'full_ci_passed': False, 'published_to_dev': False, 'goal_complete': False}
    environment = dict(os.environ, NEVERC_CPP_PROTOCOL_EXECUTION_DIR=str(out / 'executions'))

    def save():
        report_path.write_text(json.dumps(report, indent=2) + '\n')

    def run(command, label, timeout=60, allow_failure=False):
        command = list(map(str, command))
        log = out / (label + '.log')
        log.parent.mkdir(parents=True, exist_ok=True)
        start = time.monotonic()
        with log.open('w') as stream:
            try:
                code = subprocess.run(command, cwd=root, env=environment, stdout=stream,
                                      stderr=subprocess.STDOUT, timeout=timeout).returncode
            except subprocess.TimeoutExpired:
                stream.write('\nDIAGNOSTIC COMMAND TIMED OUT\n')
                code = -1000
        row = {'command': command, 'returncode': code, 'timeout_seconds': timeout,
               'log': str(log.relative_to(out)), 'log_sha256': sha(log),
               'elapsed_seconds': time.monotonic() - start}
        report['commands'].append(row)
        save()
        print(label, code, flush=True)
        if not allow_failure:
            assert code == 0, (label, code)
        return row, log

    try:
        report['revision'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
        frontend = root / 'neverc/lib/Translate/Cpp/Frontend'
        cmake = (frontend / 'CMakeLists.txt').read_text()
        names = re.search(r'foreach\(_input (.*?)\)\n  file\(SHA256', cmake, re.S).group(1).split()
        assert len(names) == 19
        sources = ['neverc/lib/Translate/Cpp/Frontend/' + name for name in names]
        sources += ['neverc/cmake/modules/BuiltinCppFrontend.cmake',
                    'utils/translate-frontends/cpp/tests/project.py',
                    'utils/ci/FrontendProtocolDriver.cpp', 'utils/ci/check-ordinary-template-call-component.py',
                    '.github/workflows/diagnose-cpp-template-source.yml', 'utils/ci/cpp-ordinary-template-call-component-plan.json']
        report['source_sha256'] = {name: sha(root / name) for name in sources}
        report['exact_private_input_sha256'] = {name: sha(frontend / name) for name in names}
        sdk = root / 'neverc/lib/Translate/Cpp/SDK'
        report['sdk_catalog_sha256'] = sha(sdk / 'catalog.json')
        assert report['sdk_catalog_sha256'] == '1497f7d37860d32d9e53798fbcdc0fe473a4c4e66ac05fa81a45910adfb1d167'
        catalog = json.loads((sdk / 'catalog.json').read_text())
        records = catalog['headers'] + catalog['metadata']
        assert len(records) == 665
        for record in records:
            assert sha(sdk / record['root'] / record['path']) == record['sha256']
        report['sdk_files_verified'] = len(records)
        run(['/usr/bin/clang++-22', '--version'], 'environment/host-compiler')
        run(['cmake', '-S', root / 'llvm', '-B', build, '-G', 'Ninja',
             '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_COMPILER=/usr/bin/clang-22',
             '-DCMAKE_CXX_COMPILER=/usr/bin/clang++-22', '-DLLVM_ENABLE_PROJECTS=neverc',
             '-DNEVERC_CPP_HOST_NM=/usr/bin/llvm-nm-22',
             '-DLLVM_TARGETS_TO_BUILD=X86;AArch64', '-DNEVERC_INCLUDE_TESTS=OFF',
             '-DNEVERC_ENABLE_PYTHON_PLUGINS=OFF', '-DNEVERC_BUNDLE_PYTHON_RUNTIME=OFF',
             '-DNEVERC_CPP_BUILD_JOBS=2', '-DLLVM_INCLUDE_TESTS=OFF',
             '-DLLVM_INCLUDE_EXAMPLES=OFF', '-DLLVM_INCLUDE_BENCHMARKS=OFF',
             '-DLLVM_ENABLE_ZLIB=OFF', '-DLLVM_ENABLE_ZSTD=OFF', '-DLLVM_ENABLE_LIBXML2=OFF',
             '-DLLVM_ENABLE_CURL=OFF', '-DLLVM_ENABLE_TERMINFO=OFF'], 'build/configure-owner', 180)
        # This is the existing production ExternalProject owner, not neverc or
        # its embedded runtimes. It enforces the real LLVM20 pin and isolation.
        run(['cmake', '--build', build, '--target', 'nevercCppFrontendBuild',
             '--parallel', '2'], 'build/actual-private-component', 7200)
        isolated_source = build / '_deps/neverc-cpp-src/clang/lib/Sema/SemaExpr.cpp'
        actual_source = isolated_source.read_bytes()
        probe = build / 'ordinary-call-reapply-source.cpp'
        probe.write_bytes(actual_source)
        source_module = ast.parse((frontend / 'IsolateSymbols.py').read_text())
        owned = next(node for node in source_module.body if isinstance(node, ast.FunctionDef)
                     and node.name == 'preserve_ordinary_template_call_source')
        namespace = {'Path': Path, 're': re}
        exec(compile(ast.Module(body=[owned], type_ignores=[]), '<actual-produced-source-text>', 'exec'), namespace)
        namespace['preserve_ordinary_template_call_source'](probe)
        assert probe.read_bytes() == actual_source
        namespace['preserve_ordinary_template_call_source'](probe)
        assert probe.read_bytes() == actual_source and isolated_source.read_bytes() == actual_source
        report['ordinary_patch_reapplication_verified_on_actual_produced_source'] = True
        report['actual_produced_semaexpr_sha256'] = sha(isolated_source)
        save()
        private = build / '_deps/neverc-cpp-build'
        archive = private / 'lib/Release/libnevercCppFrontend.a'
        assert archive.is_file()
        report['actual_private_archive_sha256'] = sha(archive)
        tests = ['IsolateMathCoffSymbolsTests.py', 'VerifyArchiveMembersTests.py',
                 'AuditArchiveTests.py', 'MsvcRuntimeSymbolsTests.py', 'CoffWeakAliasesTests.py']
        for name in tests:
            mode = '-I' if name == 'VerifyArchiveMembersTests.py' else '-E'
            run(['python3', mode, '-B', frontend / name], 'audit/' + name, 120)
        run(['python3', '-E', '-B', root / 'tests/neverc/HostCoffSymbolsTests.py'], 'audit/HostCoffSymbolsTests.py', 120)
        run(['python3', '-I', '-B', frontend / 'VerifyArchiveMembers.py',
             '--archive', archive, '--archiver', '/usr/bin/llvm-ar-22', '--kind', 'ar'], 'audit/actual-members', 120)
        run(['python3', '-E', '-B', frontend / 'AuditArchive.py', '--nm', private / 'bin/llvm-nm',
             '--archive', archive, '--prefix-header', private / 'NeverCCppPrivatePrefix.h'], 'audit/actual-private-ABI', 600)
        # Preserve real build inputs remotely even if later linking/requests fail.
        # The small evidence artifact never contains this archive or executables.
        _, command_log = run(['ninja', '-C', private, '-t', 'compdb'],
                             'build/private-compile-commands')
        compile_commands = json.loads(command_log.read_text())
        bridge_commands = [entry for entry in compile_commands
            if str(frontend) + '/' in entry['file'] and entry['file'].endswith('.cpp')]
        expected_sources = {str(frontend / name) for name in
            ['Frontend.cpp', 'Lowering.cpp', 'Project.cpp', 'MathSDK.cpp', 'RandomSDK.cpp']}
        assert {entry['file'] for entry in bridge_commands} == expected_sources
        assert len(bridge_commands) == 5
        report['actual_private_bridge_compile_commands'] = bridge_commands
        reproducer = build / 'private-component-reproducer'
        reproducer.mkdir()
        generated = [path for path in private.rglob('BuildID.h')
                     if '#define NEVERC_CPP_BUILD_ID ' in path.read_text()]
        assert len(generated) == 1
        for name in ['BuildID.h', 'BuiltinCppSdkData.h']:
            shutil.copyfile(generated[0].parent / name, reproducer / name)
        producer = {'producer_revision': report['revision'],
            'exact_private_input_sha256': report['exact_private_input_sha256'],
            'actual_private_archive_sha256': report['actual_private_archive_sha256'],
            'sdk_catalog_sha256': report['sdk_catalog_sha256'],
            'llvm_pin': '20.1.8', 'llvm_source_archive_sha256':
                '6898f963c8e938981e6c4a302e83ec5beb4630147c7311183cf61069af16333d',
            'bridge_compile_commands': bridge_commands,
            'generated_header_sha256': {name: sha(reproducer / name)
                for name in ['BuildID.h', 'BuiltinCppSdkData.h']},
            'actual_prefix_sha256': sha(private / 'NeverCCppPrivatePrefix.h'),
            'host_compiler_version_log_sha256': sha(out / 'environment/host-compiler.log'),
            'archive_and_header_artifact_remote_only': True,
            'must_not_download_archive_or_binaries_to_user_host': True,
            'incremental_source_change_requires_new_build_id_all_five_bridge_units_and_archive_audit': True,
            'source_isolation_or_upstream_abi_change_requires_fresh_private_dependency_build': True}
        manifest = reproducer / 'producer.json'
        manifest.write_text(json.dumps(producer, indent=2) + '\n')
        shutil.copyfile(manifest, out / 'private-component-producer.json')
        report['remote_component_producer_manifest_sha256'] = sha(manifest)
        save()
        run(['cmake', '--build', private, '--target', 'llvm-config', '--parallel', '2'],
            'build/actual-private-config-tool', 1800)
        tool = private / 'bin/llvm-config'
        _, log = run([tool, '--version'], 'build/private-tool-version')
        assert log.read_text().strip() == '20.1.8'
        _, log = run([tool, '--system-libs', '--link-static', 'support'], 'build/private-system-libraries')
        system_libraries = shlex.split(log.read_text())
        binary = build / 'private-frontend-protocol'
        run(['/usr/bin/clang++-22', '-std=c++17', '-O2', '-fno-fast-math', '-ffp-contract=off',
             root / 'utils/ci/FrontendProtocolDriver.cpp', '-I' + str(frontend),
             '-Wl,--start-group', archive, '-Wl,--end-group', *system_libraries,
             '-o', binary], 'build/real-c-entry-caller')
        data = binary.read_bytes()
        assert data[:6] == b'\x7fELF\x02\x01' and data[18:20] == b'\x3e\x00'
        header = out / 'actual-frontend-elf-header.bin'
        header.write_bytes(data[:64])
        report['actual_private_executable'] = {'sha256': sha(binary), 'bytes': len(data),
            'remote_path': str(binary), 'header': str(header.relative_to(out)), 'header_sha256': sha(header),
            'separate_remote_binary_artifact_required': True, 'must_not_download_binary_locally': True}
        _, log = run([binary, '__neverc_cpp_frontend', '--version'], 'execution/real-version')
        version = log.read_text().strip()
        assert version.startswith('neverc-cpp-frontend 20.1.8 protocol 1 cpp-frontend-1-')
        report['actual_frontend_version'] = version
        project, _ = run(['python3', root / 'utils/translate-frontends/cpp/tests/project.py',
                           '--neverc', binary, '--target', 'x86_64-unknown-linux-gnu',
                           '--output-dir', out / 'project'], 'execution/original-project-suite',
                         1800, allow_failure=True)
        report['original_project_suite_passed'] = project['returncode'] == 0
        project_root = out / 'project/environment'
        if project_root.exists():
            clean = project_root / 'a.cpp.response.json'
            poison = project_root / 'a.cpp-poisoned.response.json'
            hidden = project_root / 'b.cpp.response.json'
            report['poison_observation'] = {
                'clean_and_poisoned_responses_equal': clean.is_file() and poison.is_file()
                    and json.loads(clean.read_text()) == json.loads(poison.read_text()),
                'implicit_hidden_header_rejected': hidden.is_file() and any(
                    d['code'] == 'TR0203' for d in json.loads(hidden.read_text())['diagnostics'])}
        targets = ['x86_64-apple-macosx15.0.0', 'arm64-apple-macosx15.0.0',
                   'x86_64-unknown-linux-gnu', 'aarch64-unknown-linux-gnu',
                   'i686-unknown-linux-gnu', 'x86_64-pc-windows-msvc',
                   'aarch64-pc-windows-msvc', 'i686-pc-windows-msvc']
        cases = {
            'cstddef-predefines': '#include <cstddef>\n#ifdef _MSC_VER\n#error host MSVC version leaked\n#endif\nint main(){return sizeof(wchar_t)==0;}\n',
            'pair-piecewise-scalars': '#include <tuple>\n#include <utility>\nint main(){std::pair<int,int> p(std::piecewise_construct,std::tuple<int>(3),std::tuple<int>(9));return p.first!=3||p.second!=9;}\n',
            'tuple-get': '#include <tuple>\nint main(){std::tuple<int,int> x(3,9);return std::get<0>(x)!=3||std::get<1>(x)!=9;}\n'}
        for target in targets:
            for name, source in cases.items():
                folder = out / 'focused' / target / name
                folder.mkdir(parents=True)
                input_file, request, response = folder / 'input.cpp', folder / 'request.json', folder / 'response.json'
                input_file.write_text(source)
                request.write_text(json.dumps({'protocol': 1, 'profile': 'cpp-core-v2',
                    'root': str(folder), 'source': str(input_file), 'target': target,
                    'arguments': ['-std=c++17'], 'sdk': {
                        'distribution_id': catalog['distribution_id'],
                        'catalog_sha256': report['sdk_catalog_sha256']}}))
                row, _ = run([binary, '__neverc_cpp_frontend', '--request', request, '--output', response],
                             'execution/focused-' + target + '-' + name, allow_failure=True)
                result = json.loads(response.read_text()) if response.is_file() else None
                codes = [d['code'] for d in result['diagnostics']] if result else []
                report['requests'].append({'target': target, 'name': name,
                    'source_sha256': sha(input_file), 'request': str(request.relative_to(out)),
                    'response': str(response.relative_to(out)) if response.is_file() else None,
                    'response_sha256': sha(response) if response.is_file() else None,
                    'returncode': row['returncode'], 'diagnostic_codes': codes,
                    'passed': row['returncode'] == 0 and result is not None and not codes})
                save()
        plan = json.loads((root / 'utils/ci/cpp-ordinary-template-call-component-plan.json').read_text())
        report['plan'] = plan
        report['source_observations'] = []
        for case in plan['cases']:
            directory = out / 'source-observations' / case['name']
            directory.mkdir(parents=True, exist_ok=True)
            source, request, response = [directory / name for name in ['input.cpp','request.json','response.json']]
            source.write_text(case['source'])
            assert sha(source) == case['source_sha256']
            request.write_text(json.dumps({'protocol':1,'profile':'cpp-core-v2','root':str(directory),
                'source':str(source),'target':'x86_64-unknown-linux-gnu','arguments':['-std=c++17'],
                'sdk':{'distribution_id':catalog['distribution_id'],'catalog_sha256':report['sdk_catalog_sha256']}})+'\n')
            command, log = run([binary,'__neverc_cpp_frontend','--request',request,'--output',response],
                'execution/source-'+case['name'],60,allow_failure=True)
            result = json.loads(response.read_text()) if response.is_file() else None
            codes = [d['code'] for d in result['diagnostics']] if result else []
            valid = bool(result and result.get('protocol')==1 and result.get('profile')=='cpp-core-v2'
                and result.get('frontend',{}).get('build')==version.split()[-1])
            if case['expected']=='accept':
                satisfied = valid and command['returncode']==0 and codes==[]
            elif case['expected']=='reject':
                satisfied = valid and command['returncode']==1 and bool(codes) and all(c in ['TR0201','TR0203'] for c in codes)
            elif case['expected']=='invalid':
                satisfied = valid and command['returncode']==1 and 'TR0202' in codes
            else:
                satisfied = valid and command['returncode'] in [0,1] and all(c in ['TR0201','TR0203'] for c in codes)
            text = log.read_text()
            report['source_observations'].append({'name':case['name'],'expected':case['expected'],
                'source_sha256':sha(source),'returncode':command['returncode'],'diagnostic_codes':codes,
                'request':str(request.relative_to(out)),'response':str(response.relative_to(out)) if response.is_file() else None,
                'response_sha256':sha(response) if response.is_file() else None,
                'selected_ingress':text.count('ORDINARY_SOURCE_SELECTED_BEGIN'),
                'default_ingress':text.count('ORDINARY_SOURCE_DEFAULTS_BEGIN'),
                'expectation_satisfied':bool(satisfied)})
            save()
        inventory = []
        for path in sorted((out / 'executions').rglob('*')):
            if path.is_file():
                inventory.append({'path': str(path.relative_to(out)), 'sha256': sha(path),
                                  'bytes': path.stat().st_size})
        report['all_actual_frontend_invocation_files'] = inventory
        report['actual_sdk_frontend_requests'] = len(report['requests'])
        report['passed'] = report['original_project_suite_passed'] and len(report['requests']) == 24 and all(
            row['passed'] for row in report['requests'])
        report['sdk_and_project_contract_passed'] = report['passed']
        report['source_observation_contract_passed'] = all(v['expectation_satisfied'] for v in report['source_observations'])
        report['private_not_fn_admission_implemented'] = False
        report['not_fn_lowering_implemented'] = False
        report['passed'] = False  # Honest unsupported diagnostic, regardless of observation results.
        report['completed'] = True
        save()
        return 0 if report['passed'] else 1
    except Exception:
        report['error'] = traceback.format_exc()
        save()
        print(report['error'], flush=True)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())

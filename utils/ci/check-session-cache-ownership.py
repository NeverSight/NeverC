"""Compile real Session/Allocator/Diagnostic/Broker code and actual GTests."""
from pathlib import Path
import argparse
import hashlib
import json
import platform
import subprocess
import time
import traceback
import xml.etree.ElementTree as ET


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--clangxx', type=Path, default=Path('/usr/bin/clang++-22'))
    args = parser.parse_args()
    root, out = args.repo.resolve(), args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    report_path = out / 'results.json'
    report = {'passed': False, 'completed': False, 'commands': [], 'variants': [],
              'neverc_compiler_rebuilt': False, 'full_ci_passed': False,
              'whole_global_state_gate_passed': False, 'goal_complete': False,
              'global_state_allowlist_changed': False,
              'original_parallel_and_lto_suites_still_required': True}

    def save():
        report_path.write_text(json.dumps(report, indent=2) + '\n')

    def run(command, label, timeout=60):
        command = list(map(str, command))
        log = out / (label + '.log')
        log.parent.mkdir(parents=True, exist_ok=True)
        started = time.monotonic()
        with log.open('w') as stream:
            try:
                code = subprocess.run(command, cwd=root, stdout=stream,
                                      stderr=subprocess.STDOUT, timeout=timeout).returncode
            except subprocess.TimeoutExpired:
                stream.write('\nDIAGNOSTIC COMMAND TIMED OUT\n')
                code = -1000
        row = {'command': command, 'returncode': code, 'timeout_seconds': timeout,
               'log': str(log.relative_to(out)), 'log_sha256': sha(log),
               'elapsed_seconds': time.monotonic() - started}
        report['commands'].append(row)
        save()
        print(label, code, flush=True)
        assert code == 0, (label, code)
        return log

    try:
        assert platform.system() == 'Linux' and platform.machine() == 'x86_64'
        assert 'VERSION_ID="22.04"' in Path('/etc/os-release').read_text()
        report['revision'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
        baseline = '34ba0cdf175a5f2af44035af766d4189cae2a185'
        report['baseline_revision'] = baseline
        source_names = [
            'neverc/include/neverc/Linker/Core/Runtime/Session.h',
            'neverc/lib/Linker/Core/Runtime/Session.cpp',
            'neverc/lib/Linker/Core/Runtime/Allocator.cpp',
            'neverc/lib/Linker/Core/Runtime/Diagnostic.cpp',
            'neverc/lib/Linker/Core/Runtime/LinkerExecutionContext.cpp',
            'neverc/lib/Foundation/Core/ProcessResourceBroker.cpp',
            'neverc/include/neverc/Linker/Core/Runtime/LinkerParallel.h',
            'neverc/include/neverc/Linker/Core/Runtime/CrashRecovery.h',
            'neverc/include/neverc/Linker/Core/Runtime/Allocator.h',
            'neverc/include/neverc/Linker/Core/Runtime/LinkerExecutionContext.h',
            'tests/neverc/PluginLinkContextTests.cpp',
            'tests/neverc/SessionCacheOwnershipTests.cpp',
            'tests/neverc/Inputs/Linker/SessionCacheLayout.cpp',
            'tests/neverc/CMakeLists.txt',
            'utils/plugin-api/global-state-allowlist.json',
            'utils/plugin-api/check-global-state.py',
            'utils/ci/check-session-cache-ownership.py',
            '.github/workflows/diagnose-cpp-template-source.yml']
        report['source_sha256'] = {name: sha(root / name) for name in source_names}
        for name in source_names:
            if name.endswith(('Allocator.cpp', 'Diagnostic.cpp', 'LinkerExecutionContext.cpp',
                              'ProcessResourceBroker.cpp', 'PluginLinkContextTests.cpp',
                              'LinkerParallel.h', 'CrashRecovery.h', 'Allocator.h',
                              'LinkerExecutionContext.h', 'global-state-allowlist.json',
                              'check-global-state.py')):
                original = subprocess.check_output(['git', 'show', baseline + ':' + name], cwd=root)
                assert hashlib.sha256(original).hexdigest() == report['source_sha256'][name], name
        sdk = root / 'neverc/lib/Translate/Cpp/SDK'
        assert sha(sdk / 'catalog.json') == '1497f7d37860d32d9e53798fbcdc0fe473a4c4e66ac05fa81a45910adfb1d167'
        catalog = json.loads((sdk / 'catalog.json').read_text())
        records = catalog['headers'] + catalog['metadata']
        assert len(records) == 665
        for record in records:
            assert sha(sdk / record['root'] / record['path']) == record['sha256']
        report['sdk_files_verified'] = len(records)
        for name, command in [('python', ['python3', '--version']),
                              ('clang', [args.clangxx, '--version']),
                              ('nm', ['nm', '--version']), ('readelf', ['readelf', '--version'])]:
            run(command, 'environment/' + name)
        old_root = out / 'original'
        for name in source_names[:2]:
            destination = old_root / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(subprocess.check_output(['git', 'show', baseline + ':' + name], cwd=root))
        report['baseline_source_sha256'] = {
            name: sha(old_root / name) for name in source_names[:2]}
        build = out / 'cmake-build'
        hook = out / 'build/session-owner.cmake'
        hook.parent.mkdir(parents=True, exist_ok=True)
        runtime = [root / name for name in source_names[1:6]]
        layout = root / 'tests/neverc/Inputs/Linker/SessionCacheLayout.cpp'
        lines = ['find_package(GTest REQUIRED)']
        target_names = []
        for target, original, mode, test in [
                ('session-layout-original', True, '-O2', False),
                ('session-layout-owned', False, '-O2', False),
                ('session-cache-O0', False, '-O0', True),
                ('session-cache-O2', False, '-O2', True)]:
            sources = list(runtime)
            if original:
                sources[0] = old_root / source_names[1]
            sources += ([root / 'tests/neverc/PluginLinkContextTests.cpp',
                         root / 'tests/neverc/SessionCacheOwnershipTests.cpp'] if test else [layout])
            lines += ['add_executable(' + target, *['  "' + s.as_posix() + '"' for s in sources], ')']
            lines += ['set_target_properties(' + target + ' PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON',
                      '  RUNTIME_OUTPUT_DIRECTORY "' + out.as_posix() + '")']
            includes = ([old_root / 'neverc/include/neverc'] if original else [])
            includes += [root / 'neverc/include', root / 'neverc/include/neverc',
                         root / 'neverc/lib/Foundation/Core', root / 'llvm/include']
            lines += ['target_include_directories(' + target + ' BEFORE PRIVATE',
                      *['  "' + inc.as_posix() + '"' for inc in includes],
                      '  "${CMAKE_BINARY_DIR}/include" "${CMAKE_BINARY_DIR}/tools/neverc/include")',
                      'target_compile_options(' + target + ' PRIVATE ' + mode + ' -g0 -fexceptions -fno-fast-math -ffp-contract=off)',
                      'target_link_libraries(' + target + ' PRIVATE LLVMCore LLVMSupport ' +
                      ('GTest::gtest_main GTest::gtest' if test else '') + ')']
            target_names.append(target)
        hook.write_text('\n'.join(lines) + '\n')
        report['cmake_owner'] = {'path': str(hook.relative_to(out)), 'sha256': sha(hook),
                                'libraries': ['LLVMCore', 'LLVMSupport'],
                                'actual_runtime_and_foundation_translation_units': source_names[1:6],
                                'library_stubs_used': False, 'apt_llvm_libraries_used': False}
        run(['cmake', '-S', root / 'llvm', '-B', build, '-G', 'Ninja',
             '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_COMPILER=/usr/bin/clang-22',
             '-DCMAKE_CXX_COMPILER=' + str(args.clangxx),
             '-DCMAKE_PROJECT_LLVM_INCLUDE=' + str(hook),
             '-DLLVM_ENABLE_PROJECTS=neverc', '-DLLVM_TARGETS_TO_BUILD=X86;AArch64',
             '-DLLVM_INCLUDE_TESTS=OFF', '-DNEVERC_INCLUDE_TESTS=OFF',
             '-DNEVERC_ENABLE_PYTHON_PLUGINS=OFF', '-DNEVERC_BUNDLE_PYTHON_RUNTIME=OFF',
             '-DLLVM_INCLUDE_EXAMPLES=OFF', '-DLLVM_INCLUDE_BENCHMARKS=OFF',
             '-DLLVM_INCLUDE_UTILS=OFF', '-DLLVM_ENABLE_ASSERTIONS=ON',
             '-DLLVM_ENABLE_ZLIB=OFF', '-DLLVM_ENABLE_ZSTD=OFF',
             '-DLLVM_ENABLE_LIBXML2=OFF', '-DLLVM_ENABLE_CURL=OFF',
             '-DLLVM_ENABLE_TERMINFO=OFF'], 'build/configure', 180)
        run(['cmake', '--build', build, '--target', *target_names, '--verbose', '--parallel', '4'],
            'build/real-runtime-and-gtest', 1800)
        layouts = []
        for target in target_names[:2]:
            log = run([out / target], 'layout/' + target)
            layouts.append(json.loads(log.read_text()))
        assert layouts[0] == layouts[1], layouts
        report['actual_original_and_owned_layout'] = layouts
        expected = [
            'PluginLinkContextTest.CleanupAndArenaObjectsFinishExactlyOnce',
            'PluginLinkContextTest.NestedExecutionRestoresOuterContext',
            'PluginLinkContextTest.NestedExecutionRestoresOuterWorkerSlot',
            'PluginLinkContextTest.WorkerBindingRestoresReusedThreadTLS',
            *['SessionCacheOwnershipTest.' + name for name in [
                'GuardBorrowClearsOnReusedForeignThread', 'NestedContextsRestoreCachedArenaAcrossUnwind',
                'ReusedContextAddressDoesNotRetainArenas', 'MoreThan64TagsUseOwnedFallback',
                'FinalizationJoinsWorkersBeforeCleanupAndRelease', 'ConcurrentContextsOwnDifferentWorkerArenas',
                'CrashBackendCleanupRestoresBorrowAndSlot', 'AbandonRestoresPreviousBorrowBeforeProcessExit']]]
        report['expected_test_names'] = expected
        for target in target_names:
            binary = out / target
            data = binary.read_bytes()
            assert data[:6] == b'\x7fELF\x02\x01' and data[18:20] == b'\x3e\x00'
            row = {'binary': {'path': target, 'sha256': sha(binary), 'size': len(data)}, 'xml': []}
            if target.startswith('session-cache'):
                for repeat in range(3):
                    xml = out / 'gtest' / (target + '-' + str(repeat) + '.xml')
                    xml.parent.mkdir(parents=True, exist_ok=True)
                    run([binary, '--gtest_shuffle', '--gtest_random_seed=' + str(17 + repeat),
                         '--gtest_output=xml:' + str(xml)], 'gtest/' + target + '-' + str(repeat))
                    cases = ET.parse(xml).getroot().findall('.//testcase')
                    assert sorted(c.attrib['classname'] + '.' + c.attrib['name'] for c in cases) == sorted(expected)
                    assert not any(c.find('failure') is not None or c.find('skipped') is not None for c in cases)
                    row['xml'].append({'path': str(xml.relative_to(out)), 'sha256': sha(xml), 'count': len(cases)})
            nm = run(['nm', '-C', '-S', '--defined-only', binary], 'symbols/' + target + '-nm')
            table = run(['readelf', '--syms', '--wide', '--demangle', binary], 'symbols/' + target + '-readelf')
            row['nm'] = str(nm.relative_to(out))
            row['readelf'] = str(table.relative_to(out))
            text = nm.read_text()
            old_cache = '(anonymous namespace)::WorkerCache'
            if target == 'session-layout-original':
                assert old_cache in text and old_cache + 'Epoch' in text
                row['legacy_mutable_payload_observed'] = True
            else:
                assert old_cache not in text and 'WorkerCacheEpoch' not in text
                tls = [line.split(maxsplit=7) for line in table.read_text().splitlines() if
                       line.endswith('linker::session_detail::ActiveWorkerCache')]
                assert len(tls) == 1 and tls[0][3] == 'TLS' and int(tls[0][2]) == 8, tls
                row['actual_nonowning_tls_binding_size'] = 8
                row['legacy_epoch_and_tls_payload_absent'] = True
            report['variants'].append(row)
            save()
        report.update(passed=True, completed=True, actual_test_cases=72,
                      actual_original_test_cases=24, actual_new_test_cases=48,
                      actual_native_test_processes=6, actual_layout_processes=2,
                      internal_layout_verified_for='Linux x86_64 Clang22/libstdc++ only')
        save()
        return 0
    except Exception:
        report['error'] = traceback.format_exc()
        save()
        print(report['error'], flush=True)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())

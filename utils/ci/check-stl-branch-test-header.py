"""Compile and run the unchanged consolidated test through its actual owning include root."""
from pathlib import Path
import argparse
import hashlib
import json
import shlex
import subprocess
import time
import traceback
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root, out = args.repo.resolve(), args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    report = {'passed': False, 'completed': False, 'commands': [], 'variants': [],
              'neverc_compiler_rebuilt': False, 'full_backend_link_verified': False,
              'original_cli_crashes_fixed': False, 'library_stubs_used': False,
              'apt_llvm_library_abi_mixed': False, 'full_ci_passed': False, 'goal_complete': False}

    def sha(path):
        return hashlib.sha256(Path(path).read_bytes()).hexdigest()

    def save():
        (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')

    def run(argv, name, timeout=60):
        argv = list(map(str, argv))
        log = out / (name + '.log')
        log.parent.mkdir(parents=True, exist_ok=True)
        start = time.monotonic()
        with log.open('w') as stream:
            try:
                code = subprocess.run(argv, cwd=root, stdout=stream,
                                      stderr=subprocess.STDOUT, timeout=timeout).returncode
            except subprocess.TimeoutExpired:
                stream.write('\nDIAGNOSTIC COMMAND TIMED OUT\n')
                code = -1000
        report['commands'].append({'command': argv, 'returncode': code,
            'timeout_seconds': timeout, 'log': str(log.relative_to(out)),
            'log_sha256': sha(log), 'elapsed_seconds': time.monotonic() - start})
        save()
        print(name, code, flush=True)
        assert code == 0, (name, code)
        return log

    save()
    try:
        report['source_only_include_fix'] = True
        report['neverc_or_private_frontend_rebuilt'] = False
        report['revision'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
        names = ['neverc/include/neverc/Linker/MachO/DataConst.h',
                 'neverc/lib/Linker/Backends/MachO/Driver/MachODriver.cpp',
                 'tests/neverc/MachODataConstTests.cpp', 'tests/neverc/CMakeLists.txt',
                 'utils/ci/check-stl-branch-test-header.py', '.github/workflows/diagnose-cpp-template-source.yml', 'utils/ci/stl-branch-test-header-plan.json']
        report['source_sha256'] = {name: sha(root / name) for name in names}
        run(['/usr/bin/clang++-22', '--version'], 'environment/clang')
        hook = out / 'build/data-const-owner.cmake'
        hook.parent.mkdir(parents=True)
        lines = ['find_package(GTest REQUIRED)']
        targets = ['macho-data-const-O0', 'macho-data-const-O2']
        for target, mode in zip(targets, ['-O0', '-O2']):
            lines += ['add_executable(' + target + ' "' + (root / names[2]).as_posix() + '")',
                'set_target_properties(' + target + ' PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON RUNTIME_OUTPUT_DIRECTORY "' + out.as_posix() + '")',
                'target_include_directories(' + target + ' BEFORE PRIVATE "' + (root / 'neverc/include').as_posix() + '" "' + (root / 'llvm/include').as_posix() + '" "${CMAKE_BINARY_DIR}/include")',
                'target_compile_options(' + target + ' PRIVATE ' + mode + ' -g0 -fno-fast-math -ffp-contract=off)',
                'target_link_libraries(' + target + ' PRIVATE LLVMSupport GTest::gtest_main GTest::gtest)']
        lines.append('file(GENERATE OUTPUT \"' + (out / 'build/llvm-support-target.txt').as_posix() + '\" CONTENT \"$<TARGET_FILE:LLVMSupport>\\n\")')
        hook.write_text('\n'.join(lines) + '\n')
        report['cmake_owner'] = {'path': str(hook.relative_to(out)), 'sha256': sha(hook),
                                 'libraries': ['LLVMSupport', 'GTest::gtest_main', 'GTest::gtest']}
        build = out / 'cmake-build'
        run(['cmake', '-S', root / 'llvm', '-B', build, '-G', 'Ninja',
             '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
             '-DCMAKE_C_COMPILER=/usr/bin/clang-22', '-DCMAKE_CXX_COMPILER=/usr/bin/clang++-22',
             '-DCMAKE_PROJECT_LLVM_INCLUDE=' + str(hook), '-DLLVM_ENABLE_PROJECTS=neverc',
             '-DLLVM_TARGETS_TO_BUILD=X86;AArch64', '-DLLVM_INCLUDE_TESTS=OFF',
             '-DNEVERC_INCLUDE_TESTS=OFF', '-DNEVERC_ENABLE_PYTHON_PLUGINS=OFF',
             '-DNEVERC_BUNDLE_PYTHON_RUNTIME=OFF', '-DLLVM_INCLUDE_EXAMPLES=OFF',
             '-DLLVM_INCLUDE_BENCHMARKS=OFF', '-DLLVM_ENABLE_ASSERTIONS=ON',
             '-DLLVM_ENABLE_ZLIB=OFF', '-DLLVM_ENABLE_ZSTD=OFF',
             '-DLLVM_ENABLE_LIBXML2=OFF', '-DLLVM_ENABLE_CURL=OFF',
             '-DLLVM_ENABLE_TERMINFO=OFF'], 'build/configure', 180)
        commands = json.loads((build / 'compile_commands.json').read_text())
        selected = [c for c in commands if Path(c['file']).resolve() == (root / names[2]).resolve()]
        assert len(selected) == 2
        for entry in selected:
            argv = entry.get('arguments') or shlex.split(entry['command'])
            assert '-I' + str(root / 'neverc/include') in argv
            assert '-I' + str(root / 'neverc/include/neverc') not in argv
        report['actual_test_compile_commands'] = selected
        run(['cmake', '--build', build, '--target', *targets, '--verbose', '--parallel', '4'],
            'build/actual-policy-and-gtest', 1800)
        support_marker = out / 'build/llvm-support-target.txt'
        support = Path(support_marker.read_text().strip()).resolve()
        assert support.is_relative_to(build.resolve()) and support.is_file()
        report['actual_root_support_archive'] = {'path':str(support.relative_to(out)), 'sha256':sha(support), 'bytes':support.stat().st_size, 'cmake_target':'LLVMSupport', 'target_marker':str(support_marker.relative_to(out)), 'target_marker_sha256':sha(support_marker)}
        run(['ar', 't', support], 'build/actual-root-support-members')
        expected = {'MachODataConstTest.SpecialImageKindsHaveWritableDataByDefault',
                    'MachODataConstTest.ExistingExecutableAndLibraryDefaultsArePreserved',
                    'MachODataConstTest.OlderMacosDeploymentKeepsDataWritable'}
        for target in targets:
            program = out / target
            raw = program.read_bytes()
            assert raw[:6] == b'\x7fELF\x02\x01' and raw[18:20] == b'\x3e\x00'
            (out / (target + '.elf-header')).write_bytes(raw[:64])
            xml = out / 'gtest' / (target + '.xml')
            xml.parent.mkdir(exist_ok=True)
            run([program, '--gtest_output=xml:' + str(xml)], 'gtest/' + target)
            cases = ET.parse(xml).getroot().findall('.//testcase')
            assert {c.attrib['classname'] + '.' + c.attrib['name'] for c in cases} == expected
            assert len(cases) == 3 and not any(c.find('failure') is not None or c.find('skipped') is not None for c in cases)
            report['variants'].append({'binary': target, 'binary_sha256': sha(program),
                'binary_bytes': len(raw), 'xml': str(xml.relative_to(out)), 'xml_sha256': sha(xml),
                'actual_test_count': len(cases)})
        report.update(completed=True, passed=True, actual_native_test_processes=2,
                      actual_gtest_cases=6, actual_production_macho_driver_object_compiled=False, actual_owning_include_root_verified=True)
        save()
        return 0
    except Exception:
        report['error'] = traceback.format_exc()
        save()
        print(report['error'], flush=True)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())

"""Replay original Windows STL fixtures against an authenticated existing compiler."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import platform
import re
import stat
import subprocess
import traceback
import xml.etree.ElementTree as ET
import zipfile


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    assert os.environ.get('GITHUB_ACTIONS') == 'true' and platform.system() == 'Windows'
    root, out = args.repo.resolve(), args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    plan = json.loads((root / 'utils/ci/windows-stl-diagnostic-plan.json').read_text())
    report = dict(diagnostic_only=True, full_stl_complete=False, completed=False,
                  revision=os.environ['GITHUB_SHA'], plan=plan, commands=[], gtests=[], abi_queries=[])

    def run(command, label, timeout=600, environment=None):
        command = list(map(str, command))
        result = subprocess.run(command, capture_output=True, timeout=timeout, env=environment)
        log = out / (label + '.log')
        log.write_bytes(result.stdout + result.stderr)
        report['commands'].append(dict(command=command, label=label, returncode=result.returncode,
                                       log=log.name, log_sha256=sha(log)))
        print(label, result.returncode, flush=True)
        return result.returncode, result.stdout + result.stderr

    try:
        for relative, identity in plan['input_sha256'].items():
            assert sha(root / relative) == identity, relative
        helper = root / 'utils/ci/download-windows-arm-sdk-baseline.py'
        spec = importlib.util.spec_from_file_location('_authenticated_native_download', helper)
        download = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(download)
        token = os.environ['GITHUB_TOKEN']
        producer = download.request('actions/runs/42953231157', token)
        assert producer['head_sha'] == plan['compiler_revision'] and producer['run_attempt'] == 1
        job = download.request('actions/jobs/130302211336', token)
        assert job['run_id'] == producer['id'] and job['head_sha'] == plan['compiler_revision']
        for name in ['Build neverc (Clang + LTO)', 'Install neverc', 'Upload Compiler Artifact']:
            rows = [item for item in job['steps'] if item['name'] == name]
            assert len(rows) == 1 and rows[0]['conclusion'] == 'success', name
        artifact = download.request('actions/artifacts/13834360945', token)
        assert artifact['workflow_run']['id'] == producer['id']
        assert artifact['workflow_run']['head_sha'] == plan['compiler_revision']
        assert not artifact['expired'] and artifact['name'] == 'windows-x64-neverc-clang-lto-compiler'
        assert artifact['digest'] == 'sha256:' + plan['compiler_artifact_sha256']
        # Binaries stay on this native runner; the result artifact contains text only.
        work = out.parent / 'windows-stl-native-work'
        work.mkdir(exist_ok=False)
        outer = work / 'compiler-artifact.zip'
        download.request('actions/artifacts/13834360945/zip', token, outer)
        assert sha(outer) == plan['compiler_artifact_sha256']
        with zipfile.ZipFile(outer) as archive:
            assert archive.namelist() == ['windows-x64-neverc-clang-lto-compiler.zip']
            package = work / archive.namelist()[0]
            with archive.open(archive.namelist()[0]) as source, package.open('xb') as target:
                while chunk := source.read(1024 * 1024):
                    target.write(chunk)
        install = work / 'unpacked'
        install.mkdir()
        download.check_zip(package, install)
        with zipfile.ZipFile(package) as archive:
            assert all(not stat.S_ISLNK(item.external_attr >> 16) for item in archive.infolist())
            archive.extractall(install)
        compiler = install / 'install/bin/neverc.exe'
        assert compiler.is_file()
        report['compiler'] = dict(path=str(compiler), sha256=sha(compiler),
            producer_run_id=producer['id'], producer_job_id=job['id'], artifact_id=artifact['id'],
            artifact_sha256=sha(outer), package_sha256=sha(package),
            producer_suite_conclusion=producer['conclusion'], producer_full_suite_passed=False)
        code, _ = run([compiler, '--version'], 'compiler-version')
        assert code == 0

        # Compile the actual fixture twice. Only its Windows temporary-directory
        # construction differs; selected original test and helper bytes are fixed.
        build_source = work / 'harness-source'
        build_source.mkdir()
        cmake = r'''cmake_minimum_required(VERSION 3.20)
project(NeverCWindowsStlFixture LANGUAGES C CXX)
include(FetchContent)
FetchContent_Declare(googletest GIT_REPOSITORY https://github.com/google/googletest.git GIT_TAG v1.17.0)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
foreach(version IN ITEMS before after)
  add_executable(fixture-${version} "${TEST_SOURCE_${version}}" "${FIXTURE_${version}}")
  target_compile_features(fixture-${version} PRIVATE cxx_std_17)
  target_include_directories(fixture-${version} PRIVATE "${FIXTURE_INCLUDE}")
  target_compile_definitions(fixture-${version} PRIVATE
    NEVERC_BINARY=\"${NEVERC_EXTERNAL_BINARY}\" TEST_SOURCE_DIR=\"${TEST_DIR}\" TEST_C_DIR=\"${TEST_C_DIR}\")
  target_link_libraries(fixture-${version} PRIVATE GTest::gtest_main)
endforeach()
'''
        (build_source / 'CMakeLists.txt').write_text(cmake)
        build = work / 'harness-build'
        config = ['cmake', '-S', build_source, '-B', build, '-G', 'Ninja',
                  '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_COMPILER=clang-cl', '-DCMAKE_CXX_COMPILER=clang-cl',
                  '-DNEVERC_EXTERNAL_BINARY=' + compiler.as_posix(),
                  '-DTEST_SOURCE_before=' + (root / 'utils/ci/windows-stl-fixture-before-tests.cpp').as_posix(),
                  '-DTEST_SOURCE_after=' + (root / 'utils/ci/windows-stl-fixture-after-tests.cpp').as_posix(),
                  '-DFIXTURE_before=' + (root / 'utils/ci/windows-stl-fixture-before.cpp').as_posix(),
                  '-DFIXTURE_after=' + (root / 'tests/neverc/NeverCTestFixture.cpp').as_posix(),
                  '-DFIXTURE_INCLUDE=' + (root / 'tests/neverc').as_posix(),
                  '-DTEST_DIR=' + (root / 'tests/neverc').as_posix(),
                  '-DTEST_C_DIR=' + (root / 'tests/c').as_posix()]
        code, _ = run(config, 'configure-harness')
        assert code == 0
        code, _ = run(['cmake', '--build', build, '--parallel', '2'], 'build-harness')
        assert code == 0
        report['harness_binaries'] = {v: sha(build / ('fixture-' + v + '.exe')) for v in ['before', 'after']}
        environment = dict(os.environ)
        # Match the ordinary user temp directory used by the original failed job.
        native_temp = Path(os.environ['LOCALAPPDATA']) / 'Temp'
        assert native_temp.is_dir()
        environment.update(TMP=str(native_temp), TEMP=str(native_temp))
        for version, names in [('before', plan['selected_tests']), ('after', plan['selected_tests'])]:
            for index, name in enumerate(names):
                xml = out / (version + '-test-' + str(index) + '.xml')
                code, log = run([build / ('fixture-' + version + '.exe'), '--gtest_filter=' + name,
                    '--gtest_output=xml:' + str(xml)], version + '-test-' + str(index), environment=environment)
                assert xml.is_file()
                tree = ET.parse(xml).getroot()
                cases = list(tree.iter('testcase'))
                assert len(cases) == 1 and cases[0].get('name') == name.split('.', 1)[1]
                assert cases[0].get('status') == 'run' and cases[0].get('result') == 'completed'
                failures = list(cases[0].iter('failure'))
                assert (code == 0) == (not failures)
                text = log.decode(errors='replace')
                if version == 'before' and index == 0:
                    assert failures and 'could not write test input ' in text
                elif version == 'before' and index == 1:
                    assert failures and 'TR0202' in text and 'conflicting types' in text
                elif version == 'before':
                    assert code == 0
                report['gtests'].append(dict(version=version, name=name, returncode=code,
                    failure_count=len(failures), xml=xml.name, xml_sha256=sha(xml)))
        report['fixture_original_red_and_fixed_green'] = (report['gtests'][0]['returncode'] != 0 and
            report['gtests'][3]['returncode'] == 0)
        report['maintenance_native_tests_passed'] = all(x['returncode'] == 0 for x in report['gtests'] if x['version'] == 'after')
        report['actual_pair_c23_executions'] = sum(2 for x in report['gtests'] if x['name'] == plan['selected_tests'][2] and x['returncode'] == 0)


        sdk = root / 'neverc/lib/Translate/Cpp/SDK'
        for relative, identity in plan['sdk_input_sha256'].items():
            assert sha(sdk / relative) == identity, relative
        for relative, identity in plan['exact_private_input_sha256'].items():
            assert sha(root / 'neverc/lib/Translate/Cpp/Frontend' / relative) == identity, relative

        # Execute the original protocol check function and new cleanup assertions
        # verbatim. The proxy only records actual subprocess evidence.
        import types
        focus = json.loads((root / 'utils/ci/windows-stl-protocol-focus.json').read_text())
        report['protocol_cases'] = []
        class RecordedSubprocess:
            @staticmethod
            def run(command, **kwargs):
                result = subprocess.run(command, **kwargs)
                data = (result.stdout + result.stderr)
                if isinstance(data, str):
                    data = data.encode('utf-8')
                label = 'protocol-process-' + str(len(report['commands']))
                log = out / (label + '.log')
                log.write_bytes(data)
                report['commands'].append(dict(command=list(map(str, command)), label=label,
                    returncode=result.returncode, log=log.name, log_sha256=sha(log)))
                return result

        nl = chr(10)
        protocol = ('def run_focus():' + nl + '    count = 0' + nl + '    ' + focus['check_function'] + nl +
            focus['positive_block'] + '    check(' + repr(focus['negative_name']) + ', ' +
            repr(focus['negative_source']) + ', ' + repr(focus['negative_code']) +
            ', profile="cpp-core-v2", sdk=True)' + nl + '    return count' + nl)
        for index, target in enumerate(plan['target_abis']):
            directory = work / ('protocol-target-' + str(index))
            namespace = dict(json=json, subprocess=RecordedSubprocess, sdk_identity=focus['sdk_identity'],
                args=types.SimpleNamespace(neverc=compiler, target=target, output_dir=directory))
            exec(compile(focus['walk_function'], '<original-protocol-walk>', 'exec'), namespace)
            exec(compile(protocol, '<original-protocol-check-and-pair-assertions>', 'exec'), namespace)
            count = namespace['run_focus']()
            assert count == 2
            for source_dir in sorted(directory.iterdir()):
                response = source_dir / 'response.json'
                request = source_dir / 'request.json'
                source = source_dir / 'input.cpp'
                label = 'protocol-' + str(index) + '-' + source_dir.name
                saved = out / (label + '.json')
                saved.write_bytes(response.read_bytes())
                report['protocol_cases'].append(dict(target=target, name=source_dir.name,
                    source_sha256=sha(source), request_sha256=sha(request),
                    response=saved.name, response_sha256=sha(saved)))
        assert len(report['protocol_cases']) == 12

        reference = Path(r'C:\Program Files\LLVM\bin\clang++.exe')
        code, data = run([reference, '--version'], 'reference-version')
        assert code == 0 and b'20.1.8' in data
        query = work / 'sdk-layout.cpp'
        query.write_text('#include <optional>\n#include <tuple>\n'
            '#ifdef _LIBCPP_ABI_MICROSOFT\n#define ABI 1\n#else\n#define ABI 0\n#endif\n'
            '#ifdef _MSC_VER\n#define MSC _MSC_VER\n#else\n#define MSC 0\n#endif\n'
            'struct E{};\ntemplate<unsigned long long...>struct actual_sdk_layout;\n'
            'actual_sdk_layout<sizeof(std::optional<bool>),sizeof(std::optional<int>),'
            'sizeof(std::optional<void*>),alignof(std::optional<int>),sizeof(std::tuple<>),'
            'sizeof(std::tuple<E,E>),__has_declspec_attribute(empty_bases),ABI,MSC> actual;\n')
        overlay = work / 'microsoft-reference-only'
        overlay.mkdir()
        config = root / 'utils/ci/windows-stl-microsoft-reference-config.h'
        (overlay / '__config_site').write_bytes(config.read_bytes())
        assert sha(overlay / '__config_site') == plan['reference_overlay_sha256']
        reference_env = dict(os.environ)
        for name in ['CPATH', 'CPLUS_INCLUDE_PATH', 'C_INCLUDE_PATH', 'OBJC_INCLUDE_PATH', 'SDKROOT',
                     'CCC_OVERRIDE_OPTIONS', 'CLANG_CONFIG_FILE_SYSTEM_DIR', 'CLANG_CONFIG_FILE_USER_DIR']:
            reference_env.pop(name, None)
        for target in [t for t in plan['target_abis'] if 'windows' in t]:
            for force_microsoft, declspec in [(False, False), (False, True), (True, False), (True, True)]:
                extra = (['-fdeclspec'] if declspec else [])
                if force_microsoft:
                    extra += ['-isystem', overlay]
                command = [reference, *plan['reference_base_arguments'], target,
                    *plan['reference_core_arguments'], '-Xclang', '-fms-compatibility-version=0',
                    *extra, '-resource-dir', sdk / 'resource', '-isystem', sdk / 'libcxx',
                    '-isystem', sdk / 'resource/include', query]
                code, data = run(command, 'reference-layout-' + str(len(report['abi_queries'])), environment=reference_env)
                assert code != 0
                text = data.decode(errors='replace')
                values = re.findall(r'actual_sdk_layout<([^>]+)>', text)
                assert values, text[:5000]
                report['abi_queries'].append(dict(target=target, force_microsoft=force_microsoft,
                    declspec=declspec, actual_layout_diagnostics=values, source_sha256=sha(query),
                    reference_only=True, production_sdk_unchanged=True))
        assert len(report['abi_queries']) == 8
        assert report['maintenance_native_tests_passed'] and report['fixture_original_red_and_fixed_green']
        report['completed'] = True
        report['fixture_fix_validated'] = True
        return 0
    except Exception:
        report['failure'] = traceback.format_exc()
        print(report['failure'], flush=True)
        return 1
    finally:
        save(out / 'results.json', report)


if __name__ == '__main__':
    raise SystemExit(main())

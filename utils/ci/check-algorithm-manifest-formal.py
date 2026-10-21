"""Replay the repaired original manifest GTest with real repository owners."""
from pathlib import Path
import argparse
from cpp_diagnostic_gtest import build_gtest
import hashlib
import json
import os
import re
import shutil
import signal
import subprocess
import time
import traceback
import xml.etree.ElementTree as ET


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    args = parser.parse_args()
    root, out, build = args.repo.resolve(), args.output.resolve(), args.build.resolve()
    out.mkdir(parents=True)
    build.mkdir(parents=True)
    r = {'passed': False, 'completed': False, 'commands': [], 'native_runs': [],
         'compiler_rebuilt': False, 'library_stubs_used': False,
         'incompatible_apt_llvm_libraries_used': False, 'full_124_formal_gate_passed': False,
         'frontend_execution_timeout_seconds': 60, 'full_ci_passed': False,
         'published_to_dev': False, 'goal_complete': False}

    def save():
        (out / 'results.json').write_text(json.dumps(r, indent=2) + '\n')

    def run(command, label, timeout=60, env=None):
        command = list(map(str, command))
        log = out / (label + '.log')
        log.parent.mkdir(parents=True, exist_ok=True)
        started = time.monotonic()
        with log.open('w') as stream:
            process = subprocess.Popen(command, cwd=root, env=env,
                stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                code = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
                stream.write('\nDIAGNOSTIC COMMAND TIMED OUT; OWNED PROCESS GROUP STOPPED\n')
                code = -1000
        r['commands'].append({'command': command, 'returncode': code, 'timeout_seconds': timeout,
            'cwd': str(root), 'log': str(log.relative_to(out)), 'log_sha256': sha(log),
            'elapsed_seconds': time.monotonic() - started})
        save()
        print(label, code, flush=True)
        return code

    save()
    try:
        plan = json.loads((root / 'utils/ci/cpp-algorithm-manifest-formal-plan.json').read_text())
        r['plan'] = plan
        r['revision'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
        r['source_sha256'] = {name: sha(root / name) for name in plan['source_sha256']}
        assert r['source_sha256'] == plan['source_sha256']
        baseline = json.loads(args.baseline.read_text())
        assert baseline['revision'] == '1617f18ead74a8fa719e3d1bd02f9c84a10de551'
        assert baseline['run_id'] == 39244936536 and baseline['artifact_id'] == 12195306550
        assert baseline['artifact_sha256'] == '9332fad0d0226257f109ae74c1655a4e35510783d1029dd2bc8bdc15bfaf5206'
        compiler = Path(baseline['compiler'])
        assert sha(compiler) == baseline['compiler_sha256'] == 'fc803587bb5ad6997f9921a05da06c79816f510e8cb9a3185f21cd0a8eb1ad84'
        r['baseline_provenance'] = baseline
        sdk = root / 'neverc/lib/Translate/Cpp/SDK'
        assert sha(sdk / 'catalog.json') == plan['sdk_catalog_sha256'] == '1497f7d37860d32d9e53798fbcdc0fe473a4c4e66ac05fa81a45910adfb1d167'
        catalog = json.loads((sdk / 'catalog.json').read_text())
        records = catalog['headers'] + catalog['metadata']
        assert len(records) == 665
        for row in records:
            assert sha(sdk / row['root'] / row['path']) == row['sha256']
        r['sdk_files_verified'] = 665
        fixture = root / plan['dependency_fixture']
        assert sha(fixture) == plan['dependency_fixture_sha256'] == '70f2b7b28316247f1b3ae06e4274581e2aa202ad1a6a541aca7f93b740d33ca8'
        expected_dependencies = json.loads(fixture.read_text())
        assert len(expected_dependencies) == 447
        source = (root / 'tests/neverc/TranslateTests.cpp').read_bytes()
        matches = list(re.finditer(rb'(?m)^TEST_F\(\s*TranslateTest\s*,\s*(\w+)\s*\)\s*\{', source))
        assert len(matches) == plan['original_test_name_count'] == 7493
        index = next(i for i, match in enumerate(matches) if match.group(1).decode() == plan['test_name'])
        body = source[matches[index].start():matches[index + 1].start()]
        assert hashlib.sha256(body).hexdigest() == plan['repaired_test_body_sha256']
        program = re.search(rb'writeFile\(Source, R"cpp\((.*?)\)cpp"\);', body, re.S).group(1)
        assert hashlib.sha256(program).hexdigest() == plan['original_program_sha256']
        prefix = source[:matches[0].start()]
        assert hashlib.sha256(prefix).hexdigest() == plan['original_fixture_prefix_sha256']
        cleanup = b'''
  void TearDown() override {
    if (const char *Directory = std::getenv("NEVERC_ALGORITHM_DIAGNOSTIC_RETAIN")) {
      std::error_code Error;
      fs::create_directories(Directory, Error);
      if (!Error)
        fs::copy(tmp(), fs::path(Directory) / tmp().filename(),
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing,
                 Error);
      EXPECT_FALSE(Error) << Error.message();
    }
    NeverCTest::TearDown();
  }
'''
        at = prefix.rindex(b'\n};')
        generated_bytes = prefix[:at] + cleanup + prefix[at:] + body + b'\n} // namespace\n'
        generated = root / 'tests/neverc/AlgorithmManifestFormalReplay.cpp'
        generated.write_bytes(generated_bytes)
        retained_source = out / generated.name
        retained_source.write_bytes(generated_bytes)
        r['generated_source_sha256'] = sha(retained_source)
        r['original_test_body_byte_preserved'] = True
        r['cleanup_override_after_assertions_only'] = True
        assert run(['/usr/bin/clang++-22', '--version'], 'environment/clang') == 0
        assert run([compiler, '--version'], 'environment/compiler') == 0
        binary, owner = build_gtest(root, build, '/usr/bin/clang++-22', compiler,
            [generated, root / 'tests/neverc/NeverCTestFixture.cpp'], ['nevercTranslate', 'LLVMSupport'],
            'algorithm-manifest-formal-tests', run, 'build/configure-real-owner', 1200)
        r['cmake_gtest_owner'] = owner
        hook = build / owner['path']
        shutil.copyfile(hook, out / 'real-gtest-owner.cmake')
        raw = binary.read_bytes()
        assert raw[:6] == b'\x7fELF\x02\x01' and raw[18:20] == b'\x3e\x00'
        (out / 'actual-gtest-header.bin').write_bytes(raw[:64])
        r['gtest_binary'] = {'sha256': sha(binary), 'bytes': len(raw), 'remote_only': True}
        assert run([binary, '--gtest_list_tests'], 'gtest/list') == 0
        assert (out / 'gtest/list.log').read_text().splitlines()[-2:] == ['TranslateTest.', '  ' + plan['test_name']]
        retained = build / 'original-fixture-products'
        env = dict(os.environ, NEVERC_ALGORITHM_DIAGNOSTIC_RETAIN=str(retained))
        xml = out / 'gtest/algorithm-manifest.xml'
        status = run([binary, '--gtest_filter=TranslateTest.' + plan['test_name'],
                      '--gtest_output=xml:' + str(xml)], 'gtest/original-repaired-body', 360, env)
        cases = ET.parse(xml).getroot().findall('.//testcase')
        assert len(cases) == 1
        case = cases[0]
        assert case.attrib['classname'] == 'TranslateTest' and case.attrib['name'] == plan['test_name']
        assert case.attrib.get('status') == 'run' and case.attrib.get('result') == 'completed'
        assert case.find('failure') is None and case.find('skipped') is None and status == 0
        r['formal_xml'] = {'path': str(xml.relative_to(out)), 'sha256': sha(xml), 'real_test_cases': 1}
        roots = [path.parent for path in retained.rglob('algorithm-standard-comparator-ordered-ranges.cpp')]
        assert len(roots) == 1
        products = roots[0]
        assert sha(products / 'algorithm-standard-comparator-ordered-ranges.cpp') == plan['original_program_sha256']
        nc = products / 'algorithm-standard-comparator-ordered-ranges.nc'
        manifest = Path(str(nc) + '.manifest.json')
        actual_sdk = json.loads(manifest.read_text())['sdk']
        assert actual_sdk['catalog_sha256'] == plan['sdk_catalog_sha256']
        assert sorted(actual_sdk['dependencies'], key=lambda row: (row['root'], row['path'])) == expected_dependencies
        capture = out / 'captured-original-products'
        capture.mkdir()
        for path in products.iterdir():
            if path.is_file() and path.suffix in ['.cpp', '.nc', '.json']:
                shutil.copyfile(path, capture / path.name)
        for optimization in ['-O0', '-O2']:
            executable = products / ('algorithm-standard-comparator-ordered-ranges' + optimization)
            raw = executable.read_bytes()
            assert raw[:6] == b'\x7fELF\x02\x01' and raw[18:20] == b'\x3e\x00'
            header = out / ('original-program' + optimization + '-header.bin')
            header.write_bytes(raw[:64])
            assert run([executable], 'native/additional' + optimization) == 0
            r['native_runs'].append({'optimization': optimization, 'sha256': sha(executable), 'bytes': len(raw),
                'header': header.name, 'header_sha256': sha(header), 'binary_retained_remotely_only': True})
        r.update(passed=True, completed=True, original_repaired_gtest_cases_passed=1,
                 original_optimization_loop_byte_preserved=True, additional_native_runs=2)
        save()
        print('PASS one real repaired original GTest XML and two retained O0/O2 products; full124 gate remains required.')
    except BaseException:
        r['failure'] = traceback.format_exc()
        save()
        raise


if __name__ == '__main__':
    main()

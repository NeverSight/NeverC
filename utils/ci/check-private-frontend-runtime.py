"""Run unchanged original GTests through the real current frontend and Translate owners.

Only the C23 backend is the authenticated existing compiler. This focused replay
does not certify a complete new compiler, platform matrix, or publication gate.
"""
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
import xml.etree.ElementTree as ET
from cpp_private_runtime_gtest import build_runtime_gtest
from extract_cpp_gtests import extract


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser()
    for name in ['repo', 'component-report', 'baseline', 'archive', 'private-prefix', 'output', 'build']:
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--component-revision', required=True)
    parser.add_argument('--component-report-sha256', required=True)
    args = parser.parse_args()
    root, out, build = args.repo.resolve(), args.output.resolve(), args.build.resolve()
    out.mkdir(parents=True)
    build.mkdir(parents=True)
    r = {'passed': False, 'completed': False, 'commands': [],
        'fixture_group': 'private-frontend-runtime', 'frontend_execution_timeout_seconds': 60,
        'real_current_private_entry_required': True, 'actual_current_translate_library_required': True,
        'library_stubs_used': False, 'host_compiler_artifact_rebuilt': False,
        'whole_original_7493_suite_executed': False,
        'full_compiler_or_platform_matrix_verified': False,
        'full_ci_passed': False, 'published_to_dev': False, 'goal_complete': False}

    def save():
        (out / 'results.json').write_text(json.dumps(r, indent=2) + '\n')

    def run(argv, label, timeout=60, env=None):
        argv = list(map(str, argv))
        log = out / (label + '.log')
        log.parent.mkdir(parents=True, exist_ok=True)
        started = time.monotonic()
        with log.open('w') as stream:
            proc = subprocess.Popen(argv, cwd=root, env=env, start_new_session=True,
                stdout=stream, stderr=subprocess.STDOUT)
            try:
                code = proc.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(proc.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                proc.wait()
                stream.write('\nDIAGNOSTIC PROCESS GROUP TIMED OUT\n')
                code = -1000
        r['commands'].append({'command': argv, 'cwd': str(root), 'log': str(log.relative_to(out)),
            'log_sha256': sha(log), 'returncode': code, 'timeout_seconds': timeout,
            'elapsed_seconds': time.monotonic() - started})
        save()
        print(label, code, flush=True)
        return code

    save()
    try:
        revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
        plan_path = root / 'utils/ci/cpp-private-runtime-plan.json'
        plan = json.loads(plan_path.read_text())
        r['execution_plan_sha256'] = sha(plan_path)
        r['source_sha256'] = {name: sha(root / name) for name in plan['source_paths']}
        shutil.copyfile(plan_path, out / 'execution-plan.json')
        component = json.loads(args.component_report.read_text())
        assert re.fullmatch('[0-9a-f]{40}', args.component_revision)
        assert re.fullmatch('[0-9a-f]{64}', args.component_report_sha256)
        assert sha(args.component_report) == args.component_report_sha256
        assert component['revision'] == args.component_revision and component['completed'] and component['passed'] is True and component['not_fn_private_support_complete'] is False
        assert component['all_five_bridge_units_recompiled'] is True
        assert component['focused_request_contract_passed'] is True and len(component['requests']) == 200
        assert sum(row['expected'] == 'accept' and row['expectation_satisfied'] for row in component['requests']) == 48
        assert sum(row['expected'] == 'reject' and row['expectation_satisfied'] for row in component['requests']) == 144
        assert sum(row['expected'] == 'invalid' and row['expectation_satisfied'] for row in component['requests']) == 8
        archive = args.archive.resolve()
        assert archive.is_file() and sha(archive) == component['replayed_archive_sha256']
        assert sha(args.private_prefix) == plan['private_prefix_sha256']
        prefix = 'neverc/lib/Translate/Cpp/Frontend/'
        assert len(component['exact_private_input_sha256']) == 19
        for name, identity in component['exact_private_input_sha256'].items():
            assert sha(root / prefix / name) == identity
        baseline = json.loads(args.baseline.read_text())
        assert baseline['revision'] == '1617f18ead74a8fa719e3d1bd02f9c84a10de551'
        assert baseline['run_id'] == 39244936536 and baseline['artifact_id'] == 12195306550
        assert baseline['artifact_sha256'] == '9332fad0d0226257f109ae74c1655a4e35510783d1029dd2bc8bdc15bfaf5206'
        backend = Path(baseline['compiler'])
        assert sha(backend) == baseline['compiler_sha256'] == 'fc803587bb5ad6997f9921a05da06c79816f510e8cb9a3185f21cd0a8eb1ad84'
        r.update(revision=revision, component_producer_revision=component['revision'],
            component_report_sha256=sha(args.component_report),
            exact_private_input_sha256=component['exact_private_input_sha256'],
            actual_private_archive_sha256=sha(archive), frontend_build_id=component['new_build_id'],
            private_prefix_sha256=sha(args.private_prefix),
            baseline_provenance=baseline, focused_component_scope_only=True)
        sdk = root / 'neverc/lib/Translate/Cpp/SDK'
        assert sha(sdk / 'catalog.json') == '1497f7d37860d32d9e53798fbcdc0fe473a4c4e66ac05fa81a45910adfb1d167'
        catalog = json.loads((sdk / 'catalog.json').read_text())
        rows = catalog['headers'] + catalog['metadata']
        assert len(rows) == 665
        for row in rows:
            assert sha(sdk / row['root'] / row['path']) == row['sha256']
        r['sdk_files_verified'] = 665
        manifest = json.loads((root / 'utils/ci/cpp-pair-piecewise-formal-tests.json').read_text())
        selected = manifest['new'] + manifest['regression']
        assert len(selected) == manifest['expected_total'] == 124
        original = (root / 'tests/neverc/TranslateTests.cpp').read_bytes()
        generated, source_proof = extract(original, selected)
        original_selected = tuple(selected)
        original_generated_sha256 = hashlib.sha256(generated).hexdigest()
        additions_path = root / plan['not_fn_formal_additions_path']
        additions = additions_path.read_bytes()
        assert sha(additions_path) == plan['not_fn_formal_additions_sha256']
        additional = plan['not_fn_formal_cases']
        assert len(additional) == 4 and len({c['formal_test_name'] for c in additional}) == 4
        assert not set(original_selected) & {c['formal_test_name'] for c in additional}
        assert generated.count(b'TEST_F(') == 124 and additions.count(b'TEST_F(') == 4
        generated += additions
        selected += [c['formal_test_name'] for c in additional]
        r['not_fn_formal_extension'] = dict(original_registered_source_names=7493,
            original_selected_names=list(original_selected),
            original_generated_before_extension_sha256=original_generated_sha256,
            additions_sha256=sha(additions_path), additional_cases=additional,
            original_extractor_and_124_bodies_unchanged=True)
        first_test = generated.index(b'TEST_F(')
        prefix_bytes = generated[:first_test]
        cleanup = b'''
  void TearDown() override {
    if (const char *Directory = std::getenv("NEVERC_PRIVATE_RUNTIME_RETAIN")) {
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
        at = prefix_bytes.rindex(b'\n};')
        prefix_bytes = prefix_bytes[:at] + cleanup + prefix_bytes[at:]
        generated = prefix_bytes + generated[first_test:]
        path = root / 'tests/neverc/PrivateCppRuntimeReplay.cpp'
        path.write_bytes(generated)
        (out / path.name).write_bytes(generated)
        source_proof['generated_with_post_assertion_retention_sha256'] = sha(path)
        (out / 'original-test-source-proof.json').write_text(json.dumps(source_proof, indent=2) + '\n')
        r['original_test_source_proof_sha256'] = sha(out / 'original-test-source-proof.json')
        # Reuse the exact system-library output authenticated for the original
        # actual component producer; no relocated llvm-config query or apt ABI.
        producer = build / 'component-system-libraries.log'
        producer.write_text(component['authenticated_system_libraries'])
        assert sha(producer) == component['authenticated_system_libraries_sha256']
        r['authenticated_system_libraries_sha256'] = sha(producer)
        driver, tests, owner = build_runtime_gtest(root, build, out, backend, archive,
            producer.read_text(), path, args.private_prefix, run)
        r['actual_cmake_owners'] = owner
        for label, binary in [('driver', driver), ('gtest', tests)]:
            header = binary.open('rb').read(64)
            assert header[:6] == b'\x7fELF\x02\x01' and header[18:20] == b'\x3e\x00'
            r[label + '_elf'] = {'path': str(binary), 'sha256': sha(binary),
                'bytes': binary.stat().st_size, 'first_64_bytes_hex': header.hex()}
        assert run([driver, '__neverc_cpp_frontend', '--version'], 'runtime/actual-entry-version') == 0
        assert (out / 'runtime/actual-entry-version.log').read_text().strip() == (
            'neverc-cpp-frontend 20.1.8 protocol 1 ' + component['new_build_id'])
        assert run([tests, '--gtest_list_tests'], 'runtime/registered-tests') == 0
        found, suite = [], None
        for line in (out / 'runtime/registered-tests.log').read_text().splitlines():
            if line and not line.startswith(' ') and line.endswith('.'):
                suite = line.strip()
            elif suite and line.startswith('  '):
                found.append(suite + line.strip().split()[0])
        assert set(found) == set(selected) and len(found) == 128
        xml = out / 'gtest.xml'
        environment = dict(os.environ, NEVERC_PRIVATE_RUNTIME_RETAIN=str(build / 'retained-programs'))
        code = run([tests, '--gtest_filter=' + ':'.join(selected), '--gtest_output=xml:' + str(xml)],
                   'runtime/original-gtests', 2400, environment)
        r['actual_gtest_returncode'] = code
        assert xml.is_file(), 'Real GoogleTest XML missing; no synthetic fallback is permitted'
        tree = ET.parse(xml).getroot()
        cases = tree.findall('.//testcase')
        names = [case.attrib['classname'] + '.' + case.attrib['name'] for case in cases]
        assert len(cases) == 128 and set(names) == set(selected)
        failed = [name for name, case in zip(names, cases) if case.find('failure') is not None]
        skipped = [name for name, case in zip(names, cases) if case.find('skipped') is not None]
        r.update(formal_xml_sha256=sha(xml), actual_formal_tests=len(cases),
                 actual_formal_failed=failed, actual_formal_skipped=skipped)
        invocations = out / 'invocations'
        assert invocations.is_dir()
        r['actual_invocation_inventory'] = [{'path': str(path.relative_to(out)), 'sha256': sha(path),
            'bytes': path.stat().st_size} for path in sorted(invocations.rglob('*')) if path.is_file()]
        requests = sorted((invocations / 'frontend').glob('*/request.json'))
        assert requests
        r['actual_frontend_request_count'] = len(requests)
        for request in requests:
            response = json.loads(request.with_name('response.json').read_text())
            assert response['frontend']['build'] == component['new_build_id']
            assert int(request.with_name('returncode.txt').read_text()) in [0, 1]
        retained = build / 'retained-programs'
        assert retained.is_dir()
        r['retained_original_products'] = [{'path': str(path.relative_to(retained)),
            'sha256': sha(path), 'bytes': path.stat().st_size,
            'first_64_bytes_hex': path.open('rb').read(64).hex()}
            for path in sorted(retained.rglob('*')) if path.is_file()]
        assert sha(archive) == r['actual_private_archive_sha256'] and sha(backend) == baseline['compiler_sha256']
        not_fn_products = []
        for case in additional:
            for optimization in ['-O0', '-O2']:
                matches = list(retained.rglob(case['name'] + optimization))
                assert len(matches) <= 1
                if matches:
                    product = matches[0]
                    with product.open('rb') as stream:
                        header = stream.read(64)
                    not_fn_products.append(dict(name=case['name'], optimization=optimization,
                        path=str(product.relative_to(retained)), bytes=product.stat().st_size,
                        sha256=sha(product), first_64_bytes_hex=header.hex()))
        r['actual_not_fn_generated_products'] = not_fn_products
        if not failed and not skipped and code == 0:
            assert len(not_fn_products) == 8
            for product in not_fn_products:
                header = bytes.fromhex(product['first_64_bytes_hex'])
                assert product['bytes'] > 64 and len(header) == 64
                assert header[:6] == b'\x7fELF\x02\x01' and header[18:20] == b'\x3e\x00'
        r.update(completed=True, passed=code == 0 and not failed and not skipped,
            actual_component_formal_tests_passed=code == 0 and not failed and not skipped)
        save()
        return 0 if r['passed'] else 1
    except BaseException:
        r['failure'] = traceback.format_exc()
        save()
        raise


if __name__ == '__main__':
    raise SystemExit(main())

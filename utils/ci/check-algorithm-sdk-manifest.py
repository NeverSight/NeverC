"""Capture the original algorithm's real pinned SDK closure and native results."""
from pathlib import Path
import argparse
import hashlib
import json
import platform
import re
import shutil
import subprocess
import time
import traceback


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=True)
    args = parser.parse_args()
    root, out, build = args.repo.resolve(), args.output.resolve(), args.build.resolve()
    out.mkdir(parents=True)
    build.mkdir(parents=True)
    report = {'passed': False, 'completed': False, 'commands': [],
              'compiler_rebuilt': False, 'formal_test_executed': False,
              'original_test_modified': False, 'frontend_timeout_seconds': 60,
              'full_ci_passed': False, 'published_to_dev': False, 'goal_complete': False}

    def save():
        (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')

    def run(command, label):
        command = list(map(str, command))
        log = out / (label + '.log')
        started = time.monotonic()
        with log.open('w') as stream:
            try:
                code = subprocess.run(command, cwd=root, stdout=stream,
                    stderr=subprocess.STDOUT, timeout=60).returncode
            except subprocess.TimeoutExpired:
                stream.write('\nDIAGNOSTIC COMMAND TIMED OUT\n')
                code = -1000
        report['commands'].append({'command': command, 'cwd': str(root),
            'returncode': code, 'timeout_seconds': 60, 'log': log.name,
            'log_sha256': sha(log), 'elapsed_seconds': time.monotonic() - started})
        save()
        print(label, code, flush=True)
        assert code == 0, (label, code)

    try:
        assert platform.system() == 'Linux' and platform.machine() == 'x86_64'
        report['revision'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
        source_name = 'tests/neverc/TranslateTests.cpp'
        report['source_sha256'] = {name: sha(root / name) for name in [source_name,
            'utils/ci/check-algorithm-sdk-manifest.py', 'utils/ci/download-cpp-linux-baseline.py',
            '.github/workflows/diagnose-cpp-template-source.yml']}
        test = 'CoreV2AlgorithmStandardComparatorOrderedRangesRunAtBothOptimizations'
        original = (root / source_name).read_text()
        start = re.search(r'TEST_F\(\s*TranslateTest,\s*' + test + r'\)\s*\{', original)
        assert start
        end = original.index('\nTEST_F(', start.end())
        body = original[start.start():end]
        assert 'EXPECT_EQ(Dependencies->size(), 435u);' in body
        match = re.search(r'writeFile\(Source, R"cpp\((.*?)\)cpp"\);', body, re.S)
        assert match
        source = out / 'algorithm-standard-comparator-ordered-ranges.cpp'
        source.write_text(match.group(1))
        report['original_test_name'] = test
        report['original_test_body_sha256'] = hashlib.sha256(body.encode()).hexdigest()
        report['original_program_sha256'] = sha(source)
        report['original_expected_dependency_count'] = 435
        sdk = root / 'neverc/lib/Translate/Cpp/SDK'
        assert sha(sdk / 'catalog.json') == '1497f7d37860d32d9e53798fbcdc0fe473a4c4e66ac05fa81a45910adfb1d167'
        catalog = json.loads((sdk / 'catalog.json').read_text())
        records = catalog['headers'] + catalog['metadata']
        assert len(records) == 665
        for entry in records:
            assert sha(sdk / entry['root'] / entry['path']) == entry['sha256']
        report['sdk_files_verified'] = 665
        report['sdk_catalog_sha256'] = sha(sdk / 'catalog.json')
        provenance = json.loads(args.baseline.read_text())
        assert provenance['revision'] == '1617f18ead74a8fa719e3d1bd02f9c84a10de551'
        assert provenance['run_id'] == 39244936536 and provenance['artifact_id'] == 12195306550
        assert provenance['artifact_sha256'] == '9332fad0d0226257f109ae74c1655a4e35510783d1029dd2bc8bdc15bfaf5206'
        compiler = Path(provenance['compiler'])
        assert sha(compiler) == provenance['compiler_sha256']
        report['baseline_provenance'] = provenance
        shutil.copyfile(args.baseline, out / 'baseline-provenance.json')
        run([compiler, '--version'], 'compiler-version')
        generated = out / 'algorithm-standard-comparator-ordered-ranges.nc'
        run([compiler, 'translate', '--from', 'cpp', source, '--profile', 'cpp-core-v2',
             '-o', generated, '--', '-std=c++17'], 'original-translation')
        manifest = generated.with_name(generated.name + '.manifest.json')
        sdk_metadata = json.loads(manifest.read_text())['sdk']
        assert sdk_metadata['catalog_sha256'] == report['sdk_catalog_sha256']
        dependencies = sdk_metadata['dependencies']
        assert len(dependencies) == 447
        index = {(row['root'], row['path']): row['sha256'] for row in records}
        seen = set()
        for row in dependencies:
            key = (row['root'], row['path'])
            assert key not in seen and row['root'] != 'platform'
            assert index[key] == row['sha256']
            seen.add(key)
        expected = sorted(dependencies, key=lambda row: (row['root'], row['path']))
        golden = out / 'algorithm-standard-sdk-dependencies.json'
        golden.write_text(json.dumps(expected, indent=2) + '\n')
        report['actual_dependency_count'] = len(dependencies)
        report['captured_dependency_manifest_sha256'] = sha(golden)
        report['generated_nc_sha256'] = sha(generated)
        report['original_manifest_sha256'] = sha(manifest)
        report['programs'] = []
        for optimization in ['-O0', '-O2']:
            binary = build / ('algorithm' + optimization)
            # Preserve the original common fixture flags; no new bypass.
            run([compiler, generated, optimization, '-fno-lto', '-fno-builtin-mimalloc',
                 '-o', binary], 'compile' + optimization)
            data = binary.read_bytes()
            assert data[:6] == b'\x7fELF\x02\x01' and data[18:20] == b'\x3e\x00'
            header = out / ('algorithm' + optimization + '-elf-header.bin')
            header.write_bytes(data[:64])
            report['programs'].append({'optimization': optimization, 'sha256': sha(binary),
                'bytes': len(data), 'header': header.name, 'header_sha256': sha(header),
                'binary_retained_remotely_only': True})
            run([binary], 'execute' + optimization)
        report.update(passed=True, completed=True)
        save()
        print('Captured447 exact catalog-bound dependencies and two original O0/O2 executions; GTest repair still required.')
    except BaseException:
        report['failure'] = traceback.format_exc()
        save()
        raise


if __name__ == '__main__':
    main()

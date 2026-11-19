"""Compare generated C23 with C++17 on the native x86-64 Actions runner."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import xml.etree.ElementTree as ET

PLAN = 'utils/ci/cpp-variant-scalar-native-plan.json'
MODES = ('-O0', '-O2')
FORMAL = (
    'CoreV2UnionCarrierChecksLayoutAndEmitsUnionMembers',
    'CoreV2LayoutWireRequiresExactFields',
    'CoreV2RecordLayoutChecksOffsetsPaddingAndNestedStorage',
    'CoreV2EmptyRecordLayoutRequiresExactIndependentEvidence',
    'CoreV2RecordLayoutWireRejectsMissingAndMalformedEvidence',
    'CoreV2ConstantArrayTreesRetainLayoutAndConstSubobjects',
)


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def elf64(path):
    with path.open('rb') as stream:
        header = stream.read(20)
    assert header[:6] == b'\x7fELF\x02\x01' and header[18:20] == b'\x3e\x00'
    return dict(sha256=digest(path), bytes=path.stat().st_size, header=header.hex())


def execute(argv, cwd, logs, name, timeout):
    cmd = list(map(str, argv))
    path = logs / (name + '.log')
    try:
        with path.open('xb') as output:
            child = subprocess.run(cmd, cwd=cwd, stdin=subprocess.DEVNULL, stdout=output,
                stderr=subprocess.STDOUT, timeout=timeout, check=False)
        status = child.returncode
    except subprocess.TimeoutExpired:
        status = None
    assert path.stat().st_size <= 256 * 1024, name
    return dict(argv=cmd, returncode=status, timeout=timeout, log='logs/' + path.name,
                log_sha256=digest(path)), path.read_text(encoding='utf-8', errors='replace')


def formal_run(executable, cwd, output, logs):
    xml = output / 'formal.xml'
    names = ['TranslateIR.' + case for case in FORMAL]
    command, _ = execute([executable, '--gtest_filter=' + ':'.join(names),
                         '--gtest_output=xml:' + str(xml)], cwd, logs, 'formal', 120)
    if command['returncode'] != 0 or not xml.exists():
        return dict(command=command, passed=False)
    tree = ET.parse(xml)
    cases = tree.findall('.//testcase')
    passed = len(cases) == len(names) and {tc.attrib['name'] for tc in cases} == set(FORMAL)
    passed &= all(tc.attrib['status'] == 'run' and tc.attrib['result'] == 'completed' and not list(tc)
                  for tc in cases)
    return dict(command=command, passed=passed, cases=list(FORMAL), xml_sha256=digest(xml))


def run_case(row, compiler, clangxx, work, logs, target, sdk):
    name = row['name']
    directory = work / name
    directory.mkdir()
    source = directory / 'fixture.cpp'
    generated = directory / 'fixture.nc'
    source.write_bytes(row['source'].encode())
    assert digest(source) == row['source_sha256']
    commands = []
    translate = [compiler, 'translate', '--from', 'cpp', source, '--profile', 'cpp-core-v2',
                 '--target', target, '-o', generated, '--', '-std=c++17']
    command, diagnostic = execute(translate, directory, logs, name + '-translate', 180)
    commands.append(command)
    case = dict(name=name, expected=row['expected'], source_sha256=digest(source),
                commands=commands, products=[], passed=False)
    if row['expected'] == 'reject':
        case['passed'] = command['returncode'] == 1 and ('TR0201' in diagnostic or 'TR0203' in diagnostic)
        case['passed'] &= 'TR0101' not in diagnostic and 'TR0102' not in diagnostic
        case['passed'] &= not list(directory.glob('fixture.nc*'))
        return case
    if command['returncode'] != 0 or not generated.is_file():
        return case
    source_hash = digest(generated)
    manifest = json.loads(generated.with_suffix('.nc.manifest.json').read_bytes())
    location_map = json.loads(generated.with_suffix('.nc.map.json').read_bytes())
    assert manifest['sdk']['catalog_sha256'] == sdk and manifest['target']['triple'] == target
    assert location_map['generated_sha256'] == source_hash
    text = generated.read_text(encoding='utf-8')
    assert 'typedef union ' in text and 'translated field offset mismatch' in text
    assert 'translated record alignment mismatch' in text
    case['generated_c23_sha256'] = source_hash
    for mode in MODES:
        for label in ('native', 'generated'):
            executable = directory / (label + mode)
            build = ([clangxx, '-std=c++17', mode, source, '-o', executable] if label == 'native'
                     else [compiler, generated, mode, '-fno-lto', '-fno-builtin-mimalloc',
                           '-fno-fast-math', '-ffp-contract=off', '-o', executable])
            built, _ = execute(build, directory, logs, name + '-' + label + mode + '-build', 180)
            commands.append(built)
            if built['returncode'] != 0:
                continue
            image = elf64(executable)
            executed, _ = execute([executable], directory, logs, name + '-' + label + mode + '-run', 30)
            commands.append(executed)
            case['products'].append(dict(kind=label, mode=mode, image=image, exit=executed['returncode']))
    case['passed'] = len(case['products']) == 4 and all(item['exit'] == 0 for item in case['products'])
    return case


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', required=True, type=Path)
    parser.add_argument('--compiler', required=True, type=Path)
    parser.add_argument('--formal', required=True, type=Path)
    parser.add_argument('--clangxx', required=True)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    assert sys.platform == 'linux' and platform.machine() == 'x86_64'
    repo, compiler, formal, output = (p.resolve() for p in (args.repo, args.compiler, args.formal, args.output))
    output.mkdir()
    work, logs = output/'work', output/'logs'
    work.mkdir()
    logs.mkdir()
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, timeout=30, text=True).strip()
    assert revision == os.environ['GITHUB_SHA'] and os.environ['GITHUB_RUN_ATTEMPT'] == '1'
    plan_file = repo / PLAN
    plan = json.loads(plan_file.read_bytes())
    assert len(plan['private']) == 19 and len(plan['sdk']) == 695 and len(plan['driver']) == 5
    for identities in (plan['private'], plan['sdk'], plan['driver']):
        for path, sha in identities.items():
            assert digest(repo / path) == sha, path
    assert len(plan['cases']) == 24 and sum(row['expected'] == 'accept' for row in plan['cases']) == 16
    target = plan['target']
    assert target == 'x86_64-unknown-linux-gnu'
    report = dict(revision=revision, product=plan['product'], run=int(os.environ['GITHUB_RUN_ID']),
                  attempt=1, target=target, plan_sha256=digest(plan_file), compiler=elf64(compiler),
                  cases=[], completed=False, passed=False, commands=[])
    try:
        for argv, name in (([compiler, '-dumpmachine'], 'neverc-target'),
                           ([compiler, '--version'], 'neverc-version'),
                           ([args.clangxx, '--version'], 'native-version')):
            command, content = execute(argv, work, logs, name, 30)
            report['commands'].append(command)
            assert command['returncode'] == 0 and content.strip(), name
            if name == 'neverc-target':
                assert content.startswith('x86_64-') and 'linux' in content
        report['formal'] = formal_run(formal, work, output, logs)
        for row in plan['cases']:
            result = run_case(row, compiler, args.clangxx, work, logs, target, plan['sdk_catalog'])
            report['cases'].append(result)
            print(result['name'], 'passed' if result['passed'] else 'failed', flush=True)
        report['completed'] = True
        report['passed'] = report['formal']['passed'] and all(row['passed'] for row in report['cases'])
    finally:
        (output / 'results.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())

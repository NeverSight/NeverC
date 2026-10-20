"""Retain real failed fixtures and replay their recorded commands under GDB."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import time
import traceback
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    base = args.evidence.resolve()
    output = base / 'residual-diagnosis'
    output.mkdir()
    result = {'completed': False, 'commands': [], 'targets': [],
              'compiler_fixed': False, 'full_ci_passed': False, 'goal_complete': False,
              'diagnostic_only': True}

    def sha(path):
        return hashlib.sha256(Path(path).read_bytes()).hexdigest()

    def save():
        (output / 'diagnosis.json').write_text(json.dumps(result, indent=2) + '\n')

    def command(argv, name, cwd, environment):
        log = output / (name + '.log')
        before = time.monotonic()
        with log.open('w') as stream:
            try:
                process = subprocess.run(list(map(str, argv)), cwd=cwd, env=environment,
                                         stdout=stream, stderr=subprocess.STDOUT, timeout=60)
                code = process.returncode
            except subprocess.TimeoutExpired:
                stream.write('\nDIAGNOSTIC COMMAND TIMED OUT\n')
                code = 124
        row = {'command': list(map(str, argv)), 'returncode': code, 'cwd': str(cwd),
               'timeout_seconds': 60, 'elapsed_seconds': time.monotonic() - before,
               'log': str(log.relative_to(base)), 'log_sha256': sha(log)}
        result['commands'].append(row)
        save()
        return row

    save()
    try:
        replay_path = base / 'results.json'
        replay = json.loads(replay_path.read_text())
        assert replay['completed'] and replay['revision']
        result.update(revision=replay['revision'], replay_report_sha256=sha(replay_path),
                      compiler_provenance=replay['baseline_provenance'])
        compiler = Path(replay['baseline_provenance']['compiler'])
        assert sha(compiler) == replay['baseline_provenance']['compiler_sha256']
        xml = base / 'gtest/linker-sdk.xml'
        cases = ET.parse(xml).getroot().findall('.//testcase')
        assert len(cases) == 63 and not any(case.find('skipped') is not None for case in cases)
        failures = [case.attrib['classname'] + '.' + case.attrib['name']
                    for case in cases if case.find('failure') is not None]
        result['actual_failed_gtests'] = failures
        result['actual_gtest_process_returncode'] = replay['gtest_process_returncode']
        result['actual_gtest_all_passed'] = replay['passed']
        result['original_gtest_xml_sha256'] = sha(xml)
        result['diagnostic_sources'] = {}
        for name in ['utils/ci/diagnose-linker-residual-failures.py',
                     'tests/neverc/LinkerTests.cpp', 'utils/ci/cpp-linker-sdk-replay-plan.json']:
            root = Path(__file__).resolve().parents[2]
            result['diagnostic_sources'][name] = sha(root / name)
        # Record the untouched snapshots before exact-path replays can write
        # outputs again. The compiler itself stays outside the evidence tree.
        inventory = []
        failed_root = base / 'captured-fixtures'
        for path in sorted(failed_root.rglob('*')):
            if path.is_file():
                assert not path.is_symlink()
                inventory.append({'path': str(path.relative_to(base)),
                                  'size': path.stat().st_size, 'sha256': sha(path)})
        assert inventory
        result['preserved_fixture_files'] = inventory
        version = command(['gdb', '--version'], 'gdb-version', base, os.environ.copy())
        assert version['returncode'] == 0
        specifications = [
            ('preload', 'NativeMachOImageKinds',
             lambda row: '-Wl,-preload' in row['arguments']),
            ('kext', 'NativeMachOProbesAndInternalOptions',
             lambda row: any(arg.startswith('-Wl,-kext_objects_dir,') for arg in row['arguments'])),
            ('relr', 'FastPipelinePacksRelativeRelocations',
             lambda row: Path(row['program']).name == 'fast_relr' and row['arguments'] == []),
        ]
        for label, test, select in specifications:
            records = []
            for path in sorted((base / 'recorded-commands' / test).glob('*.json')):
                record = json.loads(path.read_text())
                if select(record):
                    records.append((path, record))
            assert len(records) == 1, (label, len(records))
            record_path, record = records[0]
            source = failed_root / test
            target = Path(record['tmp'])
            assert target.parent == Path('/tmp') and target.name.startswith('neverc_test_' + test + '_')
            assert source.is_dir() and not target.exists()
            shutil.copytree(source, target)
            environment = os.environ.copy()
            environment.pop('NEVERC_ELF_FASTLINK_TIME', None)
            environment.update(record['environment'])
            argv = [record['program'], *record['arguments']]
            if label != 'relr':
                assert Path(argv[0]) == compiler
                assert '-fno-builtin-mimalloc' in argv
            else:
                assert Path(argv[0]).parent == target
                data = Path(argv[0]).read_bytes()
                assert data[:6] == b'\x7fELF\x02\x01'
            item = {'name': label, 'test': test,
                    'record': str(record_path.relative_to(base)), 'record_sha256': sha(record_path),
                    'original_returncode': record['returncode'], 'original_command': argv,
                    'recorded_environment': record['environment'], 'normal_replays': []}
            result['targets'].append(item)
            for repeat in range(3):
                row = command(argv, label + '-original-' + str(repeat), Path(record['cwd']), environment)
                item['normal_replays'].append(row['returncode'])
            script = output / (label + '.gdb')
            if label == 'relr':
                script.write_text('''set pagination off
set confirm off
break main
run
printf "RELR_VALUES_BASE "
p/x (void *)&values
printf "RELR_TABLE_BASE "
p/x (void *)&table
x/24gx (void *)&table
set $index = 0
set $mismatches = 0
while $index < 200
  set $actual = ((void **)(void *)&table)[$index]
  set $expected = (void *)((char *)(void *)&values + 4 * $index)
  if $actual != $expected
    printf "RELR_MISMATCH %d actual=%p expected=%p\\n", $index, $actual, $expected
    set $mismatches = $mismatches + 1
  end
  set $index = $index + 1
end
printf "RELR_MISMATCH_COUNT %d\\n", $mismatches
continue
''')
            else:
                script.write_text('''set pagination off
set confirm off
run
info registers
thread apply all bt 32
''')
            debugger = ['gdb', '-q', '-batch', '-x', script, '--args', *argv]
            row = command(debugger, label + '-gdb', Path(record['cwd']), environment)
            item['gdb_returncode'] = row['returncode']
            item['gdb_script'] = str(script.relative_to(base))
            item['gdb_script_sha256'] = sha(script)
            # Keep live decoded ELF facts for the actual failing program, not
            # an object inferred from the command text or a replacement build.
            if label == 'relr':
                row = command(['readelf', '-W', '-h', '-S', '-d', '-r', '-s', argv[0]],
                              label + '-elf', Path(record['cwd']), environment)
                assert row['returncode'] == 0
            save()
        result['completed'] = True
        result['all_three_original_commands_replayed'] = len(result['targets']) == 3
    except Exception:
        result['error'] = traceback.format_exc()
        save()
        raise
    finally:
        save()
    print('Retained actual failing linker inputs and recorded-command GDB evidence; no repair or full-CI pass inferred.')


if __name__ == '__main__':
    main()

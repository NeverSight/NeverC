"""Actions-only source metadata and two independent native capture probes.

This does not execute the private frontend, repeat the initial seven native
references, or establish private bind support. Compiler output is retained as
evidence to design the independent admission and lowering proofs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import signal
import subprocess
import traceback


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root, out = args.repo.resolve(), args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    report = dict(passed=False, completed=False, source_observation_only=True,
        private_frontend_requests=0, generated_programs=0, formal_xml=0,
        private_support_verified=False, published_to_dev=False, goal_complete=False,
        commands=[], native=[], metadata=[])

    def save():
        (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')

    def run(command, label, allow_failure=False):
        command = list(map(str, command))
        log = out / (label + '.log')
        log.parent.mkdir(parents=True, exist_ok=True)
        with log.open('x') as stream:
            process = subprocess.Popen(command, cwd=root, stdout=stream,
                stderr=subprocess.STDOUT, start_new_session=True)
            try:
                code = process.wait(timeout=60)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
                stream.write('\nSOURCE COMMAND TIMED OUT\n')
                code = -1000
        report['commands'].append(dict(command=command, returncode=code,
            timeout_seconds=60, log=str(log.relative_to(out)), log_sha256=sha(log),
            log_bytes=log.stat().st_size))
        save()
        assert allow_failure or code == 0, (label, code)
        return code, log

    save()
    try:
        assert os.environ.get('GITHUB_ACTIONS') == 'true'
        assert platform.system() == 'Linux' and platform.machine() == 'x86_64'
        plan_path = root / 'utils/ci/cpp-bind-source-structure-plan.json'
        plan = json.loads(plan_path.read_bytes())
        assert plan['source_observation_only'] and not plan['private_support_verified']
        assert len(plan['native_cases']) == 2 and len(plan['metadata_cases']) == 4
        assert len(plan['target_abis']) == 8
        report['plan'] = plan
        report['revision'] = subprocess.check_output(
            ['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
        report['source_sha256'] = {p: sha(root / p) for p in plan['candidate_source_paths']}
        report['exact_private_input_sha256'] = {
            n: sha(root / 'neverc/lib/Translate/Cpp/Frontend' / n)
            for n in plan['exact_private_input_sha256']}
        assert report['exact_private_input_sha256'] == plan['exact_private_input_sha256']
        sdk = root / 'neverc/lib/Translate/Cpp/SDK'
        report['sdk_catalog_sha256'] = sha(sdk / 'catalog.json')
        assert report['sdk_catalog_sha256'] == plan['sdk_catalog_sha256']
        catalog = json.loads((sdk / 'catalog.json').read_bytes())
        headers = catalog['headers'] + catalog['metadata']
        assert len(headers) == 665
        for row in headers:
            assert sha(sdk / row['root'] / row['path']) == row['sha256']
        report['sdk_files_verified'] = 665
        common = ['--no-default-config', '-std=c++17', '-stdlib=libc++', '-nostdinc++',
            '-resource-dir', str(sdk / 'resource'), '-isystem', str(sdk / 'libcxx'),
            '-fno-fast-math', '-ffp-contract=off', '-D_LIBCPP_REMOVE_TRANSITIVE_INCLUDES',
            '-D_NEVERC_CPP_CORE_V2_SDK', '-D_LIBCPP___ITERATOR_ISTREAM_ITERATOR_H',
            '-D_LIBCPP___ITERATOR_ISTREAMBUF_ITERATOR_H',
            '-D_LIBCPP___ITERATOR_OSTREAM_ITERATOR_H',
            '-D_LIBCPP___ITERATOR_OSTREAMBUF_ITERATOR_H',
            '-D_LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS']
        report['common_flags'] = common
        for version in [20, 22]:
            compiler = Path('/usr/bin/clang++-' + str(version))
            _, log = run([compiler, '--version'], 'clang-' + str(version))
            assert ('clang version ' + str(version) + '.') in log.read_text()
            report.setdefault('compilers', {})[str(version)] = dict(
                path=str(compiler), sha256=sha(compiler))
        assert 'clang version 20.1.8' in (out / 'clang-20.log').read_text()
        for case in plan['native_cases']:
            folder = out / case['name']
            folder.mkdir()
            source = folder / 'input.cpp'
            source.write_text(case['source'])
            assert sha(source) == case['source_sha256']
            modes = []
            for mode in ['-O0', '-O2']:
                binary = folder / ('native' + mode)
                built, _ = run(['/usr/bin/clang++-20'] + common + [mode, source,
                    '-o', binary], case['name'] + '/' + mode + '-build', True)
                executed = None
                record = dict(mode=mode, build_returncode=built, run_returncode=None,
                    passed=False)
                if built == 0:
                    data = binary.read_bytes()
                    assert data[:6] == b'\x7fELF\x02\x01' and data[18:20] == b'\x3e\x00'
                    executed, _ = run([binary], case['name'] + '/' + mode + '-run', True)
                    record.update(binary_sha256=sha(binary), binary_bytes=len(data),
                        elf_header_hex=data[:64].hex())
                record.update(run_returncode=executed, passed=built == executed == 0)
                modes.append(record)
            report['native'].append(dict(name=case['name'],
                source_sha256=sha(source), modes=modes))
            save()
        metadata_common = common + ['-ffreestanding', '-nostdinc',
            '-isystem', str(sdk / 'resource/include'), '-D_LIBCPP_NO_VCRUNTIME',
            '-fno-ms-compatibility', '-fno-ms-extensions', '-fno-delayed-template-parsing']
        report['metadata_flags'] = metadata_common
        for case in plan['metadata_cases']:
            folder = out / case['name']
            folder.mkdir()
            source = folder / 'input.cpp'
            source.write_text(case['source'])
            assert sha(source) == case['source_sha256']
            targets = []
            for target in plan['target_abis']:
                command = ['/usr/bin/clang++-22'] + metadata_common + ['--target=' + target,
                    '-fsyntax-only', '-Xclang', '-ast-dump', '-Xclang',
                    '-ast-dump-filter=__bind', '-Xclang', '-fdump-record-layouts', source]
                code, log = run(command, case['name'] + '/' + target, True)
                text = log.read_text(errors='replace')
                targets.append(dict(target=target, returncode=code,
                    log=str(log.relative_to(out)), log_sha256=sha(log),
                    bind_field_names_observed='__bound_args_' in text and '__f_' in text,
                    bind_declaration_observed='__bind' in text,
                    layout_section_observed='*** Dumping AST Record Layout' in text))
            report['metadata'].append(dict(name=case['name'],
                source_sha256=sha(source), targets=targets))
            save()
        report['completed'] = True
        report['passed'] = (len(report['native']) == 2 and
            all(m['passed'] for c in report['native'] for m in c['modes']) and
            len(report['metadata']) == 4 and
            all(t['returncode'] == 0 and t['bind_field_names_observed'] and
                t['bind_declaration_observed'] and t['layout_section_observed']
                for c in report['metadata'] for t in c['targets']))
        save()
    except Exception:
        report['error'] = traceback.format_exc()
        save()
        print(report['error'], flush=True)
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())

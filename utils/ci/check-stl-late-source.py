"""Observe one original six-target failure using the exact just-recompiled diagnostic driver."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import traceback


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--source-report', type=Path, required=True)
    parser.add_argument('--driver', type=Path, required=True)
    args = parser.parse_args()
    assert os.environ.get('GITHUB_ACTIONS') == 'true' and os.name == 'posix'
    root, out = args.repo.resolve(), args.out.resolve()
    out.mkdir(exist_ok=False)
    plan = json.loads((root / 'utils/ci/stl-late-source-plan.json').read_text())
    report = dict(revision=os.environ['GITHUB_SHA'], plan=plan, diagnostic_only=True,
        completed=False, commands=[], observations=[], actual_c23_executions=0, full_stl_complete=False)
    try:
        for relative, identity in plan['exact_private_input_sha256'].items():
            assert sha(root / 'neverc/lib/Translate/Cpp/Frontend' / relative) == identity, relative
        for relative, identity in plan['sdk_input_sha256'].items():
            assert sha(root / 'neverc/lib/Translate/Cpp/SDK' / relative) == identity, relative
        previous = json.loads(args.source_report.read_text())
        assert previous['revision'] == os.environ['GITHUB_SHA'] and previous['completed']
        assert previous['all_five_bridge_units_recompiled']
        assert previous['exact_private_input_sha256'] == plan['exact_private_input_sha256']
        assert previous['new_executable_sha256'] == sha(args.driver)
        assert len(previous['requests']) == 72 and len(previous['commands']) == 89
        report.update(source72_report_sha256=sha(args.source_report), driver_sha256=sha(args.driver))
        assert len(plan['target_abis']) == 6 and all(
            target.startswith(('x86_64-', 'arm64-', 'aarch64-')) for target in plan['target_abis'])
        source_bytes = plan['source'].encode()
        assert hashlib.sha256(source_bytes).hexdigest() == plan['source_sha256']
        environment = dict(os.environ, NEVERC_STL_SOURCE_TRACE='1')
        environment.pop('NEVERC_CPP_BIND_PROOF_TRACE', None)
        for index, target in enumerate(plan['target_abis']):
            directory = out / ('target-' + str(index))
            directory.mkdir()
            source = directory / 'input.cpp'
            source.write_bytes(source_bytes)
            request, response = directory / 'request.json', directory / 'response.json'
            request.write_text(json.dumps(dict(protocol=1, profile='cpp-core-v2', target=target,
                root=str(directory), source=str(source), arguments=['-std=c++17'], sdk=plan['sdk_identity'])))
            command = [str(args.driver), '__neverc_cpp_frontend', '--request', str(request), '--output', str(response)]
            actual = subprocess.run(command, capture_output=True, env=environment, timeout=60)
            data = actual.stdout + actual.stderr
            assert len(data) <= 2 * 1024**2 and response.is_file()
            log = directory / 'execution.log'
            log.write_bytes(data)
            value = json.loads(response.read_text())
            assert value['target']['triple'] == target
            report['commands'].append(dict(command=command, returncode=actual.returncode,
                log=log.relative_to(out).as_posix(), log_sha256=sha(log), timeout_seconds=60))
            report['observations'].append(dict(target=target, source_sha256=sha(source),
                response=response.relative_to(out).as_posix(), response_sha256=sha(response),
                diagnostics=value['diagnostics'], trace_events=data.count(b'NEVERC_STL_LATE ')))
        assert len(report['observations']) == 6
        report['completed'] = True
        return 0
    except Exception:
        report['failure'] = traceback.format_exc()
        print(report['failure'], flush=True)
        return 1
    finally:
        (out / 'late-source-results.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    raise SystemExit(main())

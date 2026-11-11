"""Observe six original requests, including the driver's required PID receipts."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import traceback

LOG_LIMIT = 2 * 1024**2
PID_FILES = {'request.json', 'argv.txt', 'environment-before.tsv', 'response.json', 'returncode.txt'}


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
        completed=False, commands=[], observations=[], invocation_inventory=[],
        actual_c23_executions=0, full_stl_complete=False)
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
        assert len(previous['invocation_inventory']) == 360
        report.update(source72_report_sha256=sha(args.source_report), driver_sha256=sha(args.driver))
        assert len(plan['target_abis']) == 6 and all(
            target.startswith(('x86_64-', 'arm64-', 'aarch64-')) for target in plan['target_abis'])
        source_bytes = plan['source'].encode()
        assert hashlib.sha256(source_bytes).hexdigest() == plan['source_sha256']
        invocations = out / 'invocations'
        invocations.mkdir()
        environment = dict(os.environ, NEVERC_STL_SOURCE_TRACE='1',
            NEVERC_CPP_PROTOCOL_EXECUTION_DIR=str(invocations))
        environment.pop('NEVERC_CPP_BIND_PROOF_TRACE', None)
        previous_pids = set()
        for index, target in enumerate(plan['target_abis']):
            directory = out / ('target-' + str(index))
            directory.mkdir()
            source = directory / 'input.cpp'
            source.write_bytes(source_bytes)
            request, response = directory / 'request.json', directory / 'response.json'
            request.write_text(json.dumps(dict(protocol=1, profile='cpp-core-v2', target=target,
                root=str(directory), source=str(source), arguments=['-std=c++17'], sdk=plan['sdk_identity'])))
            command = [str(args.driver), '__neverc_cpp_frontend', '--request', str(request), '--output', str(response)]
            returncode, timed_out = None, False
            try:
                actual = subprocess.run(command, capture_output=True, env=environment, timeout=60)
                data, returncode = actual.stdout + actual.stderr, actual.returncode
            except subprocess.TimeoutExpired as failure:
                data = (failure.stdout or b'') + (failure.stderr or b'')
                timed_out = True
            # Retain the actual status and bounded output before checking the
            # response, so a driver setup failure cannot erase its evidence.
            log = directory / 'execution.log'
            log.write_bytes(data[:LOG_LIMIT])
            report['commands'].append(dict(command=command, returncode=returncode,
                timed_out=timed_out, log=log.relative_to(out).as_posix(), log_sha256=sha(log),
                actual_log_bytes=len(data), complete_log=len(data) <= LOG_LIMIT, timeout_seconds=60))
            assert not timed_out and len(data) <= LOG_LIMIT and response.is_file()
            current_pids = {p.name for p in invocations.iterdir()}
            pid, = current_pids - previous_pids
            assert re.fullmatch(r'[1-9][0-9]*', pid)
            receipt = invocations / pid
            assert receipt.is_dir() and not receipt.is_symlink()
            assert {p.name for p in receipt.iterdir()} == PID_FILES
            assert sha(receipt / 'request.json') == sha(request)
            assert sha(receipt / 'response.json') == sha(response)
            assert (receipt / 'argv.txt').read_text().splitlines() == command
            assert (receipt / 'returncode.txt').read_text() == str(returncode) + '\n'
            names = [line.split('\t', 1)[0] for line in (receipt / 'environment-before.tsv').read_text().splitlines()]
            assert names == ['CPATH', 'CPLUS_INCLUDE_PATH', 'C_INCLUDE_PATH', 'SDKROOT', 'CCC_OVERRIDE_OPTIONS']
            for name in sorted(PID_FILES):
                path = receipt / name
                report['invocation_inventory'].append(dict(path=path.relative_to(out).as_posix(), sha256=sha(path)))
            previous_pids = current_pids
            value = json.loads(response.read_text())
            assert value['target']['triple'] == target
            report['observations'].append(dict(target=target, source_sha256=sha(source),
                response=response.relative_to(out).as_posix(), response_sha256=sha(response),
                pid=int(pid), diagnostics=value['diagnostics'], trace_events=data.count(b'NEVERC_STL_LATE ')))
        assert len(report['observations']) == len(previous_pids) == 6
        assert len(report['invocation_inventory']) == 30
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

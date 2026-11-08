#!/usr/bin/env python3
"""Run local Python checks with owned scratch space and bounded evidence.

The default store is build-neverc/local-artifacts. Existing build profiles,
canonical SDKs and historical packets outside that store are never collected.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

from git_check_fixture import CHECKS, GitInputs
from local_artifacts import ArtifactStore, atomic_json, digest


class Interrupted(BaseException):
    def __init__(self, signum):
        self.signum = signum


def interrupted(signum, frame):
    raise Interrupted(signum)


def execute(command, cwd, environment, log, lease_fd, timeout):
    started = time.monotonic()
    process = None
    code, reason = 1, None
    try:
        with log.open('wb') as stream:
            process = subprocess.Popen(command, cwd=cwd, env=environment, stdout=stream,
                                       stderr=subprocess.STDOUT, start_new_session=True,
                                       pass_fds=(lease_fd,))
            try:
                code = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                code, reason = 124, 'timeout'
            except Interrupted as exc:
                code, reason = 128 + exc.signum, 'signal:' + str(exc.signum)
            finally:
                # Retire the whole owned command group, including readers that
                # outlive its main process. No unrelated processes are signalled.
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
    except OSError as exc:
        reason = repr(exc)
        log.write_text(reason + '\n')
    return dict(command=command, returncode=code, reason=reason,
                elapsed_seconds=time.monotonic() - started, log_sha256=digest(log))


def run(args, store):
    command = args.command
    if command and command[0] == '--':
        command = command[1:]
    if not command:
        raise ValueError('run needs a command after --')
    with store.attempt(args.purpose) as attempt:
        work = attempt.directory('work')
        command = [part.replace('{work}', str(work)) for part in command]
        environment = dict(os.environ, NEVERC_ARTIFACT_WORK=str(work),
                           NEVERC_ARTIFACT_RUN_ID=attempt.id,
                           NEVERC_ARTIFACT_STORE=str(store.root),
                           PYTHONDONTWRITEBYTECODE='1', TMPDIR=str(work))
        row = execute(command, Path.cwd(), environment, attempt.path / 'console.log',
                      attempt.lease_fd, args.timeout)
        files = [('console.log', attempt.path / 'console.log')]
        for name in args.retain:
            source = work / name
            if not source.resolve().is_relative_to(work) or not source.is_file():
                row['retention_error'] = 'missing or unsafe report: ' + name
                row['returncode'] = row['returncode'] or 1
                continue
            files.append((name, source))
        receipt = store.publish(args.purpose, dict(run=attempt.id, **row), files)
        print(json.dumps(dict(receipt=receipt, store=str(store.root), **row)), flush=True)
        return row['returncode']


def docs(args, store):
    inputs = GitInputs(args.repo, args.commit, fetch_missing=args.fetch_missing)
    with store.attempt('git-python-docs-registration') as attempt:
        fixture = attempt.directory('fixture')
        checks, logs, manifest, error = [], [], {}, None
        environment = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', TMPDIR=str(attempt.path))
        try:
            manifest = inputs.materialize(fixture, inputs.docs_profile())
            atomic_json(attempt.path / 'inputs.json', manifest)
            logs.append(('inputs.json', attempt.path / 'inputs.json'))
            for script in CHECKS:
                log = attempt.path / (Path(script).stem + '.log')
                row = execute([sys.executable, '-B', str(fixture / script)], fixture,
                              environment, log, attempt.lease_fd, args.timeout)
                row['script'] = script
                checks.append(row)
                logs.append((log.name, log))
                if row['reason'] and row['reason'].startswith('signal:'):
                    break
        except (Exception, Interrupted) as exc:
            error = repr(exc)
        result = dict(commit=inputs.commit, tree=inputs.tree, checks=checks,
                      passed=error is None and len(checks) == len(CHECKS) and all(row['returncode'] == 0 for row in checks),
                      error=error, git_blobs_verified=manifest.get('git_blobs_verified'), fixture_bytes=manifest.get('bytes'),
                      fixture_is_temporary=True, cpp_executed=False)
        receipt = store.publish('git-python-docs-registration', result, logs)
        print(json.dumps(dict(receipt=receipt, store=str(store.root), **result)), flush=True)
        return 0 if result['passed'] else next((r['returncode'] for r in checks if r['returncode']), 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--store', type=Path,
                        default=Path(os.environ.get('NEVERC_ARTIFACT_STORE', 'build-neverc/local-artifacts')))
    parser.add_argument('--max-receipts', type=int, default=16)
    parser.add_argument('--max-bytes', type=int, default=256 * 1024**2)
    commands = parser.add_subparsers(dest='action', required=True)
    run_parser = commands.add_parser('run')
    run_parser.add_argument('--purpose', required=True, help='stable workflow name, not a timestamp')
    run_parser.add_argument('--timeout', type=int, default=300)
    run_parser.add_argument('--retain', action='append', default=[])
    run_parser.add_argument('command', nargs=argparse.REMAINDER)
    docs_parser = commands.add_parser('docs')
    docs_parser.add_argument('--repo', type=Path, default=Path.cwd())
    docs_parser.add_argument('--commit', required=True)
    docs_parser.add_argument('--timeout', type=int, default=300)
    docs_parser.add_argument('--fetch-missing', action='store_true', help='fetch only missing selected Git blob IDs; do not update refs')
    commands.add_parser('gc')
    pin_parser = commands.add_parser('pin')
    pin_parser.add_argument('--owner', required=True)
    pin_parser.add_argument('--run')
    pin_parser.add_argument('--receipt')
    unpin_parser = commands.add_parser('unpin')
    unpin_parser.add_argument('--owner', required=True)
    args = parser.parse_args()
    store = ArtifactStore(args.store, max_receipts=args.max_receipts, max_bytes=args.max_bytes)
    handlers = {number: signal.signal(number, interrupted) for number in (signal.SIGTERM, signal.SIGINT)}
    try:
        if args.action == 'run':
            return run(args, store)
        if args.action == 'docs':
            return docs(args, store)
        if args.action == 'pin':
            store.pin(args.owner, args.run, args.receipt)
        if args.action == 'unpin':
            print(json.dumps(store.unpin(args.owner)))
        if args.action == 'gc':
            print(json.dumps(store.collect()))
        return 0
    except Interrupted as exc:
        return 128 + exc.signum
    finally:
        for number, handler in handlers.items():
            signal.signal(number, handler)


if __name__ == '__main__':
    sys.exit(main())

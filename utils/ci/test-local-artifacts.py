"""Small real-process lifecycle tests; no compiler, SDK download or full clone."""
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest

from git_check_fixture import GitInputs
from local_artifacts import ArtifactStore, digest, read_json


HERE = Path(__file__).resolve().parent
CLI = HERE / 'local-artifacts.py'
CMAKE_MODULE = HERE.parents[1] / 'neverc/cmake/modules/ManagedCPython.cmake'
ENV = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', PYTHONPATH=str(HERE))


def wait_for(predicate):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.025)
    raise AssertionError('timed out waiting for test process')


class LifecycleTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.store = ArtifactStore(self.root / 'store', max_receipts=4, max_bytes=32000)

    def test_repeated_success_failure_and_evidence_are_bounded(self):
        for index in range(30):
            try:
                with self.store.attempt('same-workflow') as attempt:
                    scratch = attempt.directory('build')
                    report = scratch / 'report.json'
                    report.write_text(json.dumps(dict(index=index)) + ' ' * 4096)
                    self.store.publish('same-workflow', {'success': index % 2 == 0}, [('report', report)])
                    if index % 2:
                        raise RuntimeError('expected failure after report')
            except RuntimeError:
                pass
            self.assertEqual(list((self.store.root / 'runs').iterdir()), [])
            self.assertLessEqual(len(list((self.store.root / 'receipts').iterdir())), 4)
            self.assertLessEqual(len(list((self.store.root / 'objects').iterdir())), 4)
            self.assertLessEqual(sum(p.stat().st_size for p in self.store.root.rglob('*') if p.is_file()), 33000)

    def test_content_dedup_and_pinned_old_receipt_survive_new_publish(self):
        source = self.root / 'same.log'
        source.write_bytes(b'actual diagnostic\n' * 20)
        first = self.store.publish('same-key', {'sequence': 0}, [('log', source)])
        self.store.pin('pending-qualifier', receipt=first)
        for index in range(12):
            self.store.publish('same-key', {'sequence': index + 1}, [('log', source)])
        self.assertEqual(len(list((self.store.root / 'objects').iterdir())), 1)
        self.assertEqual(read_json(self.store.root / 'receipts' / first)['result']['sequence'], 0)
        self.assertEqual(digest(self.store.root / 'objects' / digest(source)), digest(source))
        self.store.unpin('pending-qualifier')
        self.store.publish('same-key', {'sequence': 13}, [('log', source)])
        self.assertFalse((self.store.root / 'receipts' / first).exists())

    def test_pending_reference_then_live_reader_then_retirement(self):
        with self.store.attempt('producer') as attempt:
            payload = attempt.path / 'payload'
            payload.write_bytes(b'required by pending reader')
            self.store.pin('formal-check', run_id=attempt.id)
        self.assertTrue(payload.exists())
        with self.store.reader(attempt.id):
            self.store.unpin('formal-check')
            self.assertTrue(payload.exists())
        self.assertFalse(payload.exists())

    def test_consumer_authenticates_receipt_and_object_bytes(self):
        source = self.root / 'diagnostic'
        source.write_bytes(b'genuine bytes')
        identity = self.store.publish('check', {'passed': False}, [('diagnostic', source)],
                                      pending_owner='reader')
        receipt = self.store.receipt(identity)
        obj = self.store.root / 'objects' / receipt['files'][0]['sha256']
        obj.write_bytes(b'tampered bytes')
        with self.assertRaisesRegex(ValueError, 'corrupt diagnostic object'):
            self.store.receipt(identity)
        obj.write_bytes(source.read_bytes())
        (self.store.root / 'receipts' / identity).write_text('{}\n')
        with self.assertRaisesRegex(ValueError, 'corrupt receipt'):
            self.store.receipt(identity)

    def test_sigkill_orphan_recovered_on_next_startup(self):
        code = "from local_artifacts import ArtifactStore; import sys,time; s=ArtifactStore(sys.argv[1]); a=s.attempt('orphan'); r=a.__enter__(); (r.directory('build')/'object').write_bytes(b'x'*8192); (r.path/'console.log').write_text('partial diagnostic before SIGKILL'); print(r.id,flush=True); time.sleep(60)"
        child = subprocess.Popen([sys.executable, '-B', '-c', code, str(self.store.root)],
                                 env=ENV, stdout=subprocess.PIPE, text=True)
        self.addCleanup(lambda: child.poll() is None and child.kill())
        identity = child.stdout.readline().strip()
        self.assertEqual(len(identity), 32)
        self.store.collect()
        self.assertTrue((self.store.root / 'runs' / identity).exists())
        child.kill()
        child.wait(timeout=5)
        child.stdout.close()
        recovered = ArtifactStore(self.store.root)
        self.assertEqual(list((recovered.root / 'runs').iterdir()), [])
        receipt_path, = (recovered.root / 'receipts').glob('*.json')
        receipt = recovered.receipt(receipt_path.name)
        self.assertFalse(receipt['result']['completed'])
        self.assertEqual((recovered.root / 'objects' / receipt['files'][0]['sha256']).read_bytes(),
                         b'partial diagnostic before SIGKILL')

    def test_inherited_reader_lease_prevents_premature_cleanup(self):
        with self.store.attempt('producer') as attempt:
            child = subprocess.Popen([sys.executable, '-B', '-c', 'import time; time.sleep(60)'],
                                     pass_fds=(attempt.lease_fd,))
        try:
            self.store.collect()
            self.assertTrue(attempt.path.exists())
        finally:
            child.terminate()
            child.wait(timeout=5)
        self.store.collect()
        self.assertFalse(attempt.path.exists())

    def test_unknown_paths_and_symlink_targets_are_preserved(self):
        original = self.root / 'canonical-sdk'
        original.mkdir()
        (original / 'input').write_bytes(b'canonical')
        unknown = self.store.root / 'runs' / 'user-not-owned'
        unknown.mkdir()
        with self.store.attempt('symlinks') as attempt:
            (attempt.path / 'sdk').symlink_to(original, target_is_directory=True)
        self.assertEqual((original / 'input').read_bytes(), b'canonical')
        self.assertTrue(unknown.exists())
        with self.assertRaises(ValueError):
            ArtifactStore(original)
        self.assertEqual({p.name for p in original.iterdir()}, {'input'})

    def test_cli_failure_preserves_actual_exit_and_diagnostic(self):
        run = subprocess.run([sys.executable, '-B', str(CLI), '--store', str(self.store.root),
                              'run', '--purpose', 'failing-tool', '--', sys.executable, '-B', '-c',
                              "import sys; print('real failure evidence'); sys.exit(7)"], env=ENV, capture_output=True, text=True)
        self.assertEqual(run.returncode, 7, run.stderr)
        result = json.loads(run.stdout)
        receipt = read_json(self.store.root / 'receipts' / result['receipt'])
        self.assertIn(b'real failure evidence', (self.store.root / 'objects' / receipt['files'][0]['sha256']).read_bytes())
        self.assertEqual(list((self.store.root / 'runs').iterdir()), [])

    def test_cli_sigterm_retires_owned_command_group_and_scratch(self):
        code = "from pathlib import Path; import os,time; Path(os.environ['NEVERC_ARTIFACT_WORK'],'ready').write_text('yes'); print('signal diagnostic',flush=True); time.sleep(60)"
        child = subprocess.Popen([sys.executable, '-B', str(CLI), '--store', str(self.store.root),
                                  'run', '--purpose', 'interrupted-tool', '--', sys.executable, '-B', '-c', code],
                                 env=ENV, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            wait_for(lambda: list((self.store.root / 'runs').glob('*/work/ready')))
            child.terminate()
            stdout, stderr = child.communicate(timeout=5)
            self.assertEqual(child.returncode, 143, stderr)
            self.assertEqual(json.loads(stdout)['reason'], 'signal:15')
            self.assertEqual(list((self.store.root / 'runs').iterdir()), [])
        finally:
            if child.poll() is None:
                child.kill()
                child.wait()

    def test_cli_timeout_uses_same_cleanup_and_retains_diagnostic(self):
        run = subprocess.run([sys.executable, '-B', str(CLI), '--store', str(self.store.root),
                              'run', '--purpose', 'timeout-tool', '--timeout', '1', '--',
                              sys.executable, '-B', '-c', "import time; print('started',flush=True); time.sleep(60)"],
                             env=ENV, capture_output=True, text=True, timeout=5)
        self.assertEqual(run.returncode, 124, run.stderr)
        self.assertEqual(json.loads(run.stdout)['reason'], 'timeout')
        self.assertEqual(list((self.store.root / 'runs').iterdir()), [])


class GitFixtureTests(unittest.TestCase):
    def test_exact_blobs_modes_link_closure_and_unsafe_link_refusal(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, fixture = root / 'repo', root / 'fixture'
            repo.mkdir(); fixture.mkdir()
            def git(*args):
                return subprocess.check_output(['git', *args], cwd=repo, stderr=subprocess.DEVNULL)
            git('init')
            (repo / 'guide.md').write_text('[target](nested/target.md#section)\n')
            (repo / 'nested').mkdir()
            (repo / 'nested/target.md').write_text('# Section\nActual content\n')
            (repo / 'tool.py').write_text('print(42)\n')
            (repo / 'tool.py').chmod(0o755)
            (repo / 'alias').symlink_to('tool.py')
            (repo / 'unsafe').symlink_to('../outside')
            git('add', '.')
            git('-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '-m', 'small fixture')
            inputs = GitInputs(repo, 'HEAD')
            manifest = inputs.materialize(fixture, {'guide.md', 'alias'})
            self.assertEqual(manifest['git_blobs_verified'], 4)
            self.assertEqual((fixture / 'alias').read_bytes(), (repo / 'tool.py').read_bytes())
            self.assertEqual((fixture / 'tool.py').stat().st_mode & 0o777, 0o755)
            for row in manifest['files']:
                self.assertEqual(hashlib.sha256(git('show', inputs.commit + ':' + row['path'])).hexdigest(), row['sha256'])
            with self.assertRaisesRegex(ValueError, 'unsafe Git fixture symlink'):
                inputs.materialize(fixture, {'unsafe'})


@unittest.skipUnless(shutil.which('cmake'), 'CMake not installed')
class SDKTransactionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / 'canonical'
        self.source.mkdir()
        (self.source / 'payload').write_bytes(b'exact SDK fixture' * 128)
        self.build = self.root / 'build'
        self.build.mkdir()
        self.work = self.build / '_deps/neverc-cpython-stage'
        self.destination = self.build / 'python'
        self.script = self.root / 'stage.cmake'
        self.script.write_text(f'''set(CMAKE_BINARY_DIR "{self.build.as_posix()}")
include("{CMAKE_MODULE.as_posix()}")
function(_neverc_managed_cpython_paths)
endfunction()
function(_neverc_managed_cpython_validate root system processor)
  if(TAG)
    file(APPEND "${{CMAKE_BINARY_DIR}}/events" "${{TAG}}:begin\n")
    execute_process(COMMAND "${{CMAKE_COMMAND}}" -E sleep 0.2)
    file(APPEND "${{CMAKE_BINARY_DIR}}/events" "${{TAG}}:end\n")
  endif()
  if(FAIL_VALIDATION)
    message(FATAL_ERROR "injected validation failure after actual copy")
  endif()
  if(INTERRUPT_VALIDATION)
    file(WRITE "${{CMAKE_BINARY_DIR}}/ready" "ready")
    execute_process(COMMAND "${{CMAKE_COMMAND}}" -E sleep 60)
  endif()
endfunction()
function(_neverc_managed_cpython_fingerprint root system output)
  file(SHA256 "${{root}}/payload" _digest)
  set(${{output}} "${{_digest}}" PARENT_SCOPE)
endfunction()
if(RECOVER_ONLY)
  _neverc_managed_cpython_recover_stage("{self.destination.as_posix()}")
else()
  _neverc_managed_cpython_stage("{self.destination.as_posix()}" Fixture x86_64 unix unused fixture-sha "{self.source.as_posix()}" unused)
endif()
''')

    def cmake(self, *definitions):
        return subprocess.run(['cmake', *definitions, '-P', str(self.script)], capture_output=True, text=True)

    def test_repeated_failed_stages_bounded_then_success_removes_transaction(self):
        self.assertEqual(self.cmake().returncode, 0)
        original = digest(self.destination / 'payload')
        (self.source / 'payload').write_bytes(b'updated SDK fixture' * 128)
        for _ in range(12):
            result = self.cmake('-DFAIL_VALIDATION=ON')
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('injected validation failure', result.stderr)
            self.assertEqual(list((self.build / '_deps').glob('neverc-cpython-stage*/*/payload')), [self.work / 'python/payload'])
            self.assertEqual(digest(self.destination / 'payload'), original)
        self.assertEqual(self.cmake().returncode, 0)
        self.assertFalse(self.work.exists())
        self.assertEqual(digest(self.destination / 'payload'), digest(self.source / 'payload'))
        # Recovery also runs on the reuse path, before considering SDK markers.
        self.assertEqual(self.cmake('-DRECOVER_ONLY=ON').returncode, 0)
        self.assertTrue((self.destination / '.neverc-managed-cpython').is_file())

    def test_real_interruption_recovery_and_previous_install_restoration(self):
        child = subprocess.Popen(['cmake', '-DINTERRUPT_VALIDATION=ON', '-P', str(self.script)],
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
        try:
            wait_for(lambda: (self.build / 'ready').exists())
        finally:
            os.killpg(child.pid, signal.SIGKILL)
            child.wait(timeout=5)
        self.assertTrue((self.work / 'python/payload').is_file())
        self.assertEqual(self.cmake('-DRECOVER_ONLY=ON').returncode, 0)
        self.assertFalse(self.work.exists())
        self.assertEqual(self.cmake().returncode, 0)
        # Exact interrupted-publication state: old install moved, new not yet
        # published. Recovery restores the real prior bytes before removing work.
        self.work.mkdir()
        self.destination.rename(self.work / 'previous')
        self.assertEqual(self.cmake('-DRECOVER_ONLY=ON').returncode, 0)
        self.assertEqual(digest(self.destination / 'payload'), digest(self.source / 'payload'))
        self.assertFalse(self.work.exists())

    def test_unowned_transaction_refused_without_deleting_files(self):
        self.work.mkdir(parents=True)
        (self.work / 'user-input').write_text('preserve me')
        result = self.cmake('-DRECOVER_ONLY=ON')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual((self.work / 'user-input').read_text(), 'preserve me')

    def test_concurrent_staging_transactions_are_serialized(self):
        processes = [subprocess.Popen(['cmake', '-DTAG=' + tag, '-P', str(self.script)],
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                     for tag in ('a', 'b')]
        for child in processes:
            stdout, stderr = child.communicate(timeout=10)
            self.assertEqual(child.returncode, 0, stdout + stderr)
        lines = (self.build / 'events').read_text().splitlines()
        self.assertIn(lines, [['a:begin', 'a:end', 'b:begin', 'b:end'],
                              ['b:begin', 'b:end', 'a:begin', 'a:end']])
        self.assertFalse(self.work.exists())

    def test_explicit_source_inside_staging_is_preserved(self):
        self.work.mkdir(parents=True)
        (self.work / 'payload').write_bytes(b'current explicit input')
        Path(str(self.work) + '.owner').write_text('neverc-managed-cpython-stage-v1\n')
        self.script.write_text(self.script.read_text().replace(
            'fixture-sha "' + self.source.as_posix() + '"',
            'fixture-sha "' + self.work.as_posix() + '"'))
        result = self.cmake()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('input is inside', result.stderr)
        self.assertEqual((self.work / 'payload').read_bytes(), b'current explicit input')


if __name__ == '__main__':
    unittest.main()

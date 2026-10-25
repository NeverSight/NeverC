#!/usr/bin/env python3
"""Host-side tests for the GKI QEMU load/unload harness."""

import gzip
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import textwrap
import unittest


TOOLS = Path(__file__).resolve().parent
HARNESS = TOOLS / "run-gki-qemu-smoke.sh"
WRITER = TOOLS / "build-gki-initramfs.py"
WORKFLOW = TOOLS.parents[3] / ".github/workflows/validate-gki-runtime.yml"


def parse_newc(payload):
    offset = 0
    entries = {}
    while True:
        header = payload[offset : offset + 110]
        if len(header) != 110 or header[:6] != b"070701":
            raise AssertionError(f"bad newc header at {offset}")
        fields = [
            int(header[6 + index * 8 : 14 + index * 8], 16) for index in range(13)
        ]
        mode = fields[1]
        size = fields[6]
        rdevmajor = fields[9]
        rdevminor = fields[10]
        namesize = fields[11]
        offset += 110
        raw_name = payload[offset : offset + namesize]
        if not raw_name.endswith(b"\0"):
            raise AssertionError("newc name lacks NUL")
        name = raw_name[:-1].decode()
        offset += namesize
        offset = (offset + 3) & ~3
        data = payload[offset : offset + size]
        offset += size
        offset = (offset + 3) & ~3
        if name == "TRAILER!!!":
            break
        entries[name] = {
            "mode": mode,
            "rdevmajor": rdevmajor,
            "rdevminor": rdevminor,
            "data": data,
        }
    return entries


class InitramfsTests(unittest.TestCase):
    def test_writer_is_deterministic_and_console_is_real_character_device(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            init = root / "init"
            module = root / "module.ko"
            init.write_bytes(b"init-binary")
            module.write_bytes(b"module-binary")
            first = root / "first.cpio.gz"
            second = root / "second.cpio.gz"
            command = [
                "python3",
                str(WRITER),
                "--init",
                str(init),
                "--module",
                str(module),
                "--output",
            ]
            subprocess.run(command + [str(first)], check=True)
            subprocess.run(command + [str(second)], check=True)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            entries = parse_newc(gzip.decompress(first.read_bytes()))
            console = entries["dev/console"]
            self.assertTrue(stat.S_ISCHR(console["mode"]))
            self.assertEqual((console["rdevmajor"], console["rdevminor"]), (5, 1))
            self.assertEqual(entries["init"]["data"], b"init-binary")
            self.assertEqual(entries["neverc-smoke.ko"]["data"], b"module-binary")


class WorkflowContractTests(unittest.TestCase):
    def release_matrix(self, catalog, lock, selected="all"):
        workflow = WORKFLOW.read_text(encoding="utf-8")
        block = workflow.split('          release_profiles=$(jq', 1)[1]
        block = '          release_profiles=$(jq' + block.split(
            '          release_repository=$(jq', 1
        )[0]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            catalog_path, lock_path = root / "catalog.json", root / "lock.json"
            catalog_path.write_text(json.dumps(catalog), encoding="utf-8")
            lock_path.write_text(json.dumps(lock), encoding="utf-8")
            return subprocess.run(
                ["bash", "-eu", "-c",
                 'die() { echo "$*" >&2; exit 1; }\n' +
                 textwrap.dedent(block) + '\nprintf "MATRIX=%s\\n" "$matrix"\n'],
                env={**os.environ, "catalog_path": str(catalog_path),
                     "lock_path": str(lock_path), "INPUT_PROFILE": selected},
                text=True, capture_output=True, check=False,
            )

    @unittest.skipUnless(shutil.which("jq"), "jq is required for the workflow gate")
    def test_unpublished_rc_preview_does_not_enter_the_release_matrix(self):
        runtime = TOOLS.parent
        catalog = json.loads((runtime / "arm64/gki-profiles.json").read_text())
        lock = json.loads((runtime / "arm64/gki-release.json").read_text())
        lock["profiles"].pop("703", None)
        result = self.release_matrix(catalog, lock)
        self.assertEqual(result.returncode, 0, result.stderr)
        matrix = json.loads(result.stdout.split("MATRIX=", 1)[1])
        self.assertEqual(set(matrix), set(lock["profiles"]))
        self.assertNotIn("703", matrix)
        result = self.release_matrix(catalog, lock, "703")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no pinned release artifact: 703", result.stderr)

    @unittest.skipUnless(shutil.which("jq"), "jq is required for the workflow gate")
    def test_published_703_preview_enters_the_release_matrix(self):
        runtime = TOOLS.parent
        catalog = json.loads((runtime / "arm64/gki-profiles.json").read_text())
        lock = json.loads((runtime / "arm64/gki-release.json").read_text())
        result = self.release_matrix(catalog, lock)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("703", json.loads(result.stdout.split("MATRIX=", 1)[1]))
        result = self.release_matrix(catalog, lock, "703")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout.split("MATRIX=", 1)[1]), ["703"])

    @unittest.skipUnless(shutil.which("jq"), "jq is required for the workflow gate")
    def test_missing_stable_release_and_unknown_locked_profiles_still_fail(self):
        catalog = {"profiles": [{"legacy_id": 612}, {"legacy_id": 703}]}
        lock = {"profiles": {"612": {}}}
        result = self.release_matrix(catalog, lock)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("stable profile catalog", result.stderr)
        result = self.release_matrix({"profiles": [{"legacy_id": 612}]},
                                     {"profiles": {"612": {}, "999": {}}})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing from the catalog", result.stderr)

    def test_kernel_matrix_does_not_run_functional_modules(self):
        workflow = WORKFLOW.read_text(encoding="utf-8")
        self.assertNotIn("gki-runtime-functional-modules", workflow)
        self.assertNotIn("--mode functional", workflow)
        self.assertNotIn("gki-qemu-functional-module.c", workflow)

    def test_release_asset_download_is_retried(self):
        workflow = WORKFLOW.read_text(encoding="utf-8")
        download_step = workflow.split(
            "      - name: Resolve and download pinned release asset", 1
        )[1].split("      - name: Verify complete release evidence", 1)[0]
        self.assertIn("for attempt in 1 2 3; do", download_step)
        self.assertIn('rm -f "release/$asset"', download_step)
        self.assertIn(
            'release asset download failed after $attempt attempts',
            download_step,
        )

    @unittest.skipUnless(shutil.which("jq"), "jq is required for the workflow gate")
    def test_download_uses_preview_override_and_stable_default_tag(self):
        workflow = WORKFLOW.read_text(encoding="utf-8")
        step = workflow.split(
            "      - name: Resolve and download pinned release asset", 1
        )[1].split("      - name: Verify complete release evidence", 1)[0]
        script = textwrap.dedent(step.split("        run: |\n", 1)[1])
        lock = json.loads((TOOLS.parent / "arm64/gki-release.json").read_text())
        for profile in ("703", "618"):
            with self.subTest(profile=profile), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                lock_path = root / "runtime/android/kernel/arm64/gki-release.json"
                lock_path.parent.mkdir(parents=True)
                lock_path.write_text(json.dumps(lock))
                binary_dir = root / "bin"
                binary_dir.mkdir()
                gh = binary_dir / "gh"
                gh.write_text(textwrap.dedent('''\
                    #!/usr/bin/env python3
                    import json,pathlib,sys
                    args = sys.argv[1:]
                    assert args[:2] == ["release", "download"], args
                    pathlib.Path("download.json").write_text(json.dumps(args))
                    target = pathlib.Path(args[args.index("--dir") + 1])
                    (target / args[args.index("--pattern") + 1]).write_bytes(b"fixture")
                    '''))
                gh.chmod(0o755)
                result = subprocess.run(
                    ["bash", "-eu", "-c", script], cwd=root,
                    env={**os.environ, "PATH": str(binary_dir) + os.pathsep + os.environ["PATH"],
                         "PROFILE": profile, "RELEASE_REPOSITORY": lock["repository"]["name"],
                         "GITHUB_OUTPUT": str(root / "outputs")},
                    text=True, capture_output=True, check=False,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                args = json.loads((root / "download.json").read_text())
                expected = lock["profiles"][profile].get("tag", lock["tag"])
                self.assertEqual(args[2], expected)
                self.assertEqual(args[args.index("--pattern") + 1],
                                 lock["profiles"][profile]["asset"])

    @unittest.skipUnless(shutil.which("jq"), "jq is required for the workflow gate")
    def test_metadata_checks_each_release_and_rejects_changed_preview_digest(self):
        workflow = WORKFLOW.read_text(encoding="utf-8")
        script = '          release_repo_json=$(gh api' + workflow.split(
            '          release_repo_json=$(gh api', 1
        )[1].split('          {\n            echo "source_sha=', 1)[0]
        lock = json.loads((TOOLS.parent / "arm64/gki-release.json").read_text())
        tags = sorted({entry.get("tag", lock["tag"])
                       for entry in lock["profiles"].values()})
        self.assertEqual(len(tags), 2)
        for bad_digest in (False, True):
            with self.subTest(bad_digest=bad_digest), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                (root / "lock.json").write_text(json.dumps(lock))
                binary_dir = root / "bin"
                binary_dir.mkdir()
                gh = binary_dir / "gh"
                gh.write_text(textwrap.dedent('''\
                    #!/usr/bin/env python3
                    import json,os,pathlib,sys
                    args = sys.argv[1:]
                    assert args[0] == "api", args
                    lock = json.loads(pathlib.Path("lock.json").read_text())
                    calls_path = pathlib.Path("calls.json")
                    calls = json.loads(calls_path.read_text()) if calls_path.exists() else []
                    calls.append(args[1])
                    calls_path.write_text(json.dumps(calls))
                    base = "repos/" + lock["repository"]["name"]
                    if args[1] == base:
                        print(json.dumps(lock["repository"]))
                    else:
                        assert args[1].startswith(base + "/releases/tags/"), args
                        tag = args[1].split("/tags/", 1)[1]
                        assets = []
                        for profile,entry in lock["profiles"].items():
                            if entry.get("tag", lock["tag"]) != tag:
                                continue
                            digest = entry["sha256"]
                            if profile == "703" and os.environ["BAD_DIGEST"] == "1":
                                digest = "0" * 64
                            assets.append({"name": entry["asset"], "size": entry["size"],
                                           "digest": "sha256:" + digest})
                        print(json.dumps({"tag_name": tag, "draft": False, "assets": assets}))
                    '''))
                gh.chmod(0o755)
                result = subprocess.run(
                    ["bash", "-eu", "-c", 'die() { echo "$*" >&2; exit 1; }\n' +
                     textwrap.dedent(script)], cwd=root,
                    env={**os.environ, "PATH": str(binary_dir) + os.pathsep + os.environ["PATH"],
                         "release_repository": lock["repository"]["name"],
                         "release_repository_id": str(lock["repository"]["id"]),
                         "release_tags": json.dumps(tags), "lock_path": str(root / "lock.json"),
                         "BAD_DIGEST": "1" if bad_digest else "0"},
                    text=True, capture_output=True, check=False,
                )
                if bad_digest:
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("release metadata digest mismatch", result.stderr)
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)
                    calls = json.loads((root / "calls.json").read_text())
                    self.assertEqual([path.split("/tags/", 1)[1] for path in calls
                                      if "/tags/" in path], tags)


class HarnessTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.image = self.root / "Image"
        self.module = self.root / "smoke.ko"
        self.image.write_bytes(b"kernel")
        self.module.write_bytes(b"module")
        self.compiler = self.make_executable(
            "fake-cc.py",
            """
            #!/usr/bin/env python3
            import pathlib,sys
            if '--version' in sys.argv:
                print('fake aarch64 static compiler 1.0')
                raise SystemExit(0)
            output = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])
            output.write_bytes(b'fake-static-init')
            output.chmod(0o755)
            """,
        )
        self.qemu = self.make_executable(
            "fake-qemu.py",
            """
            #!/usr/bin/env python3
            import os,sys,time
            if '--version' in sys.argv:
                print('fake qemu-system-aarch64 1.0')
                raise SystemExit(0)
            scenario = os.environ.get('FAKE_QEMU_SCENARIO', 'success')
            if scenario == 'success':
                print('NEVERC_GKI_LOAD_PASS')
                print('NEVERC_GKI_UNLOAD_PASS')
            elif scenario == 'success-crlf':
                sys.stdout.buffer.write(
                    b'NEVERC_GKI_LOAD_PASS\\r\\nNEVERC_GKI_UNLOAD_PASS\\r\\n'
                )
            elif scenario == 'load-failure':
                print('NEVERC_GKI_LOAD_FAIL errno=8 Exec format error')
            elif scenario == 'unload-failure':
                print('NEVERC_GKI_LOAD_PASS')
                print('NEVERC_GKI_UNLOAD_FAIL errno=16 Device busy')
            elif scenario == 'timeout':
                print('booting forever', flush=True)
                time.sleep(5)
            elif scenario == 'empty-success':
                pass
            else:
                raise SystemExit(9)
            """,
        )

    def tearDown(self):
        self.temporary.cleanup()

    def make_executable(self, name, body):
        path = self.root / name
        path.write_text(textwrap.dedent(body).lstrip())
        path.chmod(path.stat().st_mode | stat.S_IXUSR)
        return path

    def run_harness(self, scenario, timeout="2"):
        output = self.root / f"output-{scenario}"
        environment = os.environ.copy()
        environment["FAKE_QEMU_SCENARIO"] = scenario
        result = subprocess.run(
            [
                "bash",
                str(HARNESS),
                "--image",
                str(self.image),
                "--module",
                str(self.module),
                "--output-dir",
                str(output),
                "--cross-cc",
                str(self.compiler),
                "--qemu",
                str(self.qemu),
                "--timeout",
                timeout,
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            env=environment,
            check=False,
        )
        log = output / "qemu.log"
        return result, log.read_text() if log.exists() else ""

    def test_success_requires_both_markers(self):
        result, log = self.run_harness("success")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("NEVERC_GKI_LOAD_PASS", log)
        self.assertIn("NEVERC_GKI_UNLOAD_PASS", log)

    def test_success_accepts_real_qemu_crlf_markers(self):
        result, _ = self.run_harness("success-crlf")
        self.assertEqual(result.returncode, 0, result.stdout)
        raw_log = (self.root / "output-success-crlf/qemu.log").read_bytes()
        self.assertIn(b"NEVERC_GKI_LOAD_PASS\r\n", raw_log)
        self.assertIn(b"NEVERC_GKI_UNLOAD_PASS\r\n", raw_log)

    def test_load_failure_is_rejected(self):
        result, _ = self.run_harness("load-failure")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("NEVERC_GKI_LOAD_FAIL", result.stdout)

    def test_unload_failure_wins_even_after_load_pass(self):
        result, _ = self.run_harness("unload-failure")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("NEVERC_GKI_UNLOAD_FAIL", result.stdout)

    def test_timeout_is_rejected(self):
        result, _ = self.run_harness("timeout", timeout="1")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("timed out", result.stdout)

    def test_early_zero_exit_without_markers_is_rejected(self):
        result, _ = self.run_harness("empty-success")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing guest success markers", result.stdout)

    def test_help_and_missing_input_are_deterministic(self):
        help_result = subprocess.run(
            ["bash", str(HARNESS), "--help"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        self.assertEqual(help_result.returncode, 0)
        self.assertIn("Usage:", help_result.stdout)
        missing = subprocess.run(
            ["bash", str(HARNESS), "--image", str(self.root / "missing")],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        self.assertEqual(missing.returncode, 2)
        self.assertIn("missing or unreadable kernel Image", missing.stdout)


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Host-side tests for the GKI QEMU load/unload harness."""

import gzip
import os
from pathlib import Path
import stat
import subprocess
import tempfile
import textwrap
import unittest


TOOLS = Path(__file__).resolve().parent
HARNESS = TOOLS / "run-gki-qemu-smoke.sh"
WRITER = TOOLS / "build-gki-initramfs.py"


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

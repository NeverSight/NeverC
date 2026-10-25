#!/usr/bin/env python3
"""Exercise the GKI release publisher with a local GitHub CLI substitute."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest


TOOLS = Path(__file__).resolve().parent
WORKFLOW = TOOLS.parents[3] / ".github/workflows/build-gki-kernels.yml"
SOURCE_SHA = "1" * 40


class ReleasePublisherTests(unittest.TestCase):
    def publish(self, profile="703", tag="gki-703-test", existing=False):
        workflow = WORKFLOW.read_text(encoding="utf-8")
        script = textwrap.dedent(workflow.split(
            "      - name: Create or update Release\n        run: |\n", 1
        )[1])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            artifact = root / "artifacts/gki-703/gki-android18-7.3-build.tar.gz"
            artifact.parent.mkdir(parents=True)
            artifact.write_bytes(b"verified kernel build fixture")
            binary_dir = root / "bin"
            binary_dir.mkdir()
            gh = binary_dir / "gh"
            gh.write_text(textwrap.dedent('''\
                #!/usr/bin/env python3
                import json, os, pathlib, sys
                args = sys.argv[1:]
                path = pathlib.Path("gh-calls.json")
                calls = json.loads(path.read_text()) if path.exists() else []
                calls.append(args)
                path.write_text(json.dumps(calls))
                if args[:2] == ["release", "view"]:
                    raise SystemExit(0 if os.environ["FAKE_EXISTS"] == "1" else 1)
                if args[:2] == ["release", "upload"]:
                    raise SystemExit("existing asset must not be overwritten")
                if args[:2] != ["release", "create"]:
                    raise SystemExit("unexpected GitHub operation")
                '''), encoding="utf-8")
            gh.chmod(0o755)
            result = subprocess.run(
                ["bash", "-eu", "-c", script], cwd=root,
                env={**os.environ, "PATH": str(binary_dir) + os.pathsep + os.environ["PATH"],
                     "KERNEL_VERSION": profile, "RELEASE_TAG": tag,
                     "SOURCE_SHA": SOURCE_SHA, "FAKE_EXISTS": "1" if existing else "0"},
                text=True, capture_output=True, check=False,
            )
            calls_path = root / "gh-calls.json"
            calls = json.loads(calls_path.read_text()) if calls_path.exists() else []
            notes_path, sums_path = root / "release-notes.md", root / "SHA256SUMS"
            return (result, calls,
                    notes_path.read_text() if notes_path.exists() else "",
                    sums_path.read_text() if sums_path.exists() else "")

    def test_703_is_a_separate_prerelease_with_source_and_checksums(self):
        result, calls, notes, sums = self.publish()
        self.assertEqual(result.returncode, 0, result.stderr)
        create = calls[-1]
        self.assertEqual(create[:3], ["release", "create", "gki-703-test"])
        self.assertIn("--prerelease", create)
        self.assertIn("--latest=false", create)
        self.assertEqual(create[create.index("--target") + 1], SOURCE_SHA)
        self.assertIn("SHA256SUMS", create)
        self.assertEqual(sums, hashlib.sha256(b"verified kernel build fixture").hexdigest() +
                         "  gki-android18-7.3-build.tar.gz\n")
        self.assertIn("7.2.0-rc9", notes)
        self.assertIn("lockdep/debug locks disabled", notes)
        self.assertIn(SOURCE_SHA, notes)

    def test_stable_release_preserves_latest_compiler_release(self):
        result, calls, _, _ = self.publish(profile="618", tag="gki-build-test")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("--prerelease", calls[-1])
        self.assertIn("--latest=false", calls[-1])

    def test_non_gki_tag_is_rejected_before_contacting_github(self):
        result, calls, _, _ = self.publish(tag="v3389.1.5")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Invalid GKI release tag", result.stderr)
        self.assertEqual(calls, [])

    def test_existing_release_assets_are_not_overwritten(self):
        result, calls, _, _ = self.publish(existing=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(calls[-1][:2], ["release", "upload"])
        self.assertNotIn("--clobber", calls[-1])


if __name__ == "__main__":
    unittest.main()

import contextlib
import importlib.util
import io
import tempfile
import unittest
from pathlib import Path
from unittest import mock


SCRIPT = Path(__file__).with_name("sync-workflow-concurrency.py")
SPEC = importlib.util.spec_from_file_location("sync_workflow_concurrency", SCRIPT)
SYNC = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SYNC)

JOBS = "jobs:\n  test:\n    runs-on: ubuntu-latest\n"


class SyncWorkflowConcurrencyTest(unittest.TestCase):
    def workflow(self, triggers, *, filename="test.yml", concurrency=""):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        path = Path(temporary.name) / filename
        original = triggers + "\n" + concurrency + JOBS
        path.write_text(original, encoding="utf-8")
        return path, original

    def test_automatic_events_are_recognized_independently_of_spelling_and_order(self):
        for triggers in (
            "on:\n  workflow_dispatch:\n  push:\n",
            "on:\n  workflow_dispatch:\n  pull_request:\n",
            "on: push\n",
            "on: 'pull_request' # review changes\n",
            "on: [workflow_dispatch, pull_request]\n",
            '"on": ["schedule", workflow_dispatch]\n',
            "'on': # a quoted key and comment\n  'workflow_dispatch': {}\n  'push':\n",
            "on:\n    workflow_dispatch:\n    pull_request_target:\n",
            'on: {workflow_dispatch: {}, "push": null}\n',
            "on: {push: {branches: [dev], paths: ['a,b', 'a#b', 'it''s.c']}}\n",
            "on:\n  workflow_dispatch:\n    inputs:\n      schedule:\n"
            "        default: 'manual'\n  schedule:\n    - cron: '0 0 * * *'\n",
            "on: [\n  workflow_dispatch, # still automatic\n  pull_request,\n  ]\n",
            "# maintainer's workflow\non:\n  workflow_dispatch:\n"
            "# Events need not be first or contiguous.\n  push: # branch updates\n",
        ):
            with self.subTest(triggers=triggers):
                path, _original = self.workflow(triggers)
                self.assertTrue(SYNC.sync_workflow(path, write=True))
                actual = path.read_text(encoding="utf-8")
                self.assertTrue(actual.startswith(triggers))
                self.assertIn(SYNC.render_block(path.name), actual)
                self.assertEqual(actual.count("\nconcurrency:\n"), 1)
                self.assertTrue(actual.endswith(JOBS))
                self.assertFalse(SYNC.sync_workflow(path, write=True))
                self.assertEqual(path.read_text(encoding="utf-8"), actual)

    def test_manual_release_and_non_event_text_are_unchanged(self):
        for triggers in (
            "on: workflow_dispatch\n",
            "on: [workflow_call, workflow_dispatch]\n",
            "on:\n  workflow_call:\n  workflow_dispatch:\n",
            "on: {workflow_call: {}, workflow_dispatch: {}}\n",
            "on: release\n",
            "on: {release: {types: [published]}}\n",
            "on: [push, release]\n",
            "on:\n  workflow_dispatch:\n    inputs:\n      push:\n"
            "        description: 'push: schedule: pull_request:'\n",
            "# on: push\non: workflow_dispatch # pull_request\n",
            "name: 'on: push'\n",
            "on: {workflow_dispatch: {inputs: {push: {default: 'schedule'}}}}\n",
        ):
            with self.subTest(triggers=triggers):
                path, original = self.workflow(triggers)
                self.assertFalse(SYNC.sync_workflow(path, write=True))
                self.assertEqual(path.read_text(encoding="utf-8"), original)

    def test_tag_only_pushes_never_gain_branch_cancellation(self):
        for triggers in (
            "on:\n  push:\n    tags: ['v*']\n  workflow_dispatch:\n",
            "on:\n  workflow_dispatch:\n  push:\n    paths: ['**']\n    tags: ['v*']\n",
            "'on':\n  'workflow_dispatch':\n  'push':\n    'tags-ignore': ['private*']\n",
            "on: {workflow_dispatch: {}, push: {tags: ['v*']}}\n",
            "on:\n  workflow_dispatch: {}\n  push: {tags-ignore: ['private*']}\n",
            "on: {\n  workflow_dispatch: {},\n  push: {paths: ['a,b'], tags: ['v*']},\n  }\n",
        ):
            with self.subTest(triggers=triggers):
                path, original = self.workflow(triggers)
                self.assertFalse(SYNC.sync_workflow(path, write=True))
                self.assertEqual(path.read_text(encoding="utf-8"), original)

                managed = SYNC.render_block(path.name)
                path.write_text(triggers + "\n" + managed + "\n" + JOBS,
                                encoding="utf-8")
                before = path.read_bytes()
                self.assertTrue(SYNC.sync_workflow(path, write=False))
                self.assertEqual(path.read_bytes(), before)
                self.assertTrue(SYNC.sync_workflow(path, write=True))
                actual = path.read_text(encoding="utf-8")
                self.assertNotIn("concurrency:", actual)
                self.assertTrue(actual.startswith(triggers))
                self.assertTrue(actual.endswith(JOBS))
                self.assertFalse(SYNC.sync_workflow(path, write=True))

    def test_branch_filters_with_tag_filters_remain_automatic(self):
        for filters in (
            "    tags: ['v*']\n    branches: [dev]\n",
            "    branches-ignore: [legacy]\n    tags-ignore: ['private*']\n",
        ):
            with self.subTest(filters=filters):
                path, _original = self.workflow("on:\n  push:\n" + filters)
                self.assertTrue(SYNC.sync_workflow(path, write=True))
        path, _original = self.workflow(
            "on: {push: {tags: ['v*'], branches: [dev]}}\n")
        self.assertTrue(SYNC.sync_workflow(path, write=True))

    def test_dry_run_reports_change_without_writing(self):
        path, _original = self.workflow("on: push\n")
        before = path.read_bytes()
        self.assertTrue(SYNC.sync_workflow(path, write=False))
        self.assertEqual(path.read_bytes(), before)

    def test_skip_workflow_and_custom_concurrency_are_preserved(self):
        custom = "concurrency:\n  group: release\n  cancel-in-progress: false\n\n"
        for policy in (
            custom,
            '"concurrency": {group: release, cancel-in-progress: false}\n\n',
            "concurrency: manual-policy\n\n",
        ):
            with self.subTest(policy=policy):
                path, original = self.workflow("on: push\n", concurrency=policy)
                self.assertFalse(SYNC.sync_workflow(path, write=True))
                self.assertEqual(path.read_text(encoding="utf-8"), original)
        path, original = self.workflow("on: push\n", filename="build-gki-kernels.yml",
                                       concurrency=custom)
        self.assertFalse(SYNC.sync_workflow(path, write=True))
        self.assertEqual(path.read_text(encoding="utf-8"), original)
        path, original = self.workflow("on: {push: {tags: ['v*']}}\n",
                                       concurrency=custom)
        self.assertFalse(SYNC.sync_workflow(path, write=True))
        self.assertEqual(path.read_text(encoding="utf-8"), original)
        path, _original = self.workflow("on: push\n", filename="merge-fuzz.yml")
        self.assertTrue(SYNC.sync_workflow(path, write=True))
        self.assertIn("group: merge-fuzz-${{ github.ref }}",
                      path.read_text(encoding="utf-8"))

    def test_unsupported_or_ambiguous_syntax_is_rejected_without_writing(self):
        for triggers in (
            "on: *shared_events\n",
            "on: push\non: workflow_dispatch\n",
            "on: {push: {}, push: {tags: ['v*']}}\n",
            "on: {push: {tags: ['v*']}} trailing\n",
            "on: [push, {workflow_dispatch: {}}]\n",
        ):
            with self.subTest(triggers=triggers):
                path, _original = self.workflow(triggers)
                before = path.read_bytes()
                with self.assertRaises(ValueError):
                    SYNC.sync_workflow(path, write=True)
                self.assertEqual(path.read_bytes(), before)

    def test_cli_reports_unsupported_trigger_syntax(self):
        path, _original = self.workflow("on: *shared_events\n")
        before = path.read_bytes()
        stderr = io.StringIO()
        with mock.patch.object(SYNC, "WORKFLOW_DIR", path.parent), \
                contextlib.redirect_stderr(stderr):
            self.assertEqual(SYNC.main(["--write"]), 1)
        self.assertIn("test.yml: unsupported workflow trigger mapping", stderr.getvalue())
        self.assertEqual(path.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()

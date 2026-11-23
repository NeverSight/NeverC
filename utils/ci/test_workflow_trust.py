import importlib.util
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GITHUB = ROOT / ".github"
SCRIPT = Path(__file__).with_name("sync-workflow-concurrency.py")
SPEC = importlib.util.spec_from_file_location("sync_workflow_concurrency", SCRIPT)
SYNC = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SYNC)

# These events load the workflow from the pull request merge commit, which a
# fork controls. pull_request_target and workflow_run load the default-branch
# file instead.
FORK_CONTROLLED_TRIGGERS = {
    "pull_request",
    "pull_request_review",
    "pull_request_review_comment",
}
UNTRUSTED_CHECKOUT = (
    "github.event.pull_request.head.sha",
    "github.event.pull_request.head.ref",
    "github.event.pull_request.head.repo",
    "github.event.workflow_run.head_sha",
    "github.event.workflow_run.head_repository",
    "refs/pull/",
)
USES = re.compile(r"^[ \t]*(?:- )?uses:\s+(\S+)")
CHECKOUT_STEP = re.compile(
    r"(?m)^(?P<indent>[ \t]*)- (?:name:|uses:).*(?:\n(?P=indent)  .*)*"
)


def workflow_files():
    return sorted(GITHUB.rglob("*.yml")) + sorted(GITHUB.rglob("*.yaml"))


class WorkflowTrustTest(unittest.TestCase):
    def test_third_party_actions_are_pinned_to_commit_shas(self):
        mutable = []
        for path in workflow_files():
            for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                match = USES.match(line.split(" #", 1)[0].rstrip())
                if match is None:
                    continue
                reference = match.group(1)
                relative = f"{path.relative_to(ROOT).as_posix()}:{lineno}: {reference}"
                if reference.startswith("./"):
                    if "@" in reference:
                        mutable.append(relative)
                    continue
                revision = reference.rsplit("@", 1)[-1] if "@" in reference else ""
                if not re.fullmatch(r"[0-9a-f]{40}", revision):
                    mutable.append(relative)
        self.assertEqual(mutable, [])

    def test_workflows_keep_fork_controlled_definitions_out(self):
        failures = []
        for path in workflow_files():
            source = path.read_text(encoding="utf-8")
            relative = path.relative_to(ROOT).as_posix()
            try:
                triggers = set(SYNC.workflow_events(source))
            except ValueError as error:
                failures.append(f"{relative}: {error}")
                continue
            failures.extend(
                f"{relative}: {trigger} executes a fork-controlled workflow file"
                for trigger in sorted(triggers & FORK_CONTROLLED_TRIGGERS))
            if "pull_request_target" in triggers and "actions/checkout@" in source:
                failures.append(
                    f"{relative}: pull_request_target checks out a repository")
            for step in CHECKOUT_STEP.finditer(source):
                block = step.group(0)
                if "actions/checkout@" not in block:
                    continue
                failures.extend(
                    f"{relative}: checkout selects {expression}"
                    for expression in UNTRUSTED_CHECKOUT if expression in block)
        self.assertEqual(failures, [])


if __name__ == "__main__":
    unittest.main()

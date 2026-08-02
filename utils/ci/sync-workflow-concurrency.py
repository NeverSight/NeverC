#!/usr/bin/env python3
"""Ensure GitHub workflow files declare shared CI concurrency settings."""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
WORKFLOW_DIR = REPO_ROOT / ".github" / "workflows"

SKIP_WORKFLOWS = {
    "build-gki-kernels.yml",  # manual multi-hour jobs; keeps cancel-in-progress: false
}

# Tag/release workflows should not inherit branch push cancellation semantics.
SKIP_IF_TAG_ONLY = re.compile(
    r"(?ms)^on:\s*\n\s*push:\s*\n\s*tags:"
)

CONCURRENCY_COMMENT = (
    "# Cancel stale runs on the same ref when a newer commit is pushed. Set repo\n"
    "# variable CI_CANCEL_IN_PROGRESS=false to keep overlapping runs (legacy mode).\n"
)


def strip_concurrency_block(text: str) -> str:
    block_pattern = re.compile(
        re.escape(CONCURRENCY_COMMENT)
        + r"^concurrency:\n"
        + r"^  group: .*\n"
        + r"^  cancel-in-progress: .*\n",
        re.MULTILINE,
    )
    text = block_pattern.sub("", text)
    orphan_pattern = re.compile(
        r"(?:^# Cancel stale runs on the same ref when a newer commit is pushed\. Set repo\n"
        r"^# variable CI_CANCEL_IN_PROGRESS=false to keep overlapping runs \(legacy mode\)\.\n)+",
        re.MULTILINE,
    )
    text = orphan_pattern.sub("", text)
    return re.sub(r"\n{3,}(?=jobs:\s*$)", "\n\n", text, flags=re.MULTILINE)

CUSTOM_GROUP = {
    "http3-quiche-interop.yml": "http3-quiche-interop-${{ github.ref }}",
    "http2-grpc-interop.yml": "http2-grpc-interop-${{ github.ref }}",
    "http-websocket-interop.yml": "http-websocket-interop-${{ github.ref }}",
    "network-protocol-fuzz.yml": "network-protocol-fuzz-${{ github.ref }}",
    "tls-protocol-fuzz.yml": "tls-protocol-fuzz-${{ github.ref }}",
    "merge-fuzz.yml": "merge-fuzz-${{ github.ref }}",
}

TRIGGER_PATTERN = re.compile(
    r"(?m)^on:\s*(?:\[.*\bpush\b|\{|\bpush:|\bpull_request:|\bpull_request_target:|\bschedule:)"
)


def workflow_has_triggers(text: str) -> bool:
    return TRIGGER_PATTERN.search(text) is not None


def render_block(filename: str) -> str:
    group = CUSTOM_GROUP.get(filename, "${{ github.workflow }}-${{ github.ref }}")
    return (
        CONCURRENCY_COMMENT
        + "concurrency:\n"
        + f"  group: {group}\n"
        + "  cancel-in-progress: ${{ vars.CI_CANCEL_IN_PROGRESS != 'false' }}\n"
    )


def insert_concurrency(text: str, block: str) -> str:
    text = strip_concurrency_block(text)
    jobs_match = re.search(r"^jobs:\s*$", text, re.MULTILINE)
    if not jobs_match:
        raise ValueError("could not locate jobs: section")
    insert_at = jobs_match.start()
    prefix = text[:insert_at].rstrip("\n")
    suffix = text[insert_at:]
    if not prefix.endswith("\n"):
        prefix += "\n"
    return f"{prefix}\n{block}\n{suffix}"


def sync_workflow(path: Path, *, write: bool) -> bool:
    if path.name in SKIP_WORKFLOWS:
        return False

    original = path.read_text(encoding="utf-8")

    if SKIP_IF_TAG_ONLY.search(original):
        updated = strip_concurrency_block(original)
        if updated == original:
            return False
        if write:
            path.write_text(updated, encoding="utf-8")
        return True

    if not workflow_has_triggers(original):
        return False

    updated = insert_concurrency(original, render_block(path.name))
    if updated == original:
        return False

    if write:
        path.write_text(updated, encoding="utf-8")
    return True


def main(argv: list[str]) -> int:
    write = "--write" in argv
    changed: list[str] = []

    for path in sorted(WORKFLOW_DIR.glob("*.yml")):
        if sync_workflow(path, write=write):
            changed.append(path.name)

    action = "Updated" if write else "Would update"
    if changed:
        print(f"{action} {len(changed)} workflow(s):")
        for name in changed:
            print(f"  - {name}")
    else:
        print("All workflows already declare shared concurrency settings.")

    if not write and changed:
        print("\nRe-run with --write to apply changes.")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

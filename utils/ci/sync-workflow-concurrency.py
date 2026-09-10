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

AUTOMATIC_EVENTS = {"push", "pull_request", "pull_request_target", "schedule"}
ON_KEY = re.compile(r'''(?m)^(?:on|'on'|"on")[ \t]*:[ \t]*(.*)$''')
CONCURRENCY_KEY = re.compile(
    r'''(?m)^(?:concurrency|'concurrency'|"concurrency")[ \t]*:''')
KEY = r'''(?:[a-zA-Z_][a-zA-Z_0-9-]*|'[a-zA-Z_][a-zA-Z_0-9-]*'|"[a-zA-Z_][a-zA-Z_0-9-]*")'''
MAPPING_ITEM = re.compile(r"(" + KEY + r")[ \t]*:[ \t]*(.*)", re.DOTALL)
# Keep quoted strings intact when removing comments or splitting flow values.
YAML_TOKEN = re.compile(
    r"""'(?:[^']|'')*'|"(?:[^"\\]|\\.)*"|(?P<comment>(?<!\S)\#[^\n]*)|[\s\S]"""
)


def split_flow(value: str) -> list[str]:
    parts = []
    start = 0
    brackets = []
    for token in YAML_TOKEN.finditer(value):
        char = token.group()
        if char in ("'", '"'):
            raise ValueError("unterminated quoted workflow trigger value")
        if char in ("[", "{"):
            brackets.append(char)
        elif char in ("]", "}"):
            if not brackets or brackets.pop() != {"]": "[", "}": "{"}[char]:
                raise ValueError("unbalanced flow value in workflow triggers")
        elif char == "," and not brackets:
            parts.append(value[start:token.start()].strip())
            start = token.end()
    if brackets:
        raise ValueError("unbalanced flow value in workflow triggers")
    parts.append(value[start:].strip())
    if not parts[-1]:
        parts.pop()  # YAML permits a trailing comma.
    return parts


def mapping_items(value: str) -> dict[str, str]:
    """Read direct mapping keys; nested values stay opaque until needed."""
    if value.lstrip().startswith("{"):
        value = value.strip()
        if not value.endswith("}"):
            raise ValueError("unterminated workflow trigger mapping")
        items = split_flow(value[1:-1])
    else:
        lines = [line for line in value.splitlines() if line.strip()]
        if not lines:
            return {}
        indent = len(lines[0]) - len(lines[0].lstrip(" "))
        items = []
        for line in lines:
            leading = len(line) - len(line.lstrip(" "))
            if leading < indent:
                raise ValueError("inconsistent workflow trigger indentation")
            if leading == indent:
                items.append(line[indent:])
            else:
                items[-1] += "\n" + line
    result = {}
    for item in items:
        match = MAPPING_ITEM.fullmatch(item)
        if not match:
            raise ValueError("unsupported workflow trigger mapping: " + item[:80])
        key = match[1].strip("\"'")
        if key in result:
            raise ValueError("duplicate workflow trigger key: " + key)
        result[key] = match[2]
    return result


def workflow_events(text: str) -> dict[str, str]:
    # This reader handles scalar/list events and block/flow mappings without
    # interpreting job bodies, YAML aliases, or nested input names as events.
    # Unsupported trigger syntax raises before any workflow is rewritten.
    matches = list(ON_KEY.finditer(text))
    if not matches:
        return {}
    if len(matches) != 1:
        raise ValueError("duplicate top-level on key")
    match = matches[0]
    lines = [match[1]]
    for line in text[match.end():].splitlines():
        if line.lstrip().startswith("#"):
            continue
        if line.strip() and not line.startswith((" ", "\t")):
            break
        lines.append(line)
    value = "".join(token.group() for token in YAML_TOKEN.finditer("\n".join(lines))
                    if token.lastgroup != "comment")
    inline = value.strip()
    if re.fullmatch(KEY, inline):
        return {inline.strip("\"'"): ""}
    if inline.startswith("["):
        if not inline.endswith("]"):
            raise ValueError("unterminated workflow event list")
        events = split_flow(inline[1:-1])
        if not all(re.fullmatch(KEY, event) for event in events):
            raise ValueError("unsupported workflow event list")
        return {event.strip("\"'"): "" for event in events}
    return mapping_items(value)


def workflow_is_tag_only(events: dict[str, str]) -> bool:
    push = events.get("push", "")
    if not push.strip() or push.strip() in ("null", "~"):
        return False
    filters = mapping_items(push)
    return bool({"tags", "tags-ignore"} & filters.keys()) and not (
        {"branches", "branches-ignore"} & filters.keys())


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
    # An explicit policy belongs to the workflow author. Do not append a
    # duplicate key or replace it with the shared cancellation policy.
    if CONCURRENCY_KEY.search(strip_concurrency_block(original)):
        return False
    events = workflow_events(original)

    # Tag/release workflows must not inherit branch cancellation semantics.
    if workflow_is_tag_only(events):
        updated = strip_concurrency_block(original)
        if updated == original:
            return False
        if write:
            path.write_text(updated, encoding="utf-8")
        return True

    if "release" in events or not AUTOMATIC_EVENTS.intersection(events):
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
        try:
            if sync_workflow(path, write=write):
                changed.append(path.name)
        except ValueError as error:
            print(f"{path.name}: {error}", file=sys.stderr)
            return 1

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

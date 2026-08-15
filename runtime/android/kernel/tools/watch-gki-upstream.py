#!/usr/bin/env python3
"""Watch official AOSP GKI branch tips, KMI generation, and NeverC-read fields.

Reads the NeverC profile catalog, fetches each family's `kernel/common`
branch head (linux VERSION/PATCHLEVEL/SUBLEVEL plus KMI_GENERATION), and
optionally the matching `-kminext` branch. Also fingerprints the headers
that define structs the runtime reads. Compares the live tip to a
previous snapshot (or to the catalog on the first run) and can post a
Discord webhook when a version, KMI, or used-field layout moved.

The webhook URL is taken only from GKI_WATCH_DISCORD_WEBHOOK_URL. Never print it.
"""

from __future__ import annotations

import argparse
import base64
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request


TOOLS_ROOT = Path(__file__).resolve().parent
RUNTIME_ROOT = TOOLS_ROOT.parent
DEFAULT_CATALOG = RUNTIME_ROOT / "arm64/gki-profiles.json"
AOSP_KERNEL_COMMON = "https://android.googlesource.com/kernel/common"
GKI_BRANCH_RE = re.compile(r"^android(\d+)-(\d+)\.(\d+)$")
KMI_NEXT_RE = re.compile(r"^android(\d+)-(\d+)\.(\d+)-kminext$")
MAKEFILE_FIELD_RE = re.compile(
    r"^(VERSION|PATCHLEVEL|SUBLEVEL)\s*=\s*(\d+)\s*$", re.MULTILINE
)
KMI_GENERATION_RE = re.compile(
    r"""^KMI_GENERATION\s*=\s*["']?(\d+)["']?\s*$""", re.MULTILINE
)
POINTER_RE = re.compile(r"^[A-Za-z0-9_./-]+\.(?:scl|bzl|constants)$")
LS_REMOTE_REF_RE = re.compile(r"^[0-9a-f]+\s+refs/heads/(.+)$")
WEBHOOK_ENV = "GKI_WATCH_DISCORD_WEBHOOK_URL"
FETCH_RETRIES = 3
FETCH_TIMEOUT_SEC = 30


def _load_layouts():
    spec = importlib.util.spec_from_file_location(
        "watch_gki_layouts", TOOLS_ROOT / "watch-gki-layouts.py"
    )
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load watch-gki-layouts.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


LAYOUTS = _load_layouts()


class WatchError(RuntimeError):
    """A catalog or upstream probe failed hard."""


def load_catalog(path):
    raw = json.loads(Path(path).read_text(encoding="utf-8"))
    profiles = raw.get("profiles")
    if not isinstance(profiles, list) or not profiles:
        raise WatchError(f"catalog {path} has no profiles")
    families = []
    for profile in profiles:
        name = profile.get("kernel_name")
        if not isinstance(name, str) or not GKI_BRANCH_RE.match(name):
            raise WatchError(f"catalog profile has a bad kernel_name: {name!r}")
        families.append(
            {
                "legacy_id": int(profile["legacy_id"]),
                "kernel_name": name,
                "android_release": int(profile["android_release"]),
                "linux_major": int(profile["linux_major"]),
                "linux_minor": int(profile["linux_minor"]),
                "linux_patch": int(profile["linux_patch"]),
                "kmi_generation": int(profile["kmi_generation"]),
            }
        )
    return families


def googlesource_text_url(branch, path):
    return f"{AOSP_KERNEL_COMMON}/+/refs/heads/{branch}/{path}?format=TEXT"


def fetch_bytes(url, opener=None):
    request = urllib.request.Request(url, headers={"User-Agent": "neverc-gki-watch/1"})
    last_error = None
    for attempt in range(FETCH_RETRIES):
        try:
            if opener is None:
                with urllib.request.urlopen(request, timeout=FETCH_TIMEOUT_SEC) as response:
                    return response.read()
            with opener.open(request, timeout=FETCH_TIMEOUT_SEC) as response:
                return response.read()
        except (urllib.error.URLError, TimeoutError, OSError) as error:
            last_error = error
            if isinstance(error, urllib.error.HTTPError) and error.code in (404, 410):
                raise
            if attempt + 1 < FETCH_RETRIES:
                time.sleep(1.5 * (attempt + 1))
    raise WatchError(f"failed to fetch AOSP text after {FETCH_RETRIES} tries") from last_error


def fetch_googlesource_text(branch, path, opener=None):
    raw = fetch_bytes(googlesource_text_url(branch, path), opener=opener)
    try:
        return base64.b64decode(raw, validate=False).decode("utf-8")
    except (ValueError, UnicodeDecodeError) as error:
        raise WatchError(f"AOSP text for {branch}:{path} is not UTF-8 base64") from error


def parse_makefile_version(text):
    found = {key: None for key in ("VERSION", "PATCHLEVEL", "SUBLEVEL")}
    for match in MAKEFILE_FIELD_RE.finditer(text):
        found[match.group(1)] = int(match.group(2))
    if None in found.values():
        raise WatchError("Makefile is missing VERSION/PATCHLEVEL/SUBLEVEL")
    return found["VERSION"], found["PATCHLEVEL"], found["SUBLEVEL"]


def parse_kmi_generation(text):
    match = KMI_GENERATION_RE.search(text)
    if match is None:
        return None
    return int(match.group(1))


def is_pointer_file(text):
    stripped = text.strip()
    return bool(POINTER_RE.match(stripped)) and "\n" not in stripped


def read_kmi_generation(branch, opener=None):
    seen = []
    pending = ["build.config.constants", "build.config.common"]
    while pending:
        path = pending.pop(0)
        if path in seen:
            continue
        seen.append(path)
        try:
            text = fetch_googlesource_text(branch, path, opener=opener)
        except urllib.error.HTTPError as error:
            if error.code in (404, 410):
                continue
            raise
        generation = parse_kmi_generation(text)
        if generation is not None:
            return generation, path
        if is_pointer_file(text):
            pending.insert(0, text.strip())
    raise WatchError(f"{branch} has no KMI_GENERATION in constants/common")


def probe_branch(branch, opener=None):
    makefile = fetch_googlesource_text(branch, "Makefile", opener=opener)
    major, minor, patch = parse_makefile_version(makefile)
    generation, source = read_kmi_generation(branch, opener=opener)
    parsed = GKI_BRANCH_RE.match(branch)
    kmi_version = (
        f"{major}.{minor}-android{parsed.group(1)}-{generation}"
        if parsed is not None
        else f"{major}.{minor}-kmi{generation}"
    )
    return {
        "branch": branch,
        "linux_major": major,
        "linux_minor": minor,
        "linux_patch": patch,
        "kmi_generation": generation,
        "kmi_source": source,
        "linux_release": f"{major}.{minor}.{patch}",
        "kmi_version": kmi_version,
    }


def probe_optional_branch(branch, opener=None):
    try:
        return probe_branch(branch, opener=opener)
    except (WatchError, urllib.error.URLError, TimeoutError, OSError):
        return None


def fetch_header_text(branch, path, opener=None, cache=None):
    key = (branch, path)
    if cache is not None and key in cache:
        return cache[key]
    try:
        text = fetch_googlesource_text(branch, path, opener=opener)
    except (WatchError, urllib.error.HTTPError, urllib.error.URLError, TimeoutError, OSError):
        text = None
    if cache is not None:
        cache[key] = text
    return text


def probe_layouts(branch, opener=None, cache=None):
    texts = {}
    for path in LAYOUTS.unique_header_paths():
        try:
            text = fetch_header_text(branch, path, opener=opener, cache=cache)
        except (WatchError, urllib.error.URLError, TimeoutError, OSError):
            text = None
        if text:
            texts[path] = text
    structs = LAYOUTS.fingerprint_headers(texts)
    if not structs:
        return None
    return LAYOUTS.compact_fingerprint(structs)


def linux_tuple(record):
    return (
        int(record["linux_major"]),
        int(record["linux_minor"]),
        int(record["linux_patch"]),
    )


def parse_ls_remote_heads(text):
    branches = []
    for line in text.splitlines():
        match = LS_REMOTE_REF_RE.match(line.strip())
        if match is None:
            continue
        name = match.group(1)
        if GKI_BRANCH_RE.match(name) or KMI_NEXT_RE.match(name):
            branches.append(name)
    return sorted(set(branches))


def list_gki_branches(ls_remote=None):
    if ls_remote is not None:
        return parse_ls_remote_heads(ls_remote)
    try:
        output = subprocess.run(
            ["git", "ls-remote", "--heads", AOSP_KERNEL_COMMON],
            check=True,
            capture_output=True,
            text=True,
            timeout=60,
        ).stdout
    except (OSError, subprocess.SubprocessError) as error:
        raise WatchError("git ls-remote of kernel/common failed") from error
    return parse_ls_remote_heads(output)


def catalog_kernel_names(families):
    return {family["kernel_name"] for family in families}


def family_identity(family):
    return {
        "legacy_id": family["legacy_id"],
        "kernel_name": family["kernel_name"],
        "android_release": family["android_release"],
        "linux_major": family["linux_major"],
        "linux_minor": family["linux_minor"],
        "linux_patch": family["linux_patch"],
        "kmi_generation": family["kmi_generation"],
        "linux_release": (
            f"{family['linux_major']}.{family['linux_minor']}."
            f"{family['linux_patch']}"
        ),
    }


def probe_family(family, opener=None, header_cache=None):
    live = probe_branch(family["kernel_name"], opener=opener)
    kminext = probe_optional_branch(f"{family['kernel_name']}-kminext", opener=opener)
    cache = {} if header_cache is None else header_cache
    live_layout = probe_layouts(family["kernel_name"], opener=opener, cache=cache)
    kminext_layout = None
    if kminext is not None:
        kminext_layout = probe_layouts(kminext["branch"], opener=opener, cache=cache)
    record = {
        **family_identity(family),
        "live": {
            "linux_major": live["linux_major"],
            "linux_minor": live["linux_minor"],
            "linux_patch": live["linux_patch"],
            "linux_release": live["linux_release"],
            "kmi_generation": live["kmi_generation"],
            "kmi_source": live["kmi_source"],
            "kmi_version": live["kmi_version"],
        },
        "kminext": None
        if kminext is None
        else {
            "branch": kminext["branch"],
            "linux_release": kminext["linux_release"],
            "kmi_generation": kminext["kmi_generation"],
            "kmi_source": kminext["kmi_source"],
        },
        "layout": live_layout,
        "kminext_layout": kminext_layout,
    }
    return record


def snapshot_family(record):
    live = record["live"]
    kminext = record.get("kminext")
    return {
        "legacy_id": record["legacy_id"],
        "kernel_name": record["kernel_name"],
        "linux_major": live["linux_major"],
        "linux_minor": live["linux_minor"],
        "linux_patch": live["linux_patch"],
        "kmi_generation": live["kmi_generation"],
        "kminext_kmi_generation": None
        if kminext is None
        else kminext["kmi_generation"],
        "layout": record.get("layout"),
        "kminext_layout": record.get("kminext_layout"),
    }


def load_snapshot(path):
    if path is None or not Path(path).exists():
        return None
    raw = json.loads(Path(path).read_text(encoding="utf-8"))
    families = raw.get("families")
    if not isinstance(families, dict):
        raise WatchError(f"snapshot {path} is missing families")
    return raw


def baseline_from_catalog(families):
    return {
        "schema": 1,
        "source": "catalog",
        "families": {
            family["kernel_name"]: {
                "legacy_id": family["legacy_id"],
                "kernel_name": family["kernel_name"],
                "linux_major": family["linux_major"],
                "linux_minor": family["linux_minor"],
                "linux_patch": family["linux_patch"],
                "kmi_generation": family["kmi_generation"],
                "kminext_kmi_generation": None,
            }
            for family in families
        },
        "known_gki_branches": sorted(catalog_kernel_names(families)),
    }


def _catalog_identity(record):
    return {
        "linux_major": record["linux_major"],
        "linux_minor": record["linux_minor"],
        "linux_patch": record["linux_patch"],
        "kmi_generation": record["kmi_generation"],
        "kminext_kmi_generation": None,
    }


def _previous_family_state(previous, record):
    """Use catalog identity when a retained snapshot row lacks version fields."""
    fallback = _catalog_identity(record)
    if not isinstance(previous, dict):
        return fallback
    try:
        state = {
            "linux_major": int(previous["linux_major"]),
            "linux_minor": int(previous["linux_minor"]),
            "linux_patch": int(previous["linux_patch"]),
            "kmi_generation": int(previous["kmi_generation"]),
        }
    except (KeyError, TypeError, ValueError):
        state = fallback
    state["kminext_kmi_generation"] = previous.get("kminext_kmi_generation")
    state["layout"] = previous.get("layout")
    state["kminext_layout"] = previous.get("kminext_layout")
    return state


def collect_changes(records, snapshot, known_branches):
    previous_families = {} if snapshot is None else snapshot.get("families", {})
    previous_branches = set()
    if snapshot is not None:
        previous_branches = set(snapshot.get("known_gki_branches") or [])
    changes = []
    for record in records:
        name = record["kernel_name"]
        live = record["live"]
        previous = _previous_family_state(previous_families.get(name), record)
        catalog_release = (
            f"{record['linux_major']}.{record['linux_minor']}."
            f"{record['linux_patch']}"
        )
        prev_release = (
            f"{previous['linux_major']}.{previous['linux_minor']}."
            f"{previous['linux_patch']}"
        )
        if linux_tuple(live) != (
            int(previous["linux_major"]),
            int(previous["linux_minor"]),
            int(previous["linux_patch"]),
        ):
            changes.append(
                {
                    "kind": "gki_version",
                    "kernel_name": name,
                    "legacy_id": record["legacy_id"],
                    "from": prev_release,
                    "to": live["linux_release"],
                    "catalog": catalog_release,
                }
            )
        if int(live["kmi_generation"]) != int(previous["kmi_generation"]):
            changes.append(
                {
                    "kind": "kmi",
                    "kernel_name": name,
                    "legacy_id": record["legacy_id"],
                    "from": int(previous["kmi_generation"]),
                    "to": int(live["kmi_generation"]),
                    "catalog": int(record["kmi_generation"]),
                }
            )
        kminext = record.get("kminext")
        previous_next = previous.get("kminext_kmi_generation")
        if kminext is not None:
            next_kmi = int(kminext["kmi_generation"])
            live_kmi = int(live["kmi_generation"])
            became_ahead = next_kmi > live_kmi and (
                previous_next is None or int(previous_next) != next_kmi
            )
            if became_ahead:
                changes.append(
                    {
                        "kind": "kmi_next",
                        "kernel_name": name,
                        "legacy_id": record["legacy_id"],
                        "stable": live_kmi,
                        "kminext": next_kmi,
                        "branch": kminext["branch"],
                    }
                )
        live_layout = record.get("layout")
        previous_layout = previous.get("layout")
        if previous_layout and live_layout:
            for change in LAYOUTS.diff_compact(
                previous_layout, live_layout, against="snapshot"
            ):
                change["kernel_name"] = name
                change["legacy_id"] = record["legacy_id"]
                changes.append(change)
        kminext_layout = record.get("kminext_layout")
        previous_kminext_layout = previous.get("kminext_layout")
        if live_layout and kminext_layout:
            kminext_diffs = LAYOUTS.diff_compact(
                live_layout, kminext_layout, against="kminext"
            )
            kminext_moved = previous_kminext_layout is None or (
                previous_kminext_layout.get("digest") != kminext_layout.get("digest")
            )
            if kminext_diffs and kminext_moved:
                for change in kminext_diffs:
                    change["kernel_name"] = name
                    change["legacy_id"] = record["legacy_id"]
                    if kminext is not None:
                        change["branch"] = kminext["branch"]
                    changes.append(change)
    catalog_names = {record["kernel_name"] for record in records}
    for branch in known_branches:
        if not GKI_BRANCH_RE.match(branch):
            continue
        if branch in catalog_names or branch in previous_branches:
            continue
        if snapshot is None or snapshot.get("source") != "live":
            continue
        changes.append({"kind": "new_branch", "branch": branch})
    return changes


def catalog_drift(records):
    drift = []
    for record in records:
        live = record["live"]
        catalog_release = (
            f"{record['linux_major']}.{record['linux_minor']}."
            f"{record['linux_patch']}"
        )
        if linux_tuple(live) != (
            record["linux_major"],
            record["linux_minor"],
            record["linux_patch"],
        ) or int(live["kmi_generation"]) != int(record["kmi_generation"]):
            drift.append(
                {
                    "kernel_name": record["kernel_name"],
                    "legacy_id": record["legacy_id"],
                    "catalog_release": catalog_release,
                    "catalog_kmi": int(record["kmi_generation"]),
                    "live_release": live["linux_release"],
                    "live_kmi": int(live["kmi_generation"]),
                }
            )
    return drift


def build_report(records, errors, snapshot, known_branches, force_notify):
    changes = collect_changes(records, snapshot, known_branches)
    return {
        "schema": 1,
        "source": AOSP_KERNEL_COMMON,
        "force_notify": bool(force_notify),
        "has_updates": bool(changes),
        "changes": changes,
        "catalog_drift": catalog_drift(records),
        "families": records,
        "errors": errors,
        "known_gki_branches": known_branches,
        "compared_to": "snapshot"
        if snapshot is not None and snapshot.get("source") != "catalog"
        else "catalog",
    }


def write_snapshot(path, records, known_branches, previous_snapshot=None):
    previous_families = {}
    previous_branches = []
    if previous_snapshot is not None:
        previous_families = previous_snapshot.get("families") or {}
        previous_branches = list(previous_snapshot.get("known_gki_branches") or [])
    families = {
        name: dict(previous)
        for name, previous in previous_families.items()
        if isinstance(previous, dict)
    }
    for record in records:
        snap = snapshot_family(record)
        previous = previous_families.get(record["kernel_name"]) or {}
        snap["layout"] = LAYOUTS.merge_compact(
            previous.get("layout"), snap.get("layout")
        )
        snap["kminext_layout"] = LAYOUTS.merge_compact(
            previous.get("kminext_layout"), snap.get("kminext_layout")
        )
        if snap.get("kminext_kmi_generation") is None:
            snap["kminext_kmi_generation"] = previous.get("kminext_kmi_generation")
        families[record["kernel_name"]] = snap
    payload = {
        "schema": 1,
        "source": "live",
        "families": families,
        "known_gki_branches": sorted(set(previous_branches) | set(known_branches)),
    }
    Path(path).write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return payload


def _change_lines(changes, kind, formatter):
    return [formatter(change) for change in changes if change["kind"] == kind]


def _layout_was_probed(report):
    return any(record.get("layout") for record in report.get("families") or [])


def _has_identity_updates(changes):
    return any(
        change["kind"] in {"gki_version", "kmi", "kmi_next"} for change in changes
    )


def format_markdown_report(report):
    lines = [
        "## GKI / KMI upstream watch",
        "",
        f"Source: `{report['source']}`",
        f"Compared to: `{report['compared_to']}`",
        f"Updates: {'yes' if report['has_updates'] else 'no'}",
        "",
    ]
    if report["errors"]:
        lines.append("### Probe errors")
        for error in report["errors"]:
            lines.append(f"- `{error['kernel_name']}`: {error['message']}")
        lines.append("")
    if report["has_updates"]:
        kmi = _change_lines(
            report["changes"],
            "kmi",
            lambda change: (
                f"- `{change['kernel_name']}` ({change['legacy_id']}) "
                f"KMI {change['from']} → **{change['to']}** "
                f"(catalog {change['catalog']})"
            ),
        )
        nxt = _change_lines(
            report["changes"],
            "kmi_next",
            lambda change: (
                f"- `{change['branch']}` KMI **{change['kminext']}** "
                f"(stable `{change['kernel_name']}` still {change['stable']})"
            ),
        )
        ver = _change_lines(
            report["changes"],
            "gki_version",
            lambda change: (
                f"- `{change['kernel_name']}` ({change['legacy_id']}) "
                f"{change['from']} → **{change['to']}** "
                f"(catalog {change['catalog']})"
            ),
        )
        new = _change_lines(
            report["changes"],
            "new_branch",
            lambda change: f"- `{change['branch']}`",
        )
        if kmi:
            lines.extend(["### KMI generation changed", *kmi, ""])
        if nxt:
            lines.extend(["### Upcoming KMI (`-kminext`)", *nxt, ""])
        if ver:
            lines.extend(["### GKI linux version changed", *ver, ""])
        if new:
            lines.extend(["### New official GKI branch", *new, ""])
        layout = _change_lines(
            report["changes"],
            "layout",
            lambda change: (
                f"- `{change.get('branch', change['kernel_name'])}` "
                f"({change['legacy_id']}) {change['severity']} "
                f"{change['struct']}.{change['field']}: {change['detail']}"
            ),
        )
        if layout:
            lines.extend(["### NeverC-read fields changed", *layout, ""])
        elif _has_identity_updates(report["changes"]) and _layout_was_probed(report):
            lines.extend(
                [
                    "### NeverC-read fields",
                    "- No used-field index/type change on the compared tips.",
                    "",
                ]
            )
    if report["catalog_drift"]:
        lines.append("### Catalog still behind live tip")
        for item in report["catalog_drift"]:
            lines.append(
                f"- `{item['kernel_name']}` ({item['legacy_id']}) "
                f"catalog {item['catalog_release']}/KMI{item['catalog_kmi']} "
                f"vs live {item['live_release']}/KMI{item['live_kmi']}"
            )
        lines.append("")
    lines.append("### Live tips")
    for record in report["families"]:
        live = record["live"]
        extra = ""
        if record.get("kminext") is not None:
            extra = f", kminext KMI {record['kminext']['kmi_generation']}"
        lines.append(
            f"- `{record['kernel_name']}` ({record['legacy_id']}) "
            f"{live['linux_release']} KMI {live['kmi_generation']}{extra}"
        )
    lines.append("")
    return "\n".join(lines)


def build_discord_payload(report):
    changes = report["changes"]
    title = "GKI / KMI upstream update" if report["has_updates"] else "GKI / KMI watch snapshot"
    color = 0x2ECC71
    if any(
        change["kind"] == "layout" and change.get("severity") == "offset_risk"
        for change in changes
    ):
        color = 0xC0392B
    elif any(change["kind"] == "kmi" for change in changes):
        color = 0xE74C3C
    elif any(change["kind"] == "kmi_next" for change in changes):
        color = 0xE67E22
    elif any(
        change["kind"] == "layout" and change.get("severity") == "sizeof_risk"
        for change in changes
    ):
        color = 0xD35400
    elif any(change["kind"] == "new_branch" for change in changes):
        color = 0x9B59B6
    elif any(change["kind"] == "gki_version" for change in changes):
        color = 0x3498DB
    elif report["errors"]:
        color = 0xF1C40F

    sections = []
    kmi = _change_lines(
        changes,
        "kmi",
        lambda change: (
            f"`{change['kernel_name']}` ({change['legacy_id']}) "
            f"KMI {change['from']} → **{change['to']}**"
        ),
    )
    nxt = _change_lines(
        changes,
        "kmi_next",
        lambda change: (
            f"`{change['branch']}` is already KMI **{change['kminext']}** "
            f"(stable branch is still {change['stable']})"
        ),
    )
    ver = _change_lines(
        changes,
        "gki_version",
        lambda change: (
            f"`{change['kernel_name']}` ({change['legacy_id']}) "
            f"{change['from']} → **{change['to']}**"
        ),
    )
    new = _change_lines(
        changes,
        "new_branch",
        lambda change: f"`{change['branch']}`",
    )
    if kmi:
        sections.append("**KMI generation changed**\n" + "\n".join(kmi))
    if nxt:
        sections.append("**Upcoming KMI (`-kminext`)**\n" + "\n".join(nxt))
    if ver:
        sections.append("**GKI version updated**\n" + "\n".join(ver))
    if new:
        sections.append("**New official GKI branch**\n" + "\n".join(new))
    layout = _change_lines(
        changes,
        "layout",
        lambda change: (
            f"`{change.get('branch', change['kernel_name'])}` "
            f"{change['struct']}.{change['field']}: {change['detail']}"
        ),
    )
    if layout:
        if len(layout) > 12:
            layout = layout[:12] + [f"... +{len(layout) - 12} more"]
        sections.append("**NeverC-read fields changed**\n" + "\n".join(layout))
    elif _has_identity_updates(changes) and _layout_was_probed(report):
        sections.append(
            "**NeverC-read fields**\n"
            "No used-field index/type change on the compared tips."
        )
    if report["catalog_drift"]:
        drift_lines = [
            f"`{item['kernel_name']}` catalog "
            f"{item['catalog_release']}/KMI{item['catalog_kmi']} → live "
            f"{item['live_release']}/KMI{item['live_kmi']}"
            for item in report["catalog_drift"]
        ]
        sections.append("**Catalog pin is behind live tip**\n" + "\n".join(drift_lines))
    if report["errors"]:
        sections.append(
            "**Probe failed**\n"
            + "\n".join(
                f"`{error['kernel_name']}`: {error['message']}"
                for error in report["errors"]
            )
        )
    if not sections:
        live_lines = [
            f"`{record['kernel_name']}` {record['live']['linux_release']} "
            f"KMI {record['live']['kmi_generation']}"
            for record in report["families"]
        ]
        sections.append("Current upstream tips:\n" + "\n".join(live_lines))

    description = "\n\n".join(sections)
    if len(description) > 3900:
        description = description[:3890] + "\n..."
    return {
        "username": "NeverC GKI watch",
        "embeds": [
            {
                "title": title,
                "description": description,
                "color": color,
                "footer": {"text": "AOSP kernel/common · GKI_WATCH_DISCORD_WEBHOOK_URL"},
            }
        ],
    }


def redact_webhook(url):
    if not url:
        return ""
    return re.sub(r"(https://discord(?:app)?\.com/api/webhooks/\d+)/.+", r"\1/<redacted>", url)


def post_discord(webhook_url, payload, opener=None):
    if not webhook_url:
        raise WatchError(f"{WEBHOOK_ENV} is empty")
    body = json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(
        webhook_url,
        data=body,
        method="POST",
        headers={"Content-Type": "application/json", "User-Agent": "neverc-gki-watch/1"},
    )
    try:
        if opener is None:
            with urllib.request.urlopen(request, timeout=FETCH_TIMEOUT_SEC) as response:
                response.read()
                return response.status
        with opener.open(request, timeout=FETCH_TIMEOUT_SEC) as response:
            response.read()
            return getattr(response, "status", 204)
    except urllib.error.HTTPError as error:
        raise WatchError(f"Discord webhook rejected the payload (HTTP {error.code})") from None
    except (urllib.error.URLError, TimeoutError, OSError) as error:
        raise WatchError("Discord webhook request failed") from error


def should_notify(report, force_notify):
    return bool(force_notify or report["has_updates"] or report["errors"])


def probe_all(families, opener=None, ls_remote=None):
    records = []
    errors = []
    header_cache = {}
    for family in families:
        try:
            records.append(
                probe_family(family, opener=opener, header_cache=header_cache)
            )
        except Exception as error:  # noqa: BLE001 — keep other families
            errors.append(
                {
                    "kernel_name": family["kernel_name"],
                    "legacy_id": family["legacy_id"],
                    "message": str(error),
                }
            )
    try:
        known_branches = list_gki_branches(ls_remote=ls_remote)
    except WatchError as error:
        errors.append({"kernel_name": "*", "legacy_id": 0, "message": str(error)})
        known_branches = sorted(catalog_kernel_names(families))
    return records, errors, known_branches


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path, default=DEFAULT_CATALOG)
    parser.add_argument("--snapshot-in", type=Path)
    parser.add_argument("--snapshot-out", type=Path)
    parser.add_argument("--report-out", type=Path)
    parser.add_argument("--summary-out", type=Path)
    parser.add_argument(
        "--notify",
        action="store_true",
        help=f"POST to ${WEBHOOK_ENV} when the tip moved",
    )
    parser.add_argument(
        "--force-notify",
        action="store_true",
        help="POST even when the tip matches the last snapshot",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print the Discord payload without posting",
    )
    parser.add_argument(
        "--ls-remote-file",
        type=Path,
        help="Use a saved git ls-remote listing instead of the network",
    )
    return parser.parse_args(argv)


def main(argv=None, opener=None, environ=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    env = os.environ if environ is None else environ
    families = load_catalog(args.catalog)
    snapshot = load_snapshot(args.snapshot_in)
    ls_remote = None
    if args.ls_remote_file is not None:
        ls_remote = Path(args.ls_remote_file).read_text(encoding="utf-8")
    records, errors, known_branches = probe_all(
        families, opener=opener, ls_remote=ls_remote
    )
    if not records and errors:
        raise WatchError("every GKI family failed to probe")
    report = build_report(
        records,
        errors,
        snapshot,
        known_branches,
        args.force_notify,
    )
    markdown = format_markdown_report(report)
    sys.stdout.write(markdown)
    if not markdown.endswith("\n"):
        sys.stdout.write("\n")
    if args.report_out is not None:
        Path(args.report_out).write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
    if args.summary_out is not None:
        Path(args.summary_out).write_text(markdown, encoding="utf-8")
    if args.snapshot_out is not None:
        write_snapshot(
            args.snapshot_out,
            records,
            known_branches,
            previous_snapshot=snapshot,
        )

    payload = build_discord_payload(report)
    if args.dry_run:
        sys.stdout.write(json.dumps(payload, ensure_ascii=False, indent=2) + "\n")
        return 0
    if args.notify or args.force_notify:
        if not should_notify(report, args.force_notify):
            print("No GKI/KMI updates; Discord left quiet.")
            return 0
        webhook = env.get(WEBHOOK_ENV, "").strip()
        if not webhook:
            print(
                f"{WEBHOOK_ENV} is unset; skipping Discord "
                "(set the repository secret and re-run)."
            )
            return 0
        post_discord(webhook, payload, opener=opener)
        print("Discord notification sent.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except WatchError as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)

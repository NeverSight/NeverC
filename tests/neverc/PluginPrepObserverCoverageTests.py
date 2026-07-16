#!/usr/bin/env python3

from __future__ import annotations

import collections
import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
OBSERVER_HEADER = ROOT / "neverc/include/neverc/Scan/PrepObserver.h"
PLUGIN_HEADER = ROOT / "neverc/include/neverc/Plugin/PluginPrep.h"
BRIDGE_SOURCE = ROOT / "neverc/lib/Plugin/Frontend/PluginPrepObserver.cpp"


def fail(message: str) -> None:
    print(f"PluginPrepObserver coverage failure: {message}", file=sys.stderr)
    raise SystemExit(1)


observer_text = OBSERVER_HEADER.read_text(encoding="utf-8")
observer_base = observer_text.split("class ChainedPrepObserver", 1)[0]
observer_methods = re.findall(
    r"\bvirtual\s+(?:void|bool)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(",
    observer_base,
)

bridge_text = BRIDGE_SOURCE.read_text(encoding="utf-8")
bridge_methods = re.findall(
    r"\b(?:void|bool)\s+PluginPrepObserver::"
    r"([A-Za-z_][A-Za-z0-9_]*)\s*\(",
    bridge_text,
)
mapped_methods = [
    method for method in bridge_methods if method in set(observer_methods)
]

if collections.Counter(mapped_methods) != collections.Counter(observer_methods):
    fail(
        "PrepObserver override set differs from PluginPrepObserver: "
        f"expected={collections.Counter(observer_methods)}, "
        f"actual={collections.Counter(mapped_methods)}"
    )

plugin_text = PLUGIN_HEADER.read_text(encoding="utf-8")
event_definitions = re.findall(
    r"^#define\s+NEVERC_PREP_EVENT_([A-Z][A-Z0-9_]*)\s+"
    r"UINT32_C\((\d+)\)\s*$",
    plugin_text,
    re.MULTILINE,
)
events = {
    name: int(value)
    for name, value in event_definitions
    if name != "COUNT"
}
if len(events) != 39:
    fail(f"expected 39 stable event IDs, found {len(events)}")
if sorted(events.values()) != list(range(1, 40)):
    fail("stable event IDs must be contiguous from 1 through 39")

mapped_events = re.findall(
    r"makeEvent\s*\(\s*NEVERC_PREP_EVENT_([A-Z][A-Z0-9_]*)\s*\)",
    bridge_text,
)
expected_events = collections.Counter(events.keys())
if collections.Counter(mapped_events) != expected_events:
    fail(
        "event mapping set differs from stable event IDs: "
        f"expected={expected_events}, "
        f"actual={collections.Counter(mapped_events)}"
    )

if len(observer_methods) != len(events):
    fail(
        f"PrepObserver has {len(observer_methods)} callbacks but "
        f"the C ABI publishes {len(events)} events"
    )

print(
    f"PluginPrepObserver coverage OK: "
    f"{len(observer_methods)} callbacks, {len(events)} events"
)

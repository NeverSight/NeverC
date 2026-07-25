#!/usr/bin/env python3

"""Hold the plugin API guides to what the schema and headers actually say.

Checks, without a build:

1. Every ``neverc.*`` phase or verifier the guides name exists in
   PhaseSchema.json, so renaming a phase cannot leave prose behind.
2. Every ``Neverc*`` / ``NEVERC_*`` / ``neverc_*`` identifier the guides name
   is declared in a public header or exported by the SDK's CMake package.
3. Every locale of a guide cites the same identifiers as the English page,
   because a technical name is never translated and a divergence means a
   translation dropped or mangled one.
4. The phase totals, policy counts, domain counts and sealed-gate list in
   README match the schema, in every locale.
5. The example CMake targets README advertises are defined by the SDK.

Check 3 only sees prose that carries a citable name, so a dropped paragraph
that carries none slips past it; check-docs-links.py compares the shape of
every translation and catches that.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DOCS = ROOT / "docs" / "plugin-api"
HEADERS = ROOT / "neverc/include/neverc/Plugin"
SCHEMA = HEADERS / "Schema" / "PhaseSchema.json"
SDK = ROOT / "pluginsdk"

LOCALES = ["", "zh-CN", "zh-TW", "ja", "ko", "fr", "de", "es", "it", "ru", "ar"]

FENCE = re.compile(r"^\s*```")
TICK = re.compile(r"`([^`\n]+)`")
DEFINITION = re.compile(r"^\[([^\]]+)\]:\s*(\S+)\s*$")
PHASE = re.compile(r"^neverc\.[a-z0-9_.]+$")
SYMBOL = re.compile(r"^(NEVERC_[A-Z0-9_]+|Neverc[A-Za-z0-9_]+|neverc_[a-z0-9_]+)$")
EXAMPLE_TARGET = re.compile(r"`(neverc-plugin-example-[a-z0-9-]+)`")
# the phase table packs two domain/count pairs per row, so the trailing pipe
# has to stay available as the next match's opening pipe
COUNT_ROW = re.compile(r"\|\s*`(\w+)`\s*\|\s*(\d+)\s*(?=\|)")

ARABIC_INDIC = str.maketrans("٠١٢٣٤٥٦٧٨٩", "0123456789")
FULL_WIDTH = {ord(c): chr(ord(c) - 0xFEE0) for c in "０１２３４５６７８９"}


def normalize_digits(text: str) -> str:
    return text.translate(ARABIC_INDIC).translate(FULL_WIDTH)


def page(stem: str, locale: str) -> Path:
    return DOCS / (f"{stem}.md" if not locale else f"{stem}.{locale}.md")


def guide_pages() -> list[Path]:
    pages = []
    for directory, subdirectories, names in os.walk(DOCS):
        subdirectories[:] = [d for d in subdirectories if d != "__pycache__"]
        pages.extend(Path(directory) / n for n in sorted(names) if n.endswith(".md"))
    return sorted(pages)


def locale_of(name: str) -> str:
    parts = name[:-3].split(".")
    return parts[-1] if len(parts) >= 2 and parts[-1] in LOCALES else ""


def stem_of(name: str) -> str:
    parts = name[:-3].split(".")
    return ".".join(parts[:-1]) if len(parts) >= 2 and parts[-1] in LOCALES else name[:-3]


def declared_symbols() -> set[str]:
    """Names a plugin author can actually write: headers plus the CMake package."""
    text = []
    for directory, _, names in os.walk(HEADERS):
        for name in sorted(names):
            if name.endswith((".h", ".inc")):
                text.append((Path(directory) / name).read_text(
                    encoding="utf-8", errors="replace"))
                # a header is citable by its own name
                text.append(Path(name).stem)
    for directory, _, names in os.walk(SDK):
        for name in sorted(names):
            if name == "CMakeLists.txt" or name.endswith(".cmake.in"):
                text.append((Path(directory) / name).read_text(
                    encoding="utf-8", errors="replace"))
    return set(re.findall(
        r"\b(NEVERC_[A-Z0-9_]+|Neverc[A-Za-z0-9_]+|neverc_[a-z0-9_]+)\b",
        "\n".join(text)))


def prose_citations(path: Path) -> Counter:
    """Backticked spans outside fenced code and link definitions."""
    found: Counter = Counter()
    fenced = False
    for line in path.read_text(encoding="utf-8").split("\n"):
        if FENCE.match(line):
            fenced = not fenced
            continue
        if fenced or DEFINITION.match(line):
            continue
        for span in TICK.findall(line):
            found[span.strip()] += 1
    return found


class Report:
    def __init__(self) -> None:
        self.failures: list[str] = []

    def fail(self, path: Path, message: str) -> None:
        self.failures.append(f"{path.relative_to(ROOT)}: {message}")


def check_names(pages: list[Path], schema: dict, report: Report) -> dict[Path, Counter]:
    phases = {p["name"] for p in schema["phases"]}
    phases |= {p["verifier"] for p in schema["phases"] if p.get("verifier")}
    symbols = declared_symbols()
    citations: dict[Path, Counter] = {}
    for page_path in pages:
        found = prose_citations(page_path)
        citations[page_path] = found
        for span in found:
            base = span.split(".")[0].split("(")[0].strip()
            if PHASE.match(span) and span not in phases:
                report.fail(page_path, f"`{span}` is not a phase in PhaseSchema.json")
            elif SYMBOL.match(base) and base not in symbols:
                report.fail(page_path, f"`{base}` is not declared in a public header")
    return citations


def locale_groups(pages: list[Path]) -> list[dict[str, Path]]:
    """One entry per guide, mapping locale to page; a page with no English
    original has nothing to be compared against and is dropped."""
    groups: dict[tuple[Path, str], dict[str, Path]] = defaultdict(dict)
    for page_path in pages:
        groups[(page_path.parent, stem_of(page_path.name))][
            locale_of(page_path.name)] = page_path
    return [found for _, found in sorted(groups.items()) if "" in found]


def check_locale_parity(
    pages: list[Path], citations: dict[Path, Counter], report: Report
) -> None:
    def technical(found: Counter) -> set[str]:
        names = set()
        for span in found:
            base = span.split(".")[0].split("(")[0].strip()
            if PHASE.match(span) or SYMBOL.match(base):
                names.add(span)
        return names

    for found in locale_groups(pages):
        english = technical(citations[found[""]])
        for locale, page_path in sorted(found.items()):
            if not locale:
                continue
            here = technical(citations[page_path])
            missing = sorted(english - here)
            extra = sorted(here - english)
            if missing or extra:
                report.fail(
                    page_path,
                    f"identifiers differ from English: missing={missing} "
                    f"unexpected={extra}",
                )


def check_statistics(schema: dict, report: Report) -> None:
    phases = schema["phases"]
    policy: Counter = Counter()
    for entry in phases:
        policy.update(entry["policy"])
    domains = Counter(entry["domain"] for entry in phases)
    sealed = sorted(
        e["name"] for e in phases if "SEALED_HOST_GATE" in e["policy"]
    )
    facts = {
        str(len(phases)): "phase total",
        str(policy["INTERCEPTABLE"]): "interceptable count",
        str(policy["REPLACEABLE"]): "replaceable count",
        str(policy["SKIPPABLE_WITH_PROOF"]): "skippable count",
        str(policy["SEALED_HOST_GATE"]): "sealed count",
        str(len(schema.get("extension_families", []))): "extension families",
    }

    for locale in LOCALES:
        path = page("README", locale)
        if not path.exists():
            continue
        text = normalize_digits(path.read_text(encoding="utf-8"))
        for value, meaning in facts.items():
            if not re.search(rf"(?<!\d){value}(?!\d)", text):
                report.fail(path, f"{meaning} ({value}) does not appear")
        for name in sealed:
            short = name[len("neverc."):]
            if short not in text and name not in text:
                report.fail(path, f"sealed gate {name} is not listed")
        # the per-domain table carries raw counts in every locale
        found = {m.group(1): int(m.group(2)) for m in COUNT_ROW.finditer(text)}
        for domain, expected in domains.items():
            if domain in found and found[domain] != expected:
                report.fail(
                    path,
                    f"domain {domain} is listed as {found[domain]}, "
                    f"schema has {expected}",
                )
            elif domain not in found:
                report.fail(path, f"domain {domain} is missing from the phase table")


def check_example_targets(report: Report) -> None:
    cmake = []
    for directory, _, names in os.walk(SDK):
        for name in sorted(names):
            if name == "CMakeLists.txt":
                cmake.append((Path(directory) / name).read_text(encoding="utf-8"))
    defined = "\n".join(cmake)
    readme = page("README", "")
    for target in sorted(set(EXAMPLE_TARGET.findall(
            readme.read_text(encoding="utf-8")))):
        if target not in defined:
            report.fail(readme, f"example target {target} is not defined by the SDK")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--quiet", action="store_true", help="only report failures")
    arguments = parser.parse_args()

    try:
        schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"check-docs-facts: cannot read phase schema: {error}", file=sys.stderr)
        return 1

    pages = guide_pages()
    if not pages:
        print("check-docs-facts: no documentation pages found", file=sys.stderr)
        return 1

    report = Report()
    citations = check_names(pages, schema, report)
    check_locale_parity(pages, citations, report)
    check_statistics(schema, report)
    check_example_targets(report)

    if report.failures:
        print("check-docs-facts: documentation disagrees with the source:",
              file=sys.stderr)
        for failure in report.failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    if not arguments.quiet:
        cited = len({s for found in citations.values() for s in found})
        print(
            f"check-docs-facts: {len(pages)} pages agree with "
            f"{len(schema['phases'])} schema phases and the public headers "
            f"({cited} distinct citations)"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

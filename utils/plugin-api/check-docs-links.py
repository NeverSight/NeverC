#!/usr/bin/env python3

"""Validate the navigation surface of the plugin API documentation.

Checks, without a build:

1. Every guide exists in all supported locales, and each page opens with a
   complete language bar whose entries resolve.
2. Every relative link resolves, and a translated page links to the same
   locale rather than dropping the reader into English.
3. Reference-link definitions are unique, used, and point at real files.
4. A citation of a repository file is a link, not inert prose, so the guides
   stay navigable as they grow.
5. All locales of one guide share the same reference-link index, which is what
   keeps a translation from silently losing a jump target.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DOCS = ROOT / "docs" / "plugin-api"
PLUGIN_INC = "neverc/include/neverc/Plugin"
SCHEMA_INC = f"{PLUGIN_INC}/Schema"
EXAMPLES = "pluginsdk/examples"

LOCALES = ["", "zh-CN", "zh-TW", "ja", "ko", "fr", "de", "es", "it", "ru", "ar"]

LANGUAGE_BAR = re.compile(
    r"^\*\*(?:Languages|语言|語言|言語|언어|Langues|Sprachen|Idiomas|Lingue"
    r"|Языки|اللغات)\*\*\s*:"
)
FENCE = re.compile(r"^\s*```")
TICK = re.compile(r"`([^`\n]+)`")
INLINE_LINK = re.compile(r"(?<!\!)\[([^\]\[]*(?:\[[^\]]*\][^\]\[]*)*)\]\(([^)]+)\)")
ANY_LINK = re.compile(
    r"(?<!\!)\[([^\]\[]*(?:\[[^\]]*\][^\]\[]*)*)\](\([^)]*\)|\[[^\]]*\])?"
)
DEFINITION = re.compile(r"^\[([^\]]+)\]:\s*(\S+)\s*$")
FILE_NAME = re.compile(r"^[\w./\-]+\.(?:c|h|cpp|py|json|inc|td|cmake|txt|nc|def|sh)$")


def citation_targets() -> dict[str, str]:
    """Map every citation spelling the guides use to a repository path."""
    targets: dict[str, str] = {}
    for name in os.listdir(ROOT / PLUGIN_INC):
        if name.endswith(".h"):
            targets[name] = f"{PLUGIN_INC}/{name}"
    for name in os.listdir(ROOT / SCHEMA_INC):
        if name.endswith((".json", ".inc")) and not name.endswith(".inc.in"):
            targets[name] = f"{SCHEMA_INC}/{name}"
            targets[f"Schema/{name}"] = f"{SCHEMA_INC}/{name}"
    for name in os.listdir(ROOT / EXAMPLES):
        if name.endswith(".c"):
            targets[name] = f"{EXAMPLES}/{name}"
    targets["coverage.json"] = "docs/plugin-api/coverage.json"
    return targets


TARGETS = citation_targets()


def resolve_citation(text: str) -> str | None:
    text = text.strip()
    if text in TARGETS:
        return TARGETS[text]
    relative = text.lstrip("./")
    if FILE_NAME.match(relative) and "/" in relative and (ROOT / relative).exists():
        return relative
    return None


def locale_of(name: str) -> str:
    parts = name[:-3].split(".")
    return parts[-1] if len(parts) >= 2 and parts[-1] in LOCALES else ""


def stem_of(name: str) -> str:
    parts = name[:-3].split(".")
    return ".".join(parts[:-1]) if len(parts) >= 2 and parts[-1] in LOCALES else name[:-3]


def page_name(stem: str, locale: str) -> str:
    return f"{stem}.md" if not locale else f"{stem}.{locale}.md"


def guide_pages() -> list[Path]:
    pages = []
    for directory, subdirectories, names in os.walk(DOCS):
        subdirectories[:] = [d for d in subdirectories if d != "__pycache__"]
        pages.extend(Path(directory) / n for n in sorted(names) if n.endswith(".md"))
    return sorted(pages)


def prose_lines(text: str) -> list[tuple[int, str]]:
    """Lines outside fenced code, paired with their 1-based number."""
    result = []
    fenced = False
    for number, line in enumerate(text.split("\n"), 1):
        if FENCE.match(line):
            fenced = not fenced
            continue
        if not fenced:
            result.append((number, line))
    return result


class Report:
    def __init__(self) -> None:
        self.failures: list[str] = []

    def fail(self, page: Path, message: str) -> None:
        self.failures.append(f"{page.relative_to(ROOT)}: {message}")


def check_locale_coverage(pages: list[Path], report: Report) -> None:
    groups: dict[tuple[Path, str], dict[str, Path]] = defaultdict(dict)
    for page in pages:
        groups[(page.parent, stem_of(page.name))][locale_of(page.name)] = page
    for (directory, stem), found in sorted(groups.items()):
        missing = [locale for locale in LOCALES if locale not in found]
        if missing:
            report.fail(
                directory / f"{stem}.md",
                "missing locales: " + ", ".join(m or "en" for m in missing),
            )


def check_language_bar(page: Path, text: str, report: Report) -> None:
    bar = text.split("\n", 1)[0]
    if not LANGUAGE_BAR.match(bar):
        report.fail(page, "first line is not a language bar")
        return
    entries = {href for _, href in INLINE_LINK.findall(bar)}
    stem = stem_of(page.name)
    for locale in LOCALES:
        expected = page_name(stem, locale)
        if expected not in entries:
            report.fail(page, f"language bar has no entry for {expected}")
        elif not (page.parent / expected).exists():
            report.fail(page, f"language bar points at missing {expected}")


def check_links(page: Path, text: str, report: Report) -> None:
    locale = locale_of(page.name)
    body = "\n".join(line for _, line in prose_lines(text)[1:])
    for label, href in INLINE_LINK.findall(body):
        href = href.strip()
        if href.startswith(("http://", "https://", "mailto:", "#")):
            continue
        target = (page.parent / href.split("#")[0]).resolve()
        if not target.exists():
            report.fail(page, f"[{label}]({href}) does not resolve")
            continue
        if not locale or target.suffix != ".md" or target.name == page.name:
            continue
        sibling = target.parent / page_name(stem_of(target.name), locale)
        if locale_of(target.name) != locale and sibling.exists():
            report.fail(
                page,
                f"[{label}]({href}) leaves the {locale} locale; "
                f"{sibling.name} exists",
            )


def check_definitions(page: Path, text: str, report: Report) -> dict[str, str]:
    definitions: dict[str, str] = {}
    for number, line in prose_lines(text):
        match = DEFINITION.match(line)
        if not match:
            continue
        label, url = match.group(1), match.group(2)
        if label in definitions:
            report.fail(page, f"line {number}: duplicate definition for [{label}]")
        definitions[label] = url
        if not (page.parent / url.split("#")[0]).exists():
            report.fail(page, f"line {number}: [{label}] points at missing {url}")

    used = set()
    for number, line in prose_lines(text):
        if DEFINITION.match(line):
            continue
        for match in ANY_LINK.finditer(line):
            suffix = match.group(2)
            if suffix and suffix.startswith("("):
                continue
            label = suffix[1:-1] if suffix and len(suffix) > 2 else match.group(1)
            if label in definitions:
                used.add(label)
            elif f"`{match.group(1)}`" == match.group(1) or TICK.fullmatch(match.group(1)):
                report.fail(page, f"line {number}: [{match.group(1)}] has no definition")
    for label in definitions.keys() - used:
        report.fail(page, f"[{label}] is defined but never used")
    return definitions


def check_citations_are_links(page: Path, text: str, report: Report) -> None:
    for number, line in prose_lines(text):
        if DEFINITION.match(line):
            continue
        linked = [(m.start(), m.end()) for m in ANY_LINK.finditer(line)]
        for match in TICK.finditer(line):
            citation = match.group(1).strip()
            if not resolve_citation(citation):
                continue
            if any(start <= match.start() < end for start, end in linked):
                continue
            report.fail(
                page, f"line {number}: `{citation}` cites a repository file "
                "but is not a link"
            )


def check_index_parity(
    pages: list[Path], indexes: dict[Path, dict[str, str]], report: Report
) -> None:
    groups: dict[tuple[Path, str], dict[str, Path]] = defaultdict(dict)
    for page in pages:
        groups[(page.parent, stem_of(page.name))][locale_of(page.name)] = page
    for (_, _), found in sorted(groups.items()):
        if "" not in found:
            continue
        english = set(indexes.get(found[""], {}))
        for locale, page in sorted(found.items()):
            if not locale:
                continue
            here = set(indexes.get(page, {}))
            missing = sorted(english - here)
            extra = sorted(here - english)
            if missing or extra:
                report.fail(
                    page,
                    f"reference index differs from English: "
                    f"missing={missing} unexpected={extra}",
                )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--quiet", action="store_true", help="only report failures"
    )
    arguments = parser.parse_args()

    pages = guide_pages()
    if not pages:
        print("check-docs-links: no documentation pages found", file=sys.stderr)
        return 1

    report = Report()
    indexes: dict[Path, dict[str, str]] = {}
    check_locale_coverage(pages, report)
    for page in pages:
        text = page.read_text(encoding="utf-8")
        check_language_bar(page, text, report)
        check_links(page, text, report)
        indexes[page] = check_definitions(page, text, report)
        check_citations_are_links(page, text, report)
    check_index_parity(pages, indexes, report)

    if report.failures:
        print("check-docs-links: documentation navigation problems:", file=sys.stderr)
        for failure in report.failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    if not arguments.quiet:
        links = sum(len(index) for index in indexes.values())
        print(
            f"check-docs-links: {len(pages)} pages across {len(LOCALES)} locales "
            f"are navigable, {links} reference definitions resolve"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

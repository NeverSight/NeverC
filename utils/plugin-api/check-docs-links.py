#!/usr/bin/env python3

"""Validate the navigation surface of docs/, in every locale.

Checks, without a build:

1. Every guide exists in all supported locales, and each page opens with a
   complete language bar whose entries resolve. A right-to-left page also
   carries the wrapper without which it renders left-to-right.
2. Every relative link resolves, and a translated page links to the same
   locale rather than dropping the reader into English.
3. Reference-link definitions are unique, used, and point at real files.
4. A citation of a repository file is a link, not inert prose, so the guides
   stay navigable as they grow.
5. All locales of one guide share the same reference-link index, which is what
   keeps a translation from silently losing a jump target.
6. Every translation renders the whole English page, or is named in
   docs-translation-debt.json. That list only shrinks, so an unfinished
   translation stays visible instead of passing as a finished one.
7. The docs index reaches every guide in its own locale, so a new guide cannot
   end up reachable only by knowing its filename.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEBT = Path(__file__).resolve().parent / "docs-translation-debt.json"
DOCS = ROOT / "docs"
# every guide is reached from this index; it is not itself a guide
INDEX = DOCS
# docs/i18n translates the repository README, so its original sits outside
# DOCS and the group has no English page of its own.
FOREIGN_ORIGINAL = {DOCS / "i18n": ROOT / "README.md"}
PLUGIN_INC = "neverc/include/neverc/Plugin"
SCHEMA_INC = f"{PLUGIN_INC}/Schema"
EXAMPLES = "pluginsdk/examples"

LOCALES = ["", "zh-CN", "zh-TW", "ja", "ko", "fr", "de", "es", "it", "ru", "ar"]
# a right-to-left page wraps its whole body, so the language bar is not line 1
RTL_LOCALES = {"ar"}
RTL_OPEN = '<div dir="rtl">'
RTL_CLOSE = "</div>"

LANGUAGE_BAR = re.compile(
    r"^\*\*(?:Languages|语言|語言|言語|언어|Langues|Sprachen|Idiomas|Lingue"
    r"|Языки|اللغات)\*\*\s*:"
)
FENCE = re.compile(r"^\s*```")
HEADING = re.compile(r"^(#{1,6})\s")
TABLE_ROW = re.compile(r"^\s*\|")
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


def bar_entry(directory: Path, stem: str, locale: str) -> str:
    """Where a page's language bar has to point for one locale."""
    original = FOREIGN_ORIGINAL.get(directory)
    if original is not None and not locale:
        return os.path.relpath(original, directory).replace(os.sep, "/")
    return page_name(stem, locale)


def language_bar(text: str) -> tuple[int, str]:
    """The bar and its 1-based line, looking past an RTL wrapper."""
    for number, line in enumerate(text.split("\n")[:3], 1):
        if LANGUAGE_BAR.match(line):
            return number, line
    return 0, ""


def guide_pages() -> list[Path]:
    pages = []
    for directory, subdirectories, names in os.walk(DOCS):
        subdirectories[:] = [d for d in subdirectories if d != "__pycache__"]
        pages.extend(Path(directory) / n for n in sorted(names) if n.endswith(".md"))
    return sorted(pages)


def locale_groups(pages: list[Path]) -> list[dict[str, Path]]:
    """One entry per guide, mapping locale to page. A group with no English
    page has no original to be compared against and is dropped."""
    groups: dict[tuple[Path, str], dict[str, Path]] = defaultdict(dict)
    for page in pages:
        groups[(page.parent, stem_of(page.name))][locale_of(page.name)] = page
    return [found for _, found in sorted(groups.items()) if "" in found]


def skeleton(text: str) -> tuple[tuple[int, ...], int, int]:
    """The shape a translation has to keep, independent of its words."""
    headings: list[int] = []
    blocks = rows = 0
    fenced = False
    for line in text.split("\n"):
        if FENCE.match(line):
            fenced = not fenced
            blocks += fenced
            continue
        if fenced:
            continue
        heading = HEADING.match(line)
        if heading:
            headings.append(len(heading.group(1)))
        elif TABLE_ROW.match(line):
            rows += 1
    return tuple(headings), blocks, rows


def translation_debt() -> set[str]:
    """Pages already known to render less than their English original."""
    listed = json.loads(DEBT.read_text(encoding="utf-8"))["incomplete"]
    return {str(entry) for entry in listed}


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
        wanted = [
            locale for locale in LOCALES
            if locale or directory not in FOREIGN_ORIGINAL
        ]
        missing = [locale for locale in wanted if locale not in found]
        if missing:
            report.fail(
                directory / f"{stem}.md",
                "missing locales: " + ", ".join(m or "en" for m in missing),
            )
    for directory, original in FOREIGN_ORIGINAL.items():
        if not original.exists():
            report.fail(directory, f"translates the missing {original}")


def check_language_bar(page: Path, text: str, report: Report) -> None:
    locale = locale_of(page.name)
    lines = text.split("\n")
    if locale in RTL_LOCALES:
        # without the wrapper a right-to-left page renders left-to-right
        if lines[0].strip() != RTL_OPEN:
            report.fail(page, f"a {locale} page must open with {RTL_OPEN}")
        elif RTL_CLOSE not in (line.strip() for line in lines):
            report.fail(page, f"the {RTL_OPEN} wrapper is never closed")
    at, bar = language_bar(text)
    if not at:
        report.fail(page, "the opening lines carry no language bar")
        return
    entries = {href for _, href in INLINE_LINK.findall(bar)}
    stem = stem_of(page.name)
    for other in LOCALES:
        expected = bar_entry(page.parent, stem, other)
        if expected not in entries:
            report.fail(page, f"language bar has no entry for {expected}")
        elif not (page.parent / expected).exists():
            report.fail(page, f"language bar points at missing {expected}")


def check_links(
    page: Path, text: str, report: Report, debt: set[str]
) -> None:
    locale = locale_of(page.name)
    short = page.relative_to(ROOT).as_posix() in debt
    at, _ = language_bar(text)
    body = "\n".join(
        line for number, line in prose_lines(text) if number != at)
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
            # an unfinished translation has to cite sections it does not
            # carry yet, and only the English page has those anchors
            if short and "#" in href:
                continue
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


def check_translation_shape(
    pages: list[Path], report: Report, debt: set[str]
) -> None:
    """A translation renders the whole English page, or says so in the debt
    list. Holding the list to exactly the short pages is what stops the debt
    from growing quietly and keeps a finished translation from staying on it."""
    for found in locale_groups(pages):
        english = skeleton(found[""].read_text(encoding="utf-8"))
        for locale, page in sorted(found.items()):
            if not locale:
                continue
            listed = page.relative_to(ROOT).as_posix() in debt
            if skeleton(page.read_text(encoding="utf-8")) == english:
                if listed:
                    report.fail(
                        page, f"matches English now; drop it from {DEBT.name}")
            elif not listed:
                report.fail(
                    page,
                    f"renders less than {found[''].name}; finish it or list "
                    f"it in {DEBT.name}",
                )


def check_index_parity(
    pages: list[Path], indexes: dict[Path, dict[str, str]], report: Report,
    debt: set[str]
) -> None:
    for found in locale_groups(pages):
        english = set(indexes.get(found[""], {}))
        for locale, page in sorted(found.items()):
            # a page that renders less than the English one cites less of the
            # repository too; check_translation_shape already reports it
            if not locale or page.relative_to(ROOT).as_posix() in debt:
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


def check_parent_index(pages: list[Path], report: Report) -> None:
    """The index above the guides must reach each of them, in its own locale."""
    guides = sorted(
        {(page.parent, stem_of(page.name)) for page in pages
         if not locale_of(page.name) and page.parent != INDEX}
    )
    for locale in LOCALES:
        index = INDEX / page_name("README", locale)
        if not index.exists():
            report.fail(index, "the guides have a locale the index does not")
            continue
        linked = {
            href.split("#")[0]
            for _, href in INLINE_LINK.findall(index.read_text(encoding="utf-8"))
        }

        def entry(directory: Path, stem: str, of_locale: str) -> str:
            return (directory / page_name(stem, of_locale)).relative_to(
                INDEX).as_posix()

        expected = {entry(d, s, locale) for d, s in guides}
        for href in sorted(expected - linked):
            report.fail(index, f"does not link {href}")
        translated = {
            entry(d, s, other) for d, s in guides for other in LOCALES
        }
        for href in sorted((linked & translated) - expected):
            report.fail(
                index, f"links {href} rather than its {locale or 'en'} page")


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

    try:
        debt = translation_debt()
    except (OSError, KeyError, ValueError) as error:
        print(f"check-docs-links: cannot read {DEBT.name}: {error}",
              file=sys.stderr)
        return 1

    report = Report()
    indexes: dict[Path, dict[str, str]] = {}
    check_locale_coverage(pages, report)
    for page in pages:
        text = page.read_text(encoding="utf-8")
        check_language_bar(page, text, report)
        check_links(page, text, report, debt)
        indexes[page] = check_definitions(page, text, report)
        check_citations_are_links(page, text, report)
    check_translation_shape(pages, report, debt)
    check_index_parity(pages, indexes, report, debt)
    check_parent_index(pages, report)

    if report.failures:
        print("check-docs-links: documentation navigation problems:", file=sys.stderr)
        for failure in report.failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    if not arguments.quiet:
        links = sum(len(index) for index in indexes.values())
        print(
            f"check-docs-links: {len(pages)} pages across {len(LOCALES)} locales "
            f"are navigable, {links} reference definitions resolve, "
            f"{len(debt)} translations are unfinished"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

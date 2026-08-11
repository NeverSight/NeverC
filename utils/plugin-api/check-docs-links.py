#!/usr/bin/env python3

"""Validate the navigation surface of the guides, in every locale.

A guide lives under docs/ or examples/ and carries a full set of translations.
The English-only pages beside them -- the repository README, SECURITY.md,
development.md, pluginsdk/README.md -- are read too: a reader follows links out
of those just the same, and the repository README is where every translation in
docs/i18n starts from.

Checks, without a build:

1. Every guide exists in all supported locales, and each page opens with a
   complete language bar whose entries resolve. A right-to-left page also
   carries the wrapper without which it renders left-to-right.
2. Every relative link resolves, and a translated page links to the same
   locale rather than dropping the reader into English.
3. Every ``#fragment`` a link carries names a heading the target page really
   has. A heading is translated, so its slug moves with it; without this a
   cross-page jump keeps resolving as a file and lands the reader at the top.
4. Reference-link definitions are unique, used, and owe a reader everything
   an inline link does: they resolve, the ``#fragment`` they carry names a
   heading the target really has, and they stay in their own locale.
5. A citation of a repository file is a link, not inert prose, so the guides
   stay navigable as they grow.
6. All locales of one guide share the same reference-link index, which is what
   keeps a translation from silently losing a jump target.
7. Every translation renders the whole English page, or is named in
   docs-translation-debt.json. That list only shrinks, so an unfinished
   translation stays visible instead of passing as a finished one.
8. Each index reaches every guide of its tree in its own locale, so a new guide
   cannot end up reachable only by knowing its filename. Every README indexes
   its own subtree, nested ones included, and the guides beside it as well, so
   a section index cannot quietly stop naming a guide while the top-level
   index still reaches it and hides the loss. docs/examples indexes a tree
   that lives outside docs/, so that one pairing stays written down rather
   than discovered.
9. Every locale of a guide makes the same jumps as the English page, down to
   which section of the target each one lands on. Check 6 covers only the
   reference-link index; an inline jump can go missing from one translation
   without any other check noticing. A section-level jump degrading into a
   whole-page one, or landing on a different section than the English page
   reaches, passes checks 2 and 3 -- the file resolves and the anchor exists,
   it is just the wrong one -- and is exactly the kind of loss nobody reads a
   diff closely enough to catch. A definition and a link out of the
   repository are counted here too: check 6 reads which labels a page
   defines but never where they point, and nothing else reads an external
   link at all, so either could be dropped or repointed in one locale while
   every other check stayed satisfied.
10. Every guide names its way back up. Check 2 holds a breadcrumb that is
    there to resolving and to staying in its locale, but a trail dropped from
    every locale at once leaves nothing behind to catch: the page is still
    reachable from the index above it, and only a reader who arrived any other
    way is stranded. The pages translating the repository README are exempt --
    they translate the top of the tree, which has nothing above it.
11. Everything the checks read also starts them. The workflow's path filter
    decides whether any of this runs, so a file the filter does not name is
    one whose rename or removal breaks a page with nothing left to report it.
    Every check above is only as good as the filter that reaches it.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import unicodedata
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEBT = Path(__file__).resolve().parent / "docs-translation-debt.json"
DOCS = ROOT / "docs"
SAMPLES = ROOT / "examples"
# every page in one of these trees is a guide: it carries a language bar and a
# full set of translations. examples/ is one of them even though its index
# lives in docs/, which is why a tree and its index are named separately.
TRANSLATED_TREES = [DOCS, SAMPLES]
# Agent implementation plans are repository-local working artifacts rather
# than shipped user guides. They deliberately have no translation/navigation
# surface and must not make a public-doc localization check absorb them.
IGNORED_GUIDE_TREES = {DOCS / "superpowers"}
# a README indexes the subtree it sits on top of, so which tree an index
# reaches is discovered rather than listed. This one reaches a tree that lives
# somewhere else entirely, which no amount of looking around it would reveal.
INDEXED_ELSEWHERE = {DOCS / "examples": SAMPLES}
# pages that ship in English only. They have no language bar and no
# translations, but a reader still follows their links, so those are checked.
STANDALONE = ["SECURITY.md", "development.md", "pluginsdk/README.md"]
# docs/i18n translates the repository README, so its original sits outside
# DOCS and the group has no English page of its own.
FOREIGN_ORIGINAL = {DOCS / "i18n": ROOT / "README.md"}
# the same lookup backwards: which directory translates a given original
TRANSLATED_BY = {
    original.resolve(): directory
    for directory, original in FOREIGN_ORIGINAL.items()
}
PLUGIN_INC = "neverc/include/neverc/Plugin"
SCHEMA_INC = f"{PLUGIN_INC}/Schema"
EXAMPLES = "pluginsdk/examples"
# the workflow that runs these checks. GitHub reads its path filter before
# anything here does, so the filter cannot be discovered at run time and is
# held to the tree instead.
WORKFLOW = ROOT / ".github/workflows/lint-docs.yml"
# files no page links to, but that a check reads anyway. The filter has to
# name them too: editing one changes what the checks conclude, and a commit
# that touches nothing else would never start them.
COMPANIONS = [
    Path(__file__).resolve(),
    Path(__file__).resolve().parent / "check-docs-facts.py",
    DEBT,
    ROOT / SCHEMA_INC / "PhaseSchema.json",
    ROOT / "neverc/include/neverc/Linker/Core/Driver/Dispatcher.h",
    ROOT / "neverc/include/neverc/Foundation/AndroidKernelModuleSymbolPolicy.h",
    ROOT / "neverc/lib/Foundation/Core/AndroidKernelModuleReleaseNames.cpp",
    ROOT / "neverc/lib/Invoke/Core/Driver.cpp",
    ROOT / "neverc/lib/Invoke/ToolChains/CommonArgs.cpp",
    ROOT / "neverc/lib/Invoke/ToolChains/NeverC.cpp",
    ROOT / "neverc/lib/Plugin/Link/AndroidKernelReleaseIdentitySeal.cpp",
    ROOT / "neverc/lib/Plugin/Link/BuiltinObjectMergeAdapter.cpp",
    ROOT / "neverc/lib/Plugin/Link/LinkExecutionHooksBridge.cpp",
    ROOT / "neverc/lib/Plugin/Core/PluginPhaseExecutor.cpp",
    ROOT / "neverc/lib/Plugin/Object/ObjectPhaseHooks.cpp",
    ROOT / "neverc/lib/Plugin/Object/ObjectWriterProvider.cpp",
    *sorted((ROOT / "examples").glob("android-kernel-*/Makefile")),
    WORKFLOW,
]

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
HEADING_TEXT = re.compile(r"^(#{1,6})\s+(.*?)\s*$")
ANCHOR_TAG = re.compile(r'<a\s+(?:id|name)="([^"]+)"')
# how every page in the tree spells the way back up
BREADCRUMB = re.compile(r"[←→]")


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


def bar_entry(page: Path, locale: str) -> str:
    """Where a page's language bar has to point for one locale. Usually a
    sibling, but the repository README and its docs/i18n translations live in
    different directories and have to reach across to each other."""
    directory, stem = page.parent, stem_of(page.name)
    original = FOREIGN_ORIGINAL.get(directory)
    if original is not None and not locale:
        return os.path.relpath(original, directory).replace(os.sep, "/")
    translated = TRANSLATED_BY.get(page.resolve())
    if translated is not None and locale:
        return os.path.relpath(
            translated / page_name(stem, locale), directory).replace(os.sep, "/")
    return page_name(stem, locale)


def language_bar(text: str) -> tuple[int, str]:
    """The bar and its 1-based line, looking past an RTL wrapper."""
    for number, line in enumerate(text.split("\n")[:3], 1):
        if LANGUAGE_BAR.match(line):
            return number, line
    return 0, ""


def guide_pages() -> list[Path]:
    pages = []
    for tree in TRANSLATED_TREES:
        for directory, subdirectories, names in os.walk(tree):
            subdirectories[:] = [
                d
                for d in subdirectories
                if d != "__pycache__"
                and Path(directory, d) not in IGNORED_GUIDE_TREES
            ]
            pages.extend(
                Path(directory) / n for n in sorted(names) if n.endswith(".md"))
    return sorted(pages)


def indexed_trees() -> list[tuple[Path, Path]]:
    """Every index, paired with the tree of guides it has to reach.

    A README is an index over the subtree it sits on top of, so a nested one
    is found rather than named here: listing them is what let a section index
    stop naming a guide while the index above it still reached the guide and
    kept the loss out of sight."""
    found = []
    for tree in TRANSLATED_TREES:
        for directory, subdirectories, names in os.walk(tree):
            subdirectories[:] = [
                d
                for d in subdirectories
                if d != "__pycache__"
                and Path(directory, d) not in IGNORED_GUIDE_TREES
            ]
            if "README.md" in names:
                here = Path(directory)
                found.append((here, INDEXED_ELSEWHERE.get(here, here)))
    return sorted(found)


def standalone_pages() -> list[Path]:
    """English-only pages, plus the repository README, whose translations sit
    in docs/i18n rather than beside it. None of them is a guide, so none is
    held to having a locale of its own, but all of them are read."""
    found = [ROOT / name for name in STANDALONE] + list(FOREIGN_ORIGINAL.values())
    return sorted({p.resolve() for p in found if p.exists()})


def locale_groups(pages: list[Path]) -> list[dict[str, Path]]:
    """One entry per guide, mapping locale to page. docs/i18n translates a
    page that sits outside the translated trees, so its original is filled in
    from FOREIGN_ORIGINAL; without it the whole group would be dropped here
    and silently escape every check that compares against an original."""
    groups: dict[tuple[Path, str], dict[str, Path]] = defaultdict(dict)
    for page in pages:
        groups[(page.parent, stem_of(page.name))][locale_of(page.name)] = page
    for (directory, _), found in groups.items():
        original = FOREIGN_ORIGINAL.get(directory)
        if original is not None and "" not in found:
            found[""] = original.resolve()
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


def body(text: str) -> str:
    """The page without its code blocks or its language bar. The bar names
    every locale by design, so leaving it in would drown out the jumps the
    page actually makes."""
    at, _ = language_bar(text)
    return "\n".join(
        line for number, line in prose_lines(text) if number != at)


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
    for other in LOCALES:
        expected = bar_entry(page, other)
        if expected not in entries:
            report.fail(page, f"language bar has no entry for {expected}")
        elif not (page.parent / expected).exists():
            report.fail(page, f"language bar points at missing {expected}")


def check_breadcrumb(page: Path, text: str, report: Report) -> None:
    """A page opens with the way back to the index above it.

    check_links already holds a breadcrumb that is there to resolving and to
    staying in its locale. What it cannot see is the trail going missing from
    every locale at once: nothing stops resolving, the index above still
    reaches the page, and only a reader who arrived from anywhere else --
    a search result, a link in a review -- is left with no way out."""
    at, _ = language_bar(text)
    for number, line in prose_lines(text):
        if number == at:
            continue
        if HEADING.match(line):
            break
        if BREADCRUMB.search(line) and INLINE_LINK.search(line):
            return
    report.fail(page, "opens with no breadcrumb back to the index above it")


_ANCHORS: dict[Path, dict[str, int | str]] = {}


def slug(text: str) -> str:
    """GitHub's heading anchor: drop inline formatting, lowercase, keep
    letters, numbers and marks, and turn spaces into hyphens."""
    text = re.sub(r"!\[([^\]]*)\]\([^)]*\)", r"\1", text)
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)
    text = re.sub(r"[`*_~]", "", text).strip().lower()
    kept = [
        ch
        for ch in text
        if ch.isalnum()
        or ch in "-_ "
        or unicodedata.category(ch).startswith(("L", "N", "M"))
    ]
    return "".join(kept).replace(" ", "-")


def page_anchors(text: str) -> dict[str, int | str]:
    """Every #fragment the page offers: one slug per heading, disambiguated
    the way GitHub does, plus any explicit <a id>/<a name>.

    Each is mapped to whatever names the same place in every locale. A
    heading is named by its position, because its words are translated and
    its slug moves with them; an explicit anchor is named by itself, because
    it is hand-written and does not move."""
    found: dict[str, int | str] = {}
    seen: dict[str, int] = defaultdict(int)
    fenced = False
    position = 0
    for line in text.split("\n"):
        if FENCE.match(line):
            fenced = not fenced
            continue
        if fenced:
            continue
        for anchor in ANCHOR_TAG.findall(line):
            found.setdefault(anchor, anchor)
        heading = HEADING_TEXT.match(line)
        if not heading:
            continue
        base = slug(heading.group(2))
        count = seen[base]
        seen[base] += 1
        found.setdefault(base if not count else f"{base}-{count}", position)
        position += 1
    return found


def anchors_of(page: Path) -> dict[str, int | str]:
    cached = _ANCHORS.get(page)
    if cached is None:
        cached = page_anchors(page.read_text(encoding="utf-8"))
        _ANCHORS[page] = cached
    return cached


def check_fragment(
    page: Path, target: Path, cite: str, fragment: str,
    report: Report, debt: set[str], prefix: str = "",
) -> None:
    """A ``#fragment`` names a heading the target page really has. A
    translation still short of its English original is skipped on either
    end: it cannot carry the anchor for a section it has not rendered yet."""
    if not fragment or target.suffix != ".md":
        return
    for end in (page, target):
        if end.relative_to(ROOT).as_posix() in debt:
            return
    if fragment not in anchors_of(target):
        report.fail(page, f"{prefix}{cite} names no anchor #{fragment}")


def check_target(
    page: Path, cite: str, href: str, report: Report, debt: set[str],
    prefix: str = "",
) -> None:
    """One jump, held to resolving, to naming an anchor its target really
    has, and to staying in its own locale.

    An inline link and a reference definition are the same jump written two
    ways, so both arrive here. Holding them apart is what let a definition
    owe the reader less than the link beside it: pointing at a file that
    exists was the whole of it, so a definition naming a section that is not
    there, or dropping a translated page's reader into English, was a jump
    nothing was watching."""
    if href.startswith(("http://", "https://", "mailto:")):
        return
    locale = locale_of(page.name)
    file_part, _, fragment = href.partition("#")
    target = page if not file_part else (page.parent / file_part).resolve()
    if not target.exists():
        report.fail(page, f"{prefix}{cite} does not resolve")
        return
    check_fragment(page, target, cite, fragment, report, debt, prefix)
    if not file_part:
        # a same-page jump stays in its own locale by construction
        return
    if not locale or target.suffix != ".md" or target.name == page.name:
        return
    sibling = target.parent / page_name(stem_of(target.name), locale)
    if locale_of(target.name) != locale and sibling.exists():
        # an unfinished translation has to cite sections it does not
        # carry yet, and only the English page has those anchors
        if fragment and page.relative_to(ROOT).as_posix() in debt:
            return
        report.fail(
            page,
            f"{prefix}{cite} leaves the {locale} locale; {sibling.name} exists",
        )


def check_links(
    page: Path, text: str, report: Report, debt: set[str]
) -> None:
    for label, href in INLINE_LINK.findall(body(text)):
        href = href.strip()
        check_target(page, f"[{label}]({href})", href, report, debt)


def check_definitions(
    page: Path, text: str, report: Report, debt: set[str]
) -> dict[str, str]:
    definitions: dict[str, str] = {}
    for number, line in prose_lines(text):
        match = DEFINITION.match(line)
        if not match:
            continue
        label, url = match.group(1), match.group(2)
        if label in definitions:
            report.fail(page, f"line {number}: duplicate definition for [{label}]")
        definitions[label] = url
        check_target(
            page, f"[{label}]: {url}", url, report, debt, f"line {number}: ")

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


def normalized_target(target: Path) -> str | None:
    """Name a jump so the same jump reads the same in every locale: drop the
    locale suffix, and speak of a translated original through the directory
    that translates it."""
    directory = TRANSLATED_BY.get(target)
    if directory is not None:
        target = directory / target.name
    if target.suffix == ".md":
        target = target.parent / page_name(stem_of(target.name), "")
    try:
        return target.relative_to(ROOT).as_posix()
    except ValueError:
        # a jump out of the repository has no locale to keep
        return None


# a section jump into a target that is unfinished in this locale. The anchor
# cannot exist there yet, so the jump is held only to still being a section
# jump rather than to landing anywhere in particular.
UNPLACED = "an unfinished translation"


def unfinished(target: Path, locale: str, debt: set[str]) -> bool:
    """Does this target still render less than its English original once
    read in ``locale``?"""
    if target.suffix != ".md":
        return False
    counterpart = target.parent / page_name(stem_of(target.name), locale)
    return counterpart.relative_to(ROOT).as_posix() in debt


def landing(
    target: Path, fragment: str, locale: str, debt: set[str]
) -> int | str | None:
    """Where in the target a jump lands, named so that the same landing
    reads the same in every locale."""
    if not fragment:
        return None
    if unfinished(target, locale, debt):
        return UNPLACED
    # check_links reports a fragment that names nothing; treating it as
    # unplaced here keeps that one failure from being reported twice
    return anchors_of(target).get(fragment, UNPLACED)


def describe(where: int | str | None) -> str:
    if where is None:
        return "as a whole page"
    if where == UNPLACED:
        return "at a section"
    if isinstance(where, int):
        return f"at section {where + 1}"
    return f"at #{where}"


def page_hrefs(text: str) -> list[str]:
    """Every href the page offers a reader, written either way round.

    A reference definition is a jump as much as an inline link is, and it is
    the one no other check reads for where it points: check_index_parity
    compares which labels a page defines and stops there, so a definition
    repointed in one locale alone changes where that locale's readers land
    with the two indexes still matching."""
    hrefs = [href for _, href in INLINE_LINK.findall(body(text))]
    for _, line in prose_lines(text):
        definition = DEFINITION.match(line)
        if definition:
            hrefs.append(definition.group(2))
    return [href.strip() for href in hrefs]


def jumps(
    page: Path, text: str, locale: str, debt: set[str]
) -> Counter[tuple[str, int | str | None]]:
    """Every jump the page makes, and where in the target it lands. Counted
    rather than collected: a page jumps to one guide from several places, and
    a set would hide all but the last of them going missing.

    A jump that leaves the repository is counted too. Nothing else reads one
    -- there is no file to resolve and no anchor to look up -- so a locale
    could drop a citation, or send it somewhere else entirely, and read as
    complete."""
    found: Counter[tuple[str, int | str | None]] = Counter()
    for href in page_hrefs(text):
        if href.startswith(("http://", "https://", "mailto:")):
            outside, _, fragment = href.partition("#")
            found[(outside, fragment or None)] += 1
            continue
        file_part, _, fragment = href.partition("#")
        target = page if not file_part else (page.parent / file_part).resolve()
        if not target.exists():
            # check_target already reports this one
            continue
        name = normalized_target(target)
        if name is not None:
            found[(name, landing(target, fragment, locale, debt))] += 1
    return found


def check_jump_parity(
    pages: list[Path], report: Report, debt: set[str]
) -> None:
    """A translation makes the same jumps as its English original, down to
    which section of the target each one lands on. check_index_parity covers
    only the reference-link index, so an inline jump can go missing from one
    locale with nothing else noticing; a section-level jump degrading into a
    whole-page one, or coming to rest on a different section than the English
    page reaches, leaves every other check satisfied.

    The English page is measured once per locale rather than once overall,
    because a jump into a translation that is still unfinished has no anchor
    to land on. Measuring both sides the same way is what keeps that from
    reading as a difference between them."""
    for found in locale_groups(pages):
        original = found[""]
        for locale, page in sorted(found.items()):
            # a page that renders less than the English one jumps less too;
            # check_translation_shape already reports it
            if not locale or page.relative_to(ROOT).as_posix() in debt:
                continue
            english = jumps(
                original, original.read_text(encoding="utf-8"), locale, debt)
            here = jumps(page, page.read_text(encoding="utf-8"), locale, debt)
            # a landing is an int, a string, or None, so it needs a key to
            # be ordered at all
            order = lambda item: (item[0][0], str(item[0][1]))  # noqa: E731
            for (name, where), count in sorted((english - here).items(), key=order):
                report.fail(
                    page,
                    f"makes {count} fewer jump(s) to {name} {describe(where)} "
                    f"than {original.name}",
                )
            for (name, where), count in sorted((here - english).items(), key=order):
                report.fail(
                    page,
                    f"makes {count} more jump(s) to {name} {describe(where)} "
                    f"than {original.name}",
                )


def within(page: Path, tree: Path) -> bool:
    return tree == page.parent or tree in page.parents


def check_parent_index(pages: list[Path], report: Report) -> None:
    """An index must reach every guide of its tree, in its own locale. Every
    README is one, nested ones included; a directory with no guides under it
    is a page that happens to be called README, not an index over anything.

    The only page an index owes nothing to is itself. A guide sitting in the
    index's own directory is the easiest one to lose -- it has no directory
    of its own to make its absence noticeable -- so it is held to the same
    rule as one a level down."""
    for index_directory, tree in indexed_trees():
        itself = index_directory / "README.md"
        guides = sorted(
            {(page.parent, stem_of(page.name)) for page in pages
             if not locale_of(page.name) and page != itself
             and within(page, tree)}
        )
        if not guides:
            continue

        def entry(directory: Path, stem: str, of_locale: str) -> str:
            return os.path.relpath(
                directory / page_name(stem, of_locale),
                index_directory,
            ).replace(os.sep, "/")

        for locale in LOCALES:
            index = index_directory / page_name("README", locale)
            if not index.exists():
                report.fail(index, "the guides have a locale the index does not")
                continue
            linked = {
                href.split("#")[0]
                for _, href in INLINE_LINK.findall(
                    index.read_text(encoding="utf-8"))
            }
            expected = {entry(d, s, locale) for d, s in guides}
            for href in sorted(expected - linked):
                report.fail(index, f"does not link {href}")
            translated = {
                entry(d, s, other) for d, s in guides for other in LOCALES
            }
            for href in sorted((linked & translated) - expected):
                report.fail(
                    index, f"links {href} rather than its {locale or 'en'} page")


def workflow_filters() -> list[list[str]]:
    """Every ``paths:`` list the workflow declares, in order.

    Scanned line by line rather than parsed: these checks run on a bare
    Python, and one list of globs does not earn a YAML dependency."""
    blocks: list[list[str]] = []
    current: list[str] | None = None
    depth = 0
    for line in WORKFLOW.read_text(encoding="utf-8").split("\n"):
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        here = len(line) - len(line.lstrip())
        if stripped == "paths:":
            current = []
            blocks.append(current)
            depth = here
            continue
        if current is None:
            continue
        if here > depth and stripped.startswith("- "):
            current.append(stripped[2:].strip().strip("'\""))
        else:
            current = None
    return blocks


def trigger_matches(pattern: str, path: str) -> bool:
    """GitHub's path filter glob: ``**`` crosses directory separators and
    ``*`` stops at one."""
    regex = []
    index = 0
    while index < len(pattern):
        if pattern.startswith("**", index):
            regex.append(".*")
            index += 2
        elif pattern[index] == "*":
            regex.append("[^/]*")
            index += 1
        elif pattern[index] == "?":
            regex.append("[^/]")
            index += 1
        else:
            regex.append(re.escape(pattern[index]))
            index += 1
    return re.fullmatch("".join(regex), path) is not None


def linked_files(pages: list[Path]) -> set[str]:
    """Every repository file the pages point at, by inline link or by
    reference definition. Either kind stops resolving when its target is
    renamed, so both have to be reachable by the filter."""
    found: set[str] = set()
    for page in pages:
        # read the same way check_links does: a code block is free to hold
        # bracket-and-paren shapes that are not links at all
        for href in page_hrefs(page.read_text(encoding="utf-8")):
            file_part = href.partition("#")[0]
            if not file_part or file_part.startswith(
                ("http://", "https://", "mailto:")
            ):
                continue
            try:
                found.add((page.parent / file_part).resolve()
                          .relative_to(ROOT).as_posix())
            except ValueError:
                # a jump out of the repository has nothing to trigger on
                continue
    return found


def check_workflow_triggers(pages: list[Path], report: Report) -> None:
    """Everything that can break these checks also starts them.

    Every check above runs only if the workflow's path filter matched the
    commit, so a file the filter leaves out is one whose rename or removal
    breaks a page with nothing left to report it: the commit touches nothing
    the filter names, the workflow never starts, and the dangling link waits
    for whoever next edits docs/ to be blamed for it. The filter is read by
    GitHub before any of this runs and so cannot be derived here; holding it
    to the tree is what keeps the two from drifting apart."""
    blocks = workflow_filters()
    if not blocks:
        report.fail(WORKFLOW, "declares no paths: filter to hold to the tree")
        return
    for block in blocks[1:]:
        if block != blocks[0]:
            report.fail(
                WORKFLOW,
                "its push and pull_request filters differ, so one event "
                "skips what the other checks",
            )
            return
    watched = {page.relative_to(ROOT).as_posix() for page in pages}
    watched |= linked_files(pages)
    watched |= {path.relative_to(ROOT).as_posix() for path in COMPANIONS}
    for path in sorted(watched):
        if not any(trigger_matches(f, path) for f in blocks[0]):
            report.fail(WORKFLOW, f"no paths: entry covers {path}")
    # an entry that matches nothing is one the tree has moved out from under,
    # and the next reader has no way to tell it from one still doing its job
    for pattern in blocks[0]:
        if not any(trigger_matches(pattern, path) for path in watched):
            report.fail(
                WORKFLOW,
                f"paths: entry {pattern} covers nothing these checks read",
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

    try:
        debt = translation_debt()
    except (OSError, KeyError, ValueError) as error:
        print(f"check-docs-links: cannot read {DEBT.name}: {error}",
              file=sys.stderr)
        return 1

    standalone = standalone_pages()
    # a guide offers a locale to choose, and so does a page whose translations
    # live in another directory; an English-only page carries no bar to check
    english_only = {page for page in standalone if page not in TRANSLATED_BY}

    report = Report()
    indexes: dict[Path, dict[str, str]] = {}
    check_locale_coverage(pages, report)
    guides = set(pages)
    for page in pages + standalone:
        text = page.read_text(encoding="utf-8")
        if page not in english_only:
            check_language_bar(page, text, report)
        # a guide sits under an index; the pages translating the repository
        # README sit at the top of the tree and have nothing above them
        if page in guides and page.parent not in FOREIGN_ORIGINAL:
            check_breadcrumb(page, text, report)
        check_links(page, text, report, debt)
        indexes[page] = check_definitions(page, text, report, debt)
        check_citations_are_links(page, text, report)
    check_translation_shape(pages, report, debt)
    check_index_parity(pages, indexes, report, debt)
    check_jump_parity(pages, report, debt)
    check_parent_index(pages, report)
    check_workflow_triggers(pages + standalone, report)

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

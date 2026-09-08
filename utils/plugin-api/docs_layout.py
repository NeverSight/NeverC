"""Map public documentation between English pages and language directories."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DOCS = ROOT / "docs"
LOCALES = ("", "zh-CN", "zh-TW", "ja", "ko", "fr", "de", "es", "it", "ru", "ar")


def locale_of(page: Path) -> str:
    """Docs use language directories; examples retain filename suffixes."""
    page = page.resolve()
    if page.is_relative_to(DOCS):
        first = page.relative_to(DOCS).parts[0]
        return first if first in LOCALES else ""
    suffix = page.stem.rsplit(".", 1)[-1]
    return suffix if suffix in LOCALES else ""


def english_page(page: Path) -> Path:
    page = page.resolve()
    locale = locale_of(page)
    if not locale:
        return page
    if page.is_relative_to(DOCS):
        relative = page.relative_to(DOCS / locale)
        return ROOT / "README.md" if relative == Path("project.md") else DOCS / relative
    return page.with_name(page.name[:-len(locale) - 4] + ".md")


def localized_page(page: Path, locale: str) -> Path:
    if locale not in LOCALES:
        raise ValueError(f"Unsupported documentation locale: {locale}")
    original = english_page(page)
    if not locale:
        return original
    if original == ROOT / "README.md":
        return DOCS / locale / "project.md"
    if original.is_relative_to(DOCS):
        return DOCS / locale / original.relative_to(DOCS)
    return original.with_name(f"{original.stem}.{locale}{original.suffix}")

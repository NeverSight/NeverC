"""Static regressions for language-directory navigation and guard coverage."""

import importlib.util
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from docs_layout import DOCS, ROOT, english_page, locale_of, localized_page

spec = importlib.util.spec_from_file_location(
    "docs_navigation", Path(__file__).resolve().parents[1] / "check-docs-links.py"
)
nav = importlib.util.module_from_spec(spec)
spec.loader.exec_module(nav)


class DocumentationLayoutTests(unittest.TestCase):
    def test_project_overview_and_documentation_index_are_distinct(self):
        self.assertEqual(localized_page(ROOT / "README.md", "ja"), DOCS / "ja/project.md")
        self.assertEqual(localized_page(DOCS / "README.md", "ja"), DOCS / "ja/README.md")
        self.assertEqual(english_page(DOCS / "ja/project.md"), ROOT / "README.md")

    def test_example_translations_stay_beside_the_example(self):
        original = ROOT / "examples/windows-exe/README.md"
        translated = original.with_name("README.zh-CN.md")
        self.assertEqual(localized_page(original, "zh-CN"), translated)
        self.assertEqual(english_page(translated), original)

    def test_every_guide_round_trips_to_its_language(self):
        for page in nav.guide_pages():
            with self.subTest(page=page):
                self.assertEqual(localized_page(english_page(page), locale_of(page)), page)

    def test_missing_directory_translation_is_rejected(self):
        missing = DOCS / "zh-CN/build.md"
        report = nav.Report()
        nav.check_locale_coverage([p for p in nav.guide_pages() if p != missing], report)
        self.assertTrue(any("docs/build.md" in e and "zh-CN" in e for e in report.failures))

    def test_same_basename_cannot_hide_a_cross_language_jump(self):
        report = nav.Report()
        nav.check_target(DOCS / "zh-CN/README.md", "English index", "../README.md", report, set())
        self.assertTrue(any("leaves the zh-CN locale" in e for e in report.failures))

    def test_missing_anchor_is_rejected(self):
        report = nav.Report()
        nav.check_target(DOCS / "zh-CN/README.md", "bad anchor", "build.md#no-such-heading-123", report, set())
        self.assertTrue(report.failures)

    def test_language_selector_requires_all_locales(self):
        page = DOCS / "zh-CN/build.md"
        text = page.read_text().replace(nav.bar_entry(page, "ja"), "missing-japanese.md")
        report = nav.Report()
        nav.check_language_bar(page, text, report)
        self.assertTrue(report.failures)

    def test_index_cannot_omit_a_topic_guide(self):
        index = DOCS / "zh-CN/README.md"
        target = "plugin-api-driver.md"
        original_read = Path.read_text
        text = index.read_text()
        self.assertIn(f"]({target})", text)
        broken = text.replace(f"]({target})", "](missing-guide.md)")
        def read(path, *args, **kwargs):
            return broken if path == index else original_read(path, *args, **kwargs)
        report = nav.Report()
        with patch.object(Path, "read_text", read):
            nav.check_parent_index(nav.guide_pages(), report)
        self.assertTrue(any(f"does not link {target}" in e for e in report.failures))


    def test_topic_membership_requires_a_hyphen_boundary(self):
        index = DOCS / "plugin-api.md"
        self.assertTrue(nav.within(DOCS / "plugin-api-driver.md", index))
        self.assertFalse(nav.within(DOCS / "plugin-apiExtra.md", index))
        self.assertFalse(nav.within(DOCS / "builtins-string.md", index))

    def test_flat_topic_index_cannot_omit_its_child(self):
        index = DOCS / "zh-CN/plugin-api.md"
        target = "plugin-api-driver.md"
        original_read = Path.read_text
        text = index.read_text()
        self.assertIn(f"]({target})", text)
        # Section links still reach the guide, so remove those targets too.
        broken = text.replace(f"]({target})", "](missing-guide.md)")
        broken = broken.replace(f"]({target}#", "](missing-guide.md#")
        self.assertNotIn(f"]({target})", broken)
        self.assertNotIn(f"]({target}#", broken)
        def read(path, *args, **kwargs):
            return broken if path == index else original_read(path, *args, **kwargs)
        report = nav.Report()
        with patch.object(Path, "read_text", read):
            nav.check_parent_index(nav.guide_pages(), report)
        self.assertTrue(any(f"does not link {target}" in e for e in report.failures))


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Check that measured alternatives preserve their compile-family ABI."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("gki_variant_compat", TOOLS / "generate-compat-table.py")
compat = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compat)


class VariantEvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.families = compat.validate_evidence(
            compat.load_catalog(compat.DEFAULT_CATALOG),
            compat.DEFAULT_MANIFEST_ROOT, compat.DEFAULT_RELEASE_LOCK)
        cls.document = json.loads(compat.DEFAULT_VARIANTS.read_text())

    def load(self, document, manifest_mutation=None):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            directory = root / "gki-variant-manifests"
            directory.mkdir()
            for record in document["variants"]:
                key = record["key"]
                original = compat.DEFAULT_VARIANTS.parent / "gki-variant-manifests" / f"{key}.json"
                manifest = json.loads(original.read_text())
                if manifest_mutation:
                    manifest_mutation(manifest)
                (directory / f"{key}.json").write_text(json.dumps(manifest))
            path = root / "gki-variants.json"
            path.write_text(json.dumps(document))
            return compat.load_variant_evidence(path, self.families)

    def test_all_measured_variants_keep_the_compile_family(self):
        records = self.load(self.document)
        self.assertEqual({p["variant_key"] for p, _, _ in records},
                         {"612-kmi7", "618-kmi6", "703-stable"})
        for profile, _, _ in records:
            self.assertNotEqual(profile["layout_id"], profile["legacy_id"])

    def test_loader_change_requires_a_dedicated_compile_profile(self):
        def move_exit(manifest):
            manifest["layouts"]["module"]["members"]["exit"] += 8
        with self.assertRaisesRegex(ValueError, "loader ABI differs"):
            self.load(self.document, move_exit)

    def test_duplicate_variant_key_is_rejected(self):
        document = copy.deepcopy(self.document)
        entry = copy.deepcopy(document["variants"][0])
        entry["layout_id"] += 1
        # Reusing a key is itself invalid; the unambiguous mapping is mandatory.
        document["variants"].append(entry)
        with self.assertRaisesRegex(ValueError, "duplicate"):
            self.load(document)

    def test_cross_android_generation_is_rejected(self):
        document = copy.deepcopy(self.document)
        document["variants"][0]["profile"]["android_release"] += 1
        with self.assertRaises(ValueError):
            self.load(document)

    def test_stale_procmap_capability_is_rejected(self):
        document = copy.deepcopy(self.document)
        document["variants"][0]["profile"]["capabilities"]["procmap_ioctl"]["proc_maps_private_mm"] = 16
        with self.assertRaisesRegex(ValueError, "procmap capabilities differ"):
            self.load(document)

    def test_malformed_release_and_unpinned_source_are_rejected(self):
        for field, value in (("source_commit", "android16-6.12"),
                             ("require_release_token", "false")):
            document = copy.deepcopy(self.document)
            document["variants"][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.load(document)


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Regression tests for canonical GKI manifest config fields."""

import importlib.util
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock


TOOLS_ROOT = Path(__file__).resolve().parent
GENERATOR = TOOLS_ROOT / "generate-gki-manifest.py"


def load_generator():
    spec = importlib.util.spec_from_file_location(
        "nvk_generate_gki_manifest_test", GENERATOR
    )
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load GKI manifest generator")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ManifestConfigTests(unittest.TestCase):
    def read(self, text: str):
        generator = load_generator()
        with tempfile.TemporaryDirectory(prefix="neverc-manifest-config-") as raw:
            config = Path(raw) / ".config"
            config.write_text(
                "CONFIG_ARM64_4K_PAGES=y\n" + text,
                encoding="utf-8",
            )
            return generator.read_config(config)

    def test_legacy_cfi_spelling_is_preserved(self):
        values = self.read("CONFIG_CFI_CLANG=y\n")
        self.assertIs(values["CONFIG_CFI_CLANG"], True)
        self.assertNotIn("CONFIG_CFI", values)

    def test_modern_cfi_spelling_maps_to_canonical_field(self):
        values = self.read("CONFIG_CFI=y\n")
        self.assertIs(values["CONFIG_CFI_CLANG"], True)
        self.assertNotIn("CONFIG_CFI", values)

    def test_absent_cfi_stays_disabled(self):
        values = self.read("")
        self.assertIs(values["CONFIG_CFI_CLANG"], False)


class DiscoveryTests(unittest.TestCase):
    def test_unregistered_profile_collects_fresh_exports_without_sdk_compilation(self):
        generator = load_generator()
        exporter = generator.load_tool("discovery_test_exports", "check-sdk-exports.py")
        layouts = {
            "module": {"size": 1024, "members": {"mem": 128},
                       "member_sizes": {"mem": 112}},
            "module_memory": {"size": 16, "members": {"base": 0, "size": 8},
                              "member_sizes": {"base": 8, "size": 4}},
        }
        extractor = SimpleNamespace(extract_layouts=mock.Mock(return_value=layouts))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            config = root / ".config"
            config.write_text("CONFIG_ARM64_4K_PAGES=y\nCONFIG_CFI=y\n")
            symvers = root / "Module.symvers"
            symvers.write_text(
                "0x12345678\tnew_export\tvmlinux\tEXPORT_SYMBOL_GPL\t\n"
                "0x87654321\tnamespaced_export\tvmlinux\tEXPORT_SYMBOL_GPL\tNEW_NS\n"
            )
            output = root / "703.discovery.json"
            argv = [str(GENERATOR), "--profile", "703", "--kernel-name", "android18-7.3",
                    "--vmlinux", str(root / "vmlinux"), "--config", str(config),
                    "--symvers", str(symvers), "--discovery", "--output", str(output)]
            with (
                mock.patch.object(sys, "argv", argv),
                mock.patch.object(generator, "STRUCTURES", ("module",)),
                mock.patch.object(generator, "OPTIONAL_STRUCTURES", ()),
                mock.patch.object(generator, "load_tool", side_effect=[extractor, exporter]),
                mock.patch.object(generator, "elf_evidence", return_value={"layout_format": "BTF"}),
                mock.patch.object(generator, "bind_arm64_runtime_config", side_effect=lambda c, _: c),
                mock.patch.object(generator, "sdk_export_evidence") as sdk,
                contextlib.redirect_stdout(io.StringIO()),
            ):
                self.assertEqual(generator.main(), 0)
            sdk.assert_not_called()
            manifest = json.loads(output.read_text())
            self.assertIs(manifest["discovery"], True)
            self.assertEqual(manifest["profile"], 703)
            self.assertEqual(manifest["kernel_name"], "android18-7.3")
            self.assertEqual(manifest["sdk_exports"], ["namespaced_export", "new_export"])
            self.assertEqual(manifest["evidence"]["symvers_sha256"], generator.sha256_file(symvers))

    def test_discovery_cannot_inherit_old_manifest_evidence(self):
        generator = load_generator()
        argv = [str(GENERATOR), "--profile", "703", "--kernel-name", "android18-7.3",
                "--vmlinux", "unused", "--discovery", "--base-manifest", "old.json",
                "--output", "unused.json"]
        with mock.patch.object(sys, "argv", argv), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(generator.main(), 1)

    def test_discovery_cannot_be_used_as_a_runtime_profile(self):
        generator = load_generator()
        compat = generator.load_tool("discovery_test_compat", "generate-compat-table.py")
        runtime = TOOLS_ROOT.parent
        profiles = compat.load_catalog(runtime / "arm64/gki-profiles.json")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for path in (runtime / "arm64/gki-manifests").glob("*.json"):
                (root / path.name).write_bytes(path.read_bytes())
            manifest = json.loads((runtime / "arm64/gki-manifests/618.json").read_text())
            manifest["discovery"] = True
            (root / "618.json").write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, "discovery evidence is not a certified profile"):
                compat.validate_evidence(profiles, root, runtime / "arm64/gki-release.json")


class PrereleaseProfileTests(unittest.TestCase):
    def test_branch_name_does_not_override_measured_release(self):
        generator = load_generator()
        compat = generator.load_tool("prerelease_compat_test", "generate-compat-table.py")
        document = json.loads((TOOLS_ROOT.parent / "arm64/gki-profiles.json").read_text())
        profile = document["profiles"][-1]
        profile.update({"legacy_id": 703, "kernel_name": "android18-7.3",
                        "symbol": "ANDROID18_7_3_KMI0", "android_release": 18,
                        "linux_major": 7, "linux_minor": 2, "linux_patch": 0,
                        "linux_extraversion": "-rc9", "kmi_generation": 0,
                        "vermagic": "7.2.0-rc9-android18-0-4k SMP preempt mod_unload modversions aarch64"})
        with tempfile.TemporaryDirectory() as directory:
            catalog = Path(directory) / "catalog.json"
            def load():
                catalog.write_text(json.dumps({"schema": 1, "profiles": [profile]}))
                return compat.load_catalog(catalog)
            self.assertEqual(load()[0]["kernel_name"], "android18-7.3")
            profile["vermagic"] = "7.3.0-android18-0-4k SMP preempt mod_unload modversions aarch64"
            with self.assertRaisesRegex(ValueError, "vermagic identity mismatch"):
                load()
            profile["vermagic"] = "7.2.0-rc10-android18-0-4k SMP preempt mod_unload modversions aarch64"
            with self.assertRaisesRegex(ValueError, "vermagic identity mismatch"):
                load()


if __name__ == "__main__":
    unittest.main()

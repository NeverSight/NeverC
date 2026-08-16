#!/usr/bin/env python3
"""Behavior tests for the local GKI evidence auditor."""

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


TOOLS_ROOT = Path(__file__).resolve().parent
AUDITOR = TOOLS_ROOT / "audit-gki-evidence.py"
RUNTIME_ROOT = TOOLS_ROOT.parent


def load_auditor_module():
    spec = importlib.util.spec_from_file_location("nvk_evidence_auditor", AUDITOR)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load evidence auditor")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class EvidenceAuditCliTests(unittest.TestCase):
    def test_repository_catalog_covers_every_profile(self):
        auditor = load_auditor_module()
        catalog = auditor.load_catalog(
            RUNTIME_ROOT / "arm64/gki-local-evidence.json"
        )
        profiles = auditor.load_profiles(RUNTIME_ROOT / "arm64/gki-profiles.json")

        self.assertEqual(set(catalog), set(profiles))
        self.assertEqual(set(catalog), {
            "510", "51013", "515", "51514", "601", "606", "612", "618"
        })
        for evidence in catalog.values():
            for key in evidence["manifest_bound_artifacts"]:
                self.assertIsNotNone(evidence["artifacts"][key])

    def test_missing_backend_symbol_is_reported_without_absolute_paths(self):
        with tempfile.TemporaryDirectory(prefix="neverc-evidence-audit-") as raw:
            root = Path(raw)
            local_docs = root / "local_docs"
            evidence = local_docs / "profile-510"
            evidence.mkdir(parents=True)
            (evidence / "System.map").write_text(
                "ffffffc080001000 t binder_transaction\n",
                encoding="utf-8",
            )
            catalog = root / "catalog.json"
            catalog.write_text(
                json.dumps(
                    {
                        "schema": 1,
                        "profiles": {
                            "510": {
                                "manifest_bound_artifacts": [],
                                "source_roots": [],
                                "artifacts": {
                                    "config": None,
                                    "image": None,
                                    "symvers": None,
                                    "system_map": "profile-510/System.map",
                                    "vmlinux": None,
                                },
                            }
                        },
                    }
                ),
                encoding="utf-8",
            )
            profiles = root / "profiles.json"
            profiles.write_text(
                json.dumps(
                    {
                        "schema": 1,
                        "profiles": [
                            {
                                "legacy_id": 510,
                                "kernel_name": "android12-5.10",
                                "capabilities": {
                                    "binder_filter_backend": "transaction",
                                    "vmalloc_visibility_backend": "seq_operations",
                                },
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )

            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDITOR),
                    "--catalog",
                    str(catalog),
                    "--profiles",
                    str(profiles),
                    "--local-docs",
                    str(local_docs),
                    "--format",
                    "json",
                ],
                capture_output=True,
                text=True,
                check=False,
            )

            self.assertEqual(result.returncode, 1, result.stderr)
            report = json.loads(result.stdout)
            issues = report["profiles"]["510"]["issues"]
            self.assertIn("missing-symbol:vmalloc_op", issues)
            self.assertIn("unbound-manifest-artifact:config", issues)
            self.assertIn("unbound-manifest-artifact:symvers", issues)
            self.assertIn("unbound-manifest-artifact:vmlinux", issues)
            self.assertNotIn(str(root), result.stdout)

    def test_manifest_hash_drift_is_reported(self):
        with tempfile.TemporaryDirectory(prefix="neverc-evidence-drift-") as raw:
            root = Path(raw)
            local_docs = root / "local_docs"
            evidence = local_docs / "profile-510"
            evidence.mkdir(parents=True)
            (evidence / ".config").write_bytes(b"CONFIG_MMU=y\n")
            (evidence / "System.map").write_text(
                "ffffffc080001000 t binder_transaction\n"
                "ffffffc081001000 d vmalloc_op\n",
                encoding="utf-8",
            )
            catalog = root / "catalog.json"
            catalog.write_text(
                json.dumps(
                    {
                        "schema": 1,
                        "profiles": {
                            "510": {
                                "manifest_bound_artifacts": ["config"],
                                "source_roots": [],
                                "artifacts": {
                                    "config": "profile-510/.config",
                                    "image": None,
                                    "symvers": None,
                                    "system_map": "profile-510/System.map",
                                    "vmlinux": None,
                                },
                            }
                        },
                    }
                ),
                encoding="utf-8",
            )
            profiles = root / "profiles.json"
            profiles.write_text(
                json.dumps(
                    {
                        "schema": 1,
                        "profiles": [
                            {
                                "legacy_id": 510,
                                "kernel_name": "android12-5.10",
                                "capabilities": {
                                    "binder_filter_backend": "transaction",
                                    "vmalloc_visibility_backend": "seq_operations",
                                },
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
            manifests = root / "manifests"
            manifests.mkdir()
            (manifests / "510.json").write_text(
                json.dumps(
                    {
                        "profile": 510,
                        "evidence": {
                            "config_sha256": "0" * 64,
                            "symvers_sha256": "1" * 64,
                        },
                    }
                ),
                encoding="utf-8",
            )

            result = subprocess.run(
                [
                    sys.executable,
                    str(AUDITOR),
                    "--catalog",
                    str(catalog),
                    "--profiles",
                    str(profiles),
                    "--manifest-root",
                    str(manifests),
                    "--local-docs",
                    str(local_docs),
                    "--format",
                    "json",
                ],
                capture_output=True,
                text=True,
                check=False,
            )

            self.assertEqual(result.returncode, 1, result.stderr)
            report = json.loads(result.stdout)
            self.assertIn(
                "evidence-drift:config_sha256",
                report["profiles"]["510"]["issues"],
            )


if __name__ == "__main__":
    unittest.main()

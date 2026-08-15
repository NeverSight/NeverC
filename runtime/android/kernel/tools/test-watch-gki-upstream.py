#!/usr/bin/env python3
"""Unit tests for the official GKI / KMI upstream watcher."""

import base64
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock
import urllib.error


TOOLS = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location(
    "watch_gki_upstream", TOOLS / "watch-gki-upstream.py"
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load watch-gki-upstream.py")
watch = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(watch)


def encode_text(text):
    return base64.b64encode(text.encode("utf-8"))


def makefile(major, minor, patch):
    return (
        f"VERSION = {major}\n"
        f"PATCHLEVEL = {minor}\n"
        f"SUBLEVEL = {patch}\n"
        "EXTRAVERSION =\n"
    )


def constants(branch, kmi, pointer=None):
    if pointer is not None:
        return pointer
    return f"BRANCH={branch}\nKMI_GENERATION={kmi}\n"


class FakeResponse:
    def __init__(self, payload, status=200):
        self.payload = payload
        self.status = status

    def read(self):
        return self.payload

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        return False


class FakeOpener:
    def __init__(self, mapping, posted=None):
        self.mapping = mapping
        self.posted = posted if posted is not None else []

    def open(self, request, timeout=None):
        del timeout
        url = request.full_url
        if request.get_method() == "POST":
            self.posted.append(
                {
                    "url": url,
                    "body": request.data,
                    "headers": dict(request.header_items()),
                }
            )
            return FakeResponse(b"", status=204)
        if url not in self.mapping:
            raise urllib.error.HTTPError(url, 404, "missing", hdrs=None, fp=None)
        payload = self.mapping[url]
        if isinstance(payload, Exception):
            raise payload
        return FakeResponse(payload)


def aosp_url(branch, path):
    return watch.googlesource_text_url(branch, path)


def family(name="android16-6.12", legacy_id=612, android=16, major=6, minor=12, patch=89, kmi=6):
    return {
        "legacy_id": legacy_id,
        "kernel_name": name,
        "android_release": android,
        "linux_major": major,
        "linux_minor": minor,
        "linux_patch": patch,
        "kmi_generation": kmi,
    }


def live_record(name="android16-6.12", legacy_id=612, android=16, catalog_patch=89, catalog_kmi=6,
                live_patch=90, live_kmi=6, kminext_kmi=None):
    major, minor = (int(part) for part in name.split("-")[1].split("."))
    record = {
        "legacy_id": legacy_id,
        "kernel_name": name,
        "android_release": android,
        "linux_major": major,
        "linux_minor": minor,
        "linux_patch": catalog_patch,
        "kmi_generation": catalog_kmi,
        "linux_release": f"{major}.{minor}.{catalog_patch}",
        "live": {
            "linux_major": major,
            "linux_minor": minor,
            "linux_patch": live_patch,
            "linux_release": f"{major}.{minor}.{live_patch}",
            "kmi_generation": live_kmi,
            "kmi_source": "build.config.constants",
            "kmi_version": f"{major}.{minor}-android{android}-{live_kmi}",
        },
        "kminext": None
        if kminext_kmi is None
        else {
            "branch": f"{name}-kminext",
            "linux_release": f"{major}.{minor}.{live_patch}",
            "kmi_generation": kminext_kmi,
            "kmi_source": "build.config.constants",
        },
    }
    return record


class ParseTests(unittest.TestCase):
    def test_parse_makefile_and_kmi(self):
        major, minor, patch = watch.parse_makefile_version(makefile(6, 12, 90))
        self.assertEqual((major, minor, patch), (6, 12, 90))
        self.assertEqual(watch.parse_kmi_generation('KMI_GENERATION=6\n'), 6)
        self.assertEqual(watch.parse_kmi_generation('KMI_GENERATION="5"\n'), 5)
        self.assertIsNone(watch.parse_kmi_generation("BRANCH=android16-6.12\n"))

    def test_pointer_file(self):
        self.assertTrue(watch.is_pointer_file("bazel/constants.scl\n"))
        self.assertFalse(watch.is_pointer_file("BRANCH=android16-6.12\nKMI_GENERATION=6\n"))

    def test_ls_remote_filters_gki_heads(self):
        listing = "\n".join(
            [
                "aaa refs/heads/android16-6.12",
                "bbb refs/heads/android16-6.12-kminext",
                "ccc refs/heads/android-mainline",
                "ddd refs/heads/android16-6.12-2026-03_r33",
                "eee refs/heads/android17-6.18",
            ]
        )
        self.assertEqual(
            watch.parse_ls_remote_heads(listing),
            [
                "android16-6.12",
                "android16-6.12-kminext",
                "android17-6.18",
            ],
        )

    def test_redact_webhook(self):
        url = "https://discord.com/api/webhooks/1/super-secret-token"
        redacted = watch.redact_webhook(url)
        self.assertNotIn("super-secret-token", redacted)
        self.assertIn("/1/<redacted>", redacted)


class ProbeTests(unittest.TestCase):
    def test_reads_constants_then_common_and_follows_pointer(self):
        mapping = {
            aosp_url("android12-5.10", "Makefile"): encode_text(makefile(5, 10, 260)),
            aosp_url("android12-5.10", "build.config.constants"): urllib.error.HTTPError(
                aosp_url("android12-5.10", "build.config.constants"),
                404,
                "missing",
                hdrs=None,
                fp=None,
            ),
            aosp_url("android12-5.10", "build.config.common"): encode_text(
                constants("android12-5.10", 9)
            ),
            aosp_url("android17-6.18", "Makefile"): encode_text(makefile(6, 18, 32)),
            aosp_url("android17-6.18", "build.config.constants"): encode_text(
                "bazel/constants.scl\n"
            ),
            aosp_url("android17-6.18", "bazel/constants.scl"): encode_text(
                'BRANCH="android17-6.18"\nKMI_GENERATION=5\n'
            ),
        }
        opener = FakeOpener(mapping)
        old = watch.probe_branch("android12-5.10", opener=opener)
        self.assertEqual(old["linux_release"], "5.10.260")
        self.assertEqual(old["kmi_generation"], 9)
        self.assertEqual(old["kmi_source"], "build.config.common")
        new = watch.probe_branch("android17-6.18", opener=opener)
        self.assertEqual(new["linux_release"], "6.18.32")
        self.assertEqual(new["kmi_generation"], 5)
        self.assertEqual(new["kmi_source"], "bazel/constants.scl")
        self.assertEqual(new["kmi_version"], "6.18-android17-5")

    def test_kminext_404_is_optional(self):
        mapping = {
            aosp_url("android15-6.6", "Makefile"): encode_text(makefile(6, 6, 142)),
            aosp_url("android15-6.6", "build.config.constants"): encode_text(
                constants("android15-6.6", 8)
            ),
        }
        record = watch.probe_family(family("android15-6.6", 606, 15, 6, 6, 139, 8), opener=FakeOpener(mapping))
        self.assertEqual(record["live"]["linux_release"], "6.6.142")
        self.assertIsNone(record["kminext"])


class DiffTests(unittest.TestCase):
    def test_first_run_compares_to_catalog_and_skips_historical_branches(self):
        records = [live_record(live_patch=90, kminext_kmi=7)]
        snapshot = watch.baseline_from_catalog([family()])
        changes = watch.collect_changes(
            records,
            snapshot,
            ["android11-5.4", "android16-6.12", "android16-6.12-kminext"],
        )
        kinds = {change["kind"] for change in changes}
        self.assertEqual(kinds, {"gki_version", "kmi_next"})
        self.assertFalse(any(change["kind"] == "new_branch" for change in changes))

    def test_kmi_bump_and_new_branch_against_live_snapshot(self):
        records = [
            live_record(live_patch=90, live_kmi=7, kminext_kmi=7),
        ]
        snapshot = {
            "source": "live",
            "families": {
                "android16-6.12": {
                    "linux_major": 6,
                    "linux_minor": 12,
                    "linux_patch": 90,
                    "kmi_generation": 6,
                    "kminext_kmi_generation": 7,
                }
            },
            "known_gki_branches": ["android16-6.12", "android16-6.12-kminext"],
        }
        changes = watch.collect_changes(
            records,
            snapshot,
            ["android16-6.12", "android16-6.12-kminext", "android18-6.21"],
        )
        self.assertEqual(
            [(change["kind"], change.get("to") or change.get("branch")) for change in changes],
            [("kmi", 7), ("new_branch", "android18-6.21")],
        )

    def test_quiet_when_live_matches_snapshot(self):
        records = [live_record(live_patch=90, live_kmi=6, kminext_kmi=7)]
        snapshot = {
            "source": "live",
            "families": {
                "android16-6.12": {
                    "linux_major": 6,
                    "linux_minor": 12,
                    "linux_patch": 90,
                    "kmi_generation": 6,
                    "kminext_kmi_generation": 7,
                }
            },
            "known_gki_branches": ["android16-6.12"],
        }
        self.assertEqual(watch.collect_changes(records, snapshot, ["android16-6.12"]), [])


class DiscordTests(unittest.TestCase):
    def test_payload_uses_english_sections_and_kmi_color(self):
        report = watch.build_report(
            [live_record(live_patch=90, live_kmi=7, kminext_kmi=8)],
            [],
            watch.baseline_from_catalog([family()]),
            ["android16-6.12"],
            False,
        )
        payload = watch.build_discord_payload(report)
        description = payload["embeds"][0]["description"]
        self.assertIn("KMI generation changed", description)
        self.assertIn("GKI version updated", description)
        self.assertIn("Upcoming KMI", description)
        self.assertEqual(payload["embeds"][0]["color"], 0xE74C3C)
        dumped = json.dumps(payload)
        self.assertNotIn("discord.com/api/webhooks", dumped)

    def test_notify_skips_when_secret_missing(self):
        mapping = {
            aosp_url("android16-6.12", "Makefile"): encode_text(makefile(6, 12, 90)),
            aosp_url("android16-6.12", "build.config.constants"): encode_text(
                constants("android16-6.12", 6)
            ),
        }
        catalog = {
            "profiles": [
                {
                    "legacy_id": 612,
                    "kernel_name": "android16-6.12",
                    "android_release": 16,
                    "linux_major": 6,
                    "linux_minor": 12,
                    "linux_patch": 89,
                    "kmi_generation": 6,
                }
            ]
        }
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            catalog_path = root / "catalog.json"
            catalog_path.write_text(json.dumps(catalog), encoding="utf-8")
            refs = root / "refs.txt"
            refs.write_text("aaa refs/heads/android16-6.12\n", encoding="utf-8")
            stdout = io.StringIO()
            with mock.patch.object(watch.sys, "stdout", stdout):
                code = watch.main(
                    [
                        "--catalog",
                        str(catalog_path),
                        "--ls-remote-file",
                        str(refs),
                        "--notify",
                    ],
                    opener=FakeOpener(mapping),
                    environ={},
                )
        self.assertEqual(code, 0)
        self.assertIn("GKI_WATCH_DISCORD_WEBHOOK_URL is unset", stdout.getvalue())

    def test_notify_posts_without_echoing_secret(self):
        mapping = {
            aosp_url("android16-6.12", "Makefile"): encode_text(makefile(6, 12, 90)),
            aosp_url("android16-6.12", "build.config.constants"): encode_text(
                constants("android16-6.12", 6)
            ),
        }
        catalog = {
            "profiles": [
                {
                    "legacy_id": 612,
                    "kernel_name": "android16-6.12",
                    "android_release": 16,
                    "linux_major": 6,
                    "linux_minor": 12,
                    "linux_patch": 89,
                    "kmi_generation": 6,
                }
            ]
        }
        posted = []
        webhook = "https://discord.com/api/webhooks/9/please-do-not-log-me"
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            catalog_path = root / "catalog.json"
            catalog_path.write_text(json.dumps(catalog), encoding="utf-8")
            refs = root / "refs.txt"
            refs.write_text("aaa refs/heads/android16-6.12\n", encoding="utf-8")
            stdout = io.StringIO()
            with mock.patch.object(watch.sys, "stdout", stdout):
                code = watch.main(
                    [
                        "--catalog",
                        str(catalog_path),
                        "--ls-remote-file",
                        str(refs),
                        "--notify",
                    ],
                    opener=FakeOpener(mapping, posted=posted),
                    environ={watch.WEBHOOK_ENV: webhook},
                )
        self.assertEqual(code, 0)
        self.assertEqual(len(posted), 1)
        self.assertEqual(posted[0]["url"], webhook)
        output = stdout.getvalue()
        self.assertIn("Discord notification sent.", output)
        self.assertNotIn("please-do-not-log-me", output)
        self.assertNotIn(webhook, output)

    def test_http_error_does_not_include_webhook(self):
        class RejectingOpener:
            def open(self, request, timeout=None):
                del timeout
                raise urllib.error.HTTPError(
                    request.full_url, 401, "nope", hdrs=None, fp=None
                )

        with self.assertRaises(watch.WatchError) as caught:
            watch.post_discord(
                "https://discord.com/api/webhooks/9/please-do-not-log-me",
                {"content": "hi"},
                opener=RejectingOpener(),
            )
        self.assertNotIn("please-do-not-log-me", str(caught.exception))


class CatalogLoadTests(unittest.TestCase):
    def test_repo_catalog_has_eight_gki_families(self):
        families = watch.load_catalog(watch.DEFAULT_CATALOG)
        names = [family["kernel_name"] for family in families]
        self.assertEqual(
            names,
            [
                "android12-5.10",
                "android13-5.10",
                "android13-5.15",
                "android14-5.15",
                "android14-6.1",
                "android15-6.6",
                "android16-6.12",
                "android17-6.18",
            ],
        )


if __name__ == "__main__":
    unittest.main()

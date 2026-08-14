#!/usr/bin/env python3
"""Focused tests for the pinned GKI release verifier."""

import hashlib
import importlib.util
import io
from pathlib import Path
import stat
import tarfile
import tempfile
import unittest


TOOLS = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location(
    "verify_gki_release", TOOLS / "verify-gki-release.py"
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load verify-gki-release.py")
verify = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(verify)


PROFILES = ("510", "51013", "515", "51514", "601", "606", "612", "618")


def profile_entry(profile):
    return {
        "kernel_name": {
            "510": "android12-5.10",
            "51013": "android13-5.10",
            "515": "android13-5.15",
            "51514": "android14-5.15",
            "601": "android14-6.1",
            "606": "android15-6.6",
            "612": "android16-6.12",
            "618": "android17-6.18",
        }[profile],
        "asset": f"gki-{profile}.tar.gz",
        "kcfi_typeids": (
            None
            if profile in ("510", "51013", "515", "51514")
            else {
                "cleanup_module": "0xe5c47d60",
                "init_module": "0x6fbb3035",
            }
        ),
        "size": 123,
        "sha256": hashlib.sha256(profile.encode()).hexdigest(),
        "vermagic": f"6.0.{profile}-android SMP mod_unload aarch64",
        "offset_module": "dist/zsmalloc.ko",
    }


def valid_lock():
    return {
        "schema": 1,
        "repository": {"id": 1240844551, "name": "NeverSight/NeverC"},
        "tag": "gki-build-20260701",
        "profiles": {profile: profile_entry(profile) for profile in PROFILES},
    }


class LockTests(unittest.TestCase):
    def test_valid_lock(self):
        lock = verify.validate_lock(valid_lock())
        self.assertEqual(tuple(lock["profiles"]), PROFILES)

    def test_missing_profile_is_rejected(self):
        lock = valid_lock()
        del lock["profiles"]["618"]
        with self.assertRaisesRegex(verify.ValidationError, "profiles"):
            verify.validate_lock(lock)

    def test_missing_android_generation_family_is_rejected(self):
        lock = valid_lock()
        del lock["profiles"]["51013"]
        with self.assertRaisesRegex(verify.ValidationError, "profiles"):
            verify.validate_lock(lock)

    def test_unknown_profile_is_rejected(self):
        lock = valid_lock()
        lock["profiles"]["999"] = profile_entry("618")
        lock["profiles"]["999"]["asset"] = "gki-999.tar.gz"
        with self.assertRaisesRegex(verify.ValidationError, "profiles"):
            verify.validate_lock(lock)

    def test_duplicate_asset_is_rejected(self):
        lock = valid_lock()
        lock["profiles"]["515"]["asset"] = lock["profiles"]["510"]["asset"]
        with self.assertRaisesRegex(verify.ValidationError, "duplicate asset"):
            verify.validate_lock(lock)

    def test_duplicate_offset_member_identity_is_rejected(self):
        lock = valid_lock()
        lock["profiles"]["515"]["asset"] = lock["profiles"]["510"]["asset"]
        lock["profiles"]["515"]["offset_module"] = lock["profiles"]["510"][
            "offset_module"
        ]
        with self.assertRaisesRegex(verify.ValidationError, "duplicate asset"):
            verify.validate_lock(lock)

    def test_kcfi_typeids_are_exact_and_canonical(self):
        for value in (
            {},
            {"init_module": "0x6fbb3035"},
            {
                "cleanup_module": "0XE5C47D60",
                "init_module": "0x6fbb3035",
            },
            {
                "cleanup_module": "0x00000000",
                "init_module": "0x6fbb3035",
            },
        ):
            with self.subTest(value=value):
                lock = valid_lock()
                lock["profiles"]["618"]["kcfi_typeids"] = value
                with self.assertRaisesRegex(verify.ValidationError, "KCFI"):
                    verify.validate_lock(lock)


class ArchiveIdentityTests(unittest.TestCase):
    def test_size_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "asset.tar.gz"
            path.write_bytes(b"payload")
            with self.assertRaisesRegex(verify.ValidationError, "size"):
                verify.verify_archive_identity(
                    path, 8, hashlib.sha256(b"payload").hexdigest()
                )

    def test_sha_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "asset.tar.gz"
            path.write_bytes(b"payload")
            with self.assertRaisesRegex(verify.ValidationError, "SHA-256"):
                verify.verify_archive_identity(path, 7, "0" * 64)


class ArchiveSafetyTests(unittest.TestCase):
    @staticmethod
    def member(name, size=0, kind=tarfile.REGTYPE, linkname=""):
        member = tarfile.TarInfo(name)
        member.size = size
        member.type = kind
        member.linkname = linkname
        return member

    def test_absolute_and_parent_paths_are_rejected(self):
        for name in ("/etc/passwd", "../escape", "safe/../escape", "C:\\escape"):
            with self.subTest(name=name), self.assertRaises(verify.ValidationError):
                verify.validate_tar_members([self.member(name)])

    def test_links_and_special_files_are_rejected(self):
        for kind in (
            tarfile.SYMTYPE,
            tarfile.LNKTYPE,
            tarfile.CHRTYPE,
            tarfile.BLKTYPE,
            tarfile.FIFOTYPE,
        ):
            with self.subTest(kind=kind), self.assertRaises(verify.ValidationError):
                verify.validate_tar_members([self.member("bad", kind=kind)])

    def test_duplicate_normalized_names_are_rejected(self):
        members = [self.member("./dist/vmlinux"), self.member("dist/vmlinux")]
        with self.assertRaisesRegex(verify.ValidationError, "duplicate"):
            verify.validate_tar_members(members)
        roots = [
            self.member("./", kind=tarfile.DIRTYPE),
            self.member(".", kind=tarfile.DIRTYPE),
        ]
        with self.assertRaisesRegex(verify.ValidationError, "duplicate"):
            verify.validate_tar_members(roots)

    def test_expanded_size_limit_is_enforced(self):
        members = [self.member("a", 6), self.member("b", 6)]
        with self.assertRaisesRegex(verify.ValidationError, "expanded"):
            verify.validate_tar_members(members, max_total_size=10)

    def test_member_count_and_individual_limits_are_enforced(self):
        with self.assertRaisesRegex(verify.ValidationError, "members"):
            verify.validate_tar_members(
                [self.member("a"), self.member("b")], max_members=1
            )
        with self.assertRaisesRegex(verify.ValidationError, "individual"):
            verify.validate_tar_members([self.member("a", 11)], max_file_size=10)

    def test_safe_extract_writes_only_regular_files_and_directories(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / "fixture.tar.gz"
            with tarfile.open(archive, "w:gz") as stream:
                info = self.member("./dist/vmlinux", size=4)
                stream.addfile(info, io.BytesIO(b"ELF!"))
                stream.addfile(self.member("./empty", kind=tarfile.DIRTYPE))
            output = Path(directory) / "out"
            verify.safe_extract_archive(archive, output)
            self.assertEqual((output / "dist/vmlinux").read_bytes(), b"ELF!")
            self.assertTrue((output / "empty").is_dir())


class ManifestTests(unittest.TestCase):
    def test_byte_drift_reports_concise_nested_difference(self):
        with tempfile.TemporaryDirectory() as directory:
            expected = Path(directory) / "expected.json"
            actual = Path(directory) / "actual.json"
            expected.write_text('{"layouts":{"module":{"size":16}}}\n')
            actual.write_text('{"layouts":{"module":{"size":24}}}\n')
            with self.assertRaisesRegex(
                verify.ValidationError, r"layouts\.module\.size.*16.*24"
            ):
                verify.compare_manifest_bytes(expected, actual)

    def test_structurally_equal_but_noncanonical_bytes_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            expected = Path(directory) / "expected.json"
            actual = Path(directory) / "actual.json"
            expected.write_text('{\n  "a": 1\n}\n')
            actual.write_text('{"a":1}\n')
            with self.assertRaisesRegex(verify.ValidationError, "byte-for-byte"):
                verify.compare_manifest_bytes(expected, actual)


class EvidenceTests(unittest.TestCase):
    def make_tree(self, directory, names):
        root = Path(directory)
        for name, data in names.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        return root

    def test_config_only_and_symvers_only_are_independent(self):
        with tempfile.TemporaryDirectory() as directory:
            root = self.make_tree(directory, {"config-618": b"config"})
            config, symvers = verify.find_evidence_files(root, "618")
            self.assertEqual([path.name for path in config], ["config-618"])
            self.assertEqual(symvers, [])

        with tempfile.TemporaryDirectory() as directory:
            root = self.make_tree(directory, {"sdk/Module.symvers": b"symbols"})
            config, symvers = verify.find_evidence_files(root, "618")
            self.assertEqual(config, [])
            self.assertEqual([path.name for path in symvers], ["Module.symvers"])

    def test_one_bad_duplicate_evidence_occurrence_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = self.make_tree(
                directory,
                {"one/Module.symvers": b"good", "two/Module.symvers": b"bad"},
            )
            expected = hashlib.sha256(b"good").hexdigest()
            _, paths = verify.find_evidence_files(root, "618")
            with self.assertRaisesRegex(verify.ValidationError, "two/Module.symvers"):
                verify.verify_evidence_hashes(paths, expected, "Module.symvers", root)


class ModuleEvidenceTests(unittest.TestCase):
    def test_pinned_entry_typeids_require_exact_uniform_prefix_layout(self):
        no_kcfi = {
            "init_module": {"section_offset": 0, "prefix": None},
            "cleanup_module": {"section_offset": 0, "prefix": None},
        }
        self.assertIsNone(verify.derive_pinned_kcfi_typeids(no_kcfi))

        with_kcfi = {
            "init_module": {
                "section_offset": 4,
                "prefix": bytes.fromhex("3530bb6f"),
            },
            "cleanup_module": {
                "section_offset": 4,
                "prefix": bytes.fromhex("607dc4e5"),
            },
        }
        self.assertEqual(
            verify.derive_pinned_kcfi_typeids(with_kcfi, byteorder="little"),
            {
                "cleanup_module": "0xe5c47d60",
                "init_module": "0x6fbb3035",
            },
        )

        mixed = dict(with_kcfi)
        mixed["cleanup_module"] = {"section_offset": 0, "prefix": None}
        with self.assertRaisesRegex(verify.ValidationError, "uniform"):
            verify.derive_pinned_kcfi_typeids(mixed)

        displaced = dict(with_kcfi)
        displaced["init_module"] = {
            "section_offset": 8,
            "prefix": bytes.fromhex("3530bb6f"),
        }
        with self.assertRaisesRegex(verify.ValidationError, "section offset"):
            verify.derive_pinned_kcfi_typeids(displaced)

    def test_vermagic_malformed_and_mismatch(self):
        expected = "6.18.24-android17-5 SMP preempt aarch64"
        with self.assertRaisesRegex(verify.ValidationError, "exactly one"):
            verify.parse_vermagic(b"license=GPL\0", expected)
        with self.assertRaisesRegex(verify.ValidationError, "exactly one"):
            verify.parse_vermagic(b"vermagic=a\0vermagic=b\0", expected)
        with self.assertRaisesRegex(verify.ValidationError, "mismatch"):
            verify.parse_vermagic(b"vermagic=wrong\0", expected)

    def test_linux_banner_mismatch(self):
        data = b"junk Linux version 6.18.23-wrong (builder) more"
        with self.assertRaisesRegex(verify.ValidationError, "linux_banner"):
            verify.verify_linux_banner(data, "6.18.24-right SMP aarch64")

    def test_missing_and_nonqualifying_pinned_module(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaisesRegex(verify.ValidationError, "missing pinned"):
                verify.resolve_pinned_module(root, "dist/zsmalloc.ko")
            module = root / "dist/zsmalloc.ko"
            module.parent.mkdir()
            module.write_bytes(b"not an ELF")
            with self.assertRaisesRegex(verify.ValidationError, "ELF"):
                verify.inspect_offset_module(module, "anything")

    def test_offset_checker_receives_only_pinned_ko_not_adjacent_mod_o(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            module = root / "dist/zsmalloc.ko"
            module.parent.mkdir()
            module.write_bytes(b"module")
            (root / "dist/poison.mod.o").write_bytes(b"poison")
            verifier = root / "fake-verifier.py"
            verifier.write_text(
                "#!/usr/bin/env python3\n"
                "import pathlib,sys\n"
                "files=sorted(p.name for p in pathlib.Path(sys.argv[-1]).iterdir())\n"
                "assert files == ['pinned.ko'], files\n"
                "print('[verify] object=' + str(pathlib.Path(sys.argv[-1]) / 'pinned.ko'))\n"
                "print('[verify] PASS: struct module offsets match')\n"
            )
            verifier.chmod(verifier.stat().st_mode | stat.S_IXUSR)
            output = verify.run_offset_verifier(
                "618",
                module,
                verifier,
                Path("unused-generator"),
                "unused-readelf",
            )
            self.assertIn("PASS", output)


if __name__ == "__main__":
    unittest.main()

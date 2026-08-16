#!/usr/bin/env python3
"""Build and run the freestanding profile-policy host contract test."""

import os
from pathlib import Path
import copy
import json
import shlex
import subprocess
import sys
import tempfile


TOOLS_ROOT = Path(__file__).resolve().parent
RUNTIME_ROOT = TOOLS_ROOT.parent
SOURCE_ROOT = RUNTIME_ROOT / "src"


def main():
    compiler = shlex.split(os.environ.get("CC", "cc"))
    if not compiler:
        raise RuntimeError("CC does not name a compiler")
    generator = TOOLS_ROOT / "generate-compat-table.py"
    subprocess.run([sys.executable, str(generator), "--check"], check=True)
    rendered_compat = (SOURCE_ROOT / "nvk_compat_table.inc").read_text(
        encoding="utf-8"
    )
    rendered_profile_layouts = rendered_compat.split(
        "struct neverc_krt_layout_certificate_entry", 1
    )[0]
    rendered_profiles = (SOURCE_ROOT / "nvk_profile_table.inc").read_text(
        encoding="utf-8"
    )
    for field, value in (
        ("filename_size", 32),
        ("filename_name", 0),
        ("filename_name_size", 8),
    ):
        catalog_count = len(
            json.loads(
                (RUNTIME_ROOT / "arm64/gki-profiles.json").read_text(
                    encoding="utf-8"
                )
            )["profiles"]
        )
        if rendered_profile_layouts.count(f".{field} = {value},") != catalog_count:
            raise RuntimeError(
                f"generated profile layouts lack {catalog_count} {field} facts"
            )
    expected_module_memory_facts = (
        ("module_memory_count", 1, 5),
        ("module_memory_count", 7, 3),
        ("module_memory_base", 0, 8),
        ("module_memory_size", 8, 7),
        ("module_memory_size", 12, 1),
    )
    for field, value, expected_count in expected_module_memory_facts:
        actual_count = rendered_profiles.count(f".{field} = {value},")
        if actual_count != expected_count:
            raise RuntimeError(
                f"generated profile layouts have {actual_count} {field}={value} "
                f"facts; expected {expected_count}"
            )

    with tempfile.TemporaryDirectory(prefix="neverc-profile-policy-") as tmp:
        tmp_root = Path(tmp)
        output = tmp_root / "test-profile-policy"
        command = compiler + [
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            f"-I{SOURCE_ROOT}",
            f"-I{RUNTIME_ROOT / 'include'}",
            str(TOOLS_ROOT / "test-profile-policy.c"),
            str(SOURCE_ROOT / "nvk_profile.c"),
            "-o",
            str(output),
        ]
        subprocess.run(command, check=True)
        subprocess.run([str(output)], check=True)

        include_flags = [
            f"-I{RUNTIME_ROOT / 'arm64/include'}",
            f"-I{RUNTIME_ROOT / 'include'}",
        ]
        reentrant_source = tmp_root / "reentrant-profile-config.c"
        reentrant_source.write_text(
            "#include <nvk_profile_config.h>\n"
            "#define NVK_KERNEL 612\n"
            "#include <nvkmod_version.h>\n"
            "_Static_assert(NEVERC_KRT_PROFILE_ID == 612, \"profile\");\n"
            "_Static_assert(NEVERC_KRT_MODULE_SIZE == 1600, \"layout\");\n"
            "_Static_assert(NEVERC_KRT_LINUX_AT_LEAST(6, 12, 89), "
            "\"patch floor\");\n"
            "_Static_assert(NEVERC_KRT_LINUX_BEFORE(6, 12, 90), "
            "\"patch ceiling\");\n"
            "int profile_config_is_reentrant(void) { return 0; }\n",
            encoding="utf-8",
        )
        subprocess.run(
            compiler
            + ["-std=c11", "-Wall", "-Wextra", "-U__weak"]
            + include_flags
            + ["-c", str(reentrant_source), "-o", str(tmp_root / "reentrant.o")],
            check=True,
        )

        alias_source = tmp_root / "alias-51012.c"
        alias_source.write_text(
            "#include <nvk_profile_config.h>\n"
            "#define NVK_KERNEL 51012\n"
            "#include <nvkmod_version.h>\n"
            "_Static_assert(NEVERC_KRT_PROFILE_ID == 510, \"alias\");\n"
            "_Static_assert(NEVERC_KRT_MODULE_SIZE == 1024, \"510 size\");\n"
            "_Static_assert(NEVERC_KRT_OFF_EXIT == 960, \"510 exit\");\n"
            "int alias_51012_selects_android12(void) { return 0; }\n",
            encoding="utf-8",
        )
        subprocess.run(
            compiler
            + ["-std=c11", "-Wall", "-Wextra", "-U__weak"]
            + include_flags
            + ["-c", str(alias_source), "-o", str(tmp_root / "alias-51012.o")],
            check=True,
        )

        later_alias_source = tmp_root / "alias-51012-later-version.c"
        later_alias_source.write_text(
            "#include <nvkmod_version.h>\n"
            "#define NVK_KERNEL 51012\n"
            "#undef NEVERC_KRT_KERNEL\n"
            "#define NEVERC_KRT_KERNEL 51513\n"
            "#include <nvkmod_version.h>\n"
            "_Static_assert(NVK_KERNEL == 510, \"51012 later\");\n"
            "_Static_assert(NEVERC_KRT_KERNEL == 515, \"51513 later\");\n"
            "int alias_remaps_on_later_nvkmod_version(void) { return 0; }\n",
            encoding="utf-8",
        )
        subprocess.run(
            compiler
            + ["-std=c11", "-Wall", "-Wextra", "-U__weak"]
            + include_flags
            + [
                "-c",
                str(later_alias_source),
                "-o",
                str(tmp_root / "alias-51012-later-version.o"),
            ],
            check=True,
        )

        local_family_source = tmp_root / "family-51013.c"
        local_family_source.write_text(
            "#include <nvk_profile_config.h>\n"
            "#define NVK_KERNEL 51013\n"
            "#include <nvkmod_version.h>\n"
            "_Static_assert(NEVERC_KRT_PROFILE_ID == 51013, \"51013\");\n"
            "_Static_assert(NEVERC_KRT_PROFILE_ANDROID_RELEASE == 13, \"51013 android\");\n"
            "_Static_assert(NEVERC_KRT_MODULE_SIZE == 1024, \"51013 size\");\n"
            "_Static_assert(NEVERC_KRT_OFF_EXIT == 936, \"51013 exit\");\n"
            "int family_51013_selects_android13(void) { return 0; }\n",
            encoding="utf-8",
        )
        subprocess.run(
            compiler
            + ["-std=c11", "-Wall", "-Wextra", "-U__weak"]
            + include_flags
            + [
                "-c",
                str(local_family_source),
                "-o",
                str(tmp_root / "family-51013.o"),
            ],
            check=True,
        )

        local_51514_source = tmp_root / "family-51514.c"
        local_51514_source.write_text(
            "#include <nvk_profile_config.h>\n"
            "#define NVK_KERNEL 51514\n"
            "#include <nvkmod_version.h>\n"
            "_Static_assert(NEVERC_KRT_PROFILE_ID == 51514, \"51514\");\n"
            "_Static_assert(NEVERC_KRT_PROFILE_ANDROID_RELEASE == 14, \"51514 android\");\n"
            "_Static_assert(NEVERC_KRT_MODULE_SIZE == 1024, \"51514 size\");\n"
            "_Static_assert(NEVERC_KRT_OFF_EXIT == 976, \"51514 exit\");\n"
            "int family_51514_selects_android14(void) { return 0; }\n",
            encoding="utf-8",
        )
        subprocess.run(
            compiler
            + ["-std=c11", "-Wall", "-Wextra", "-U__weak"]
            + include_flags
            + [
                "-c",
                str(local_51514_source),
                "-o",
                str(tmp_root / "family-51514.o"),
            ],
            check=True,
        )

        conflict_source = tmp_root / "conflicting-kcfi-mode.c"
        conflict_source.write_text(
            "#define NVK_KERNEL 612\n"
            "#define NEVERC_KRT_KCFI_MODE 0\n"
            "#include <nvkmod_version.h>\n",
            encoding="utf-8",
        )
        conflict = subprocess.run(
            compiler
            + ["-std=c11", "-U__weak"]
            + include_flags
            + ["-c", str(conflict_source), "-o", str(tmp_root / "conflict.o")],
            capture_output=True,
            text=True,
        )
        if conflict.returncode == 0 or "conflicts with selected profile" not in conflict.stderr:
            raise RuntimeError(
                "profile config accepted conflicting KCFI override: "
                + conflict.stderr.strip()
            )

        forbidden_overrides = (
            (
                "layout",
                "#define NEVERC_KRT_MODULE_SIZE 1024\n",
                "conflicts with selected profile",
            ),
            (
                "vermagic",
                '#define NEVERC_KRT_VERMAGIC "untrusted"\n',
                "override requires a generated profile",
            ),
        )
        for name, override, diagnostic in forbidden_overrides:
            source = tmp_root / f"conflicting-{name}-override.c"
            source.write_text(
                "#define NVK_KERNEL 612\n"
                + override
                + "#include <nvkmod_version.h>\n",
                encoding="utf-8",
            )
            result = subprocess.run(
                compiler
                + ["-std=c11", "-U__weak"]
                + include_flags
                + ["-c", str(source), "-o", str(tmp_root / f"{name}.o")],
                capture_output=True,
                text=True,
            )
            if result.returncode == 0 or diagnostic not in result.stderr:
                raise RuntimeError(
                    f"profile config accepted conflicting {name} override: "
                    + result.stderr.strip()
                )

        queried = subprocess.run(
            [sys.executable, str(generator), "--query-profile", "612"],
            capture_output=True,
            text=True,
            check=True,
        )
        query_contract = json.loads(queried.stdout)
        if query_contract["legacy_id"] != 612 or query_contract[
            "module_size"
        ] != 1600:
            raise RuntimeError("profile query returned the wrong contract")
        alias_query = json.loads(
            subprocess.run(
                [sys.executable, str(generator), "--query-profile", "51012"],
                capture_output=True,
                text=True,
                check=True,
            ).stdout
        )
        if alias_query["legacy_id"] != 510 or alias_query["module_exit_offset"] != 960:
            raise RuntimeError("alias profile query did not remap to 510")
        local_query = json.loads(
            subprocess.run(
                [sys.executable, str(generator), "--query-profile", "51013"],
                capture_output=True,
                text=True,
                check=True,
            ).stdout
        )
        if (
            local_query["legacy_id"] != 51013
            or local_query["module_exit_offset"] != 936
        ):
            raise RuntimeError("51013 profile query returned the wrong contract")
        local_51514_query = json.loads(
            subprocess.run(
                [sys.executable, str(generator), "--query-profile", "51514"],
                capture_output=True,
                text=True,
                check=True,
            ).stdout
        )
        if (
            local_51514_query["legacy_id"] != 51514
            or local_51514_query["module_exit_offset"] != 976
            or local_51514_query["module_size"] != 1024
        ):
            raise RuntimeError("51514 profile query returned the wrong contract")
        workflow = (
            RUNTIME_ROOT.parents[2] / ".github/workflows/build-gki-kernels.yml"
        ).read_text(encoding="utf-8")
        catalog_ids = {
            str(profile["legacy_id"])
            for profile in json.loads(
                (RUNTIME_ROOT / "arm64/gki-profiles.json").read_text(
                    encoding="utf-8"
                )
            )["profiles"]
        }
        missing_matrix = [
            profile_id
            for profile_id in sorted(catalog_ids, key=int)
            if f'"key":"{profile_id}"' not in workflow
        ]
        if missing_matrix:
            raise RuntimeError(
                "build-gki-kernels matrix is missing catalog profiles: "
                + ", ".join(missing_matrix)
            )
        if '"key":"51514"' in workflow:
            if "--lto=thin" not in workflow:
                raise RuntimeError(
                    "android14-5.15 must build with ThinLTO so classic CFI "
                    "runtime symbols remain in the official ksymtab check"
                )
            if "--nokmi_symbol_list_strict_mode" in workflow:
                raise RuntimeError(
                    "android14-5.15 must keep official kmi_symbol_list_strict_mode"
                )
        watch_workflow = (
            RUNTIME_ROOT.parents[2] / ".github/workflows/watch-gki-updates.yml"
        ).read_text(encoding="utf-8")
        if "GKI_WATCH_DISCORD_WEBHOOK_URL" not in watch_workflow:
            raise RuntimeError(
                "watch-gki-updates must read GKI_WATCH_DISCORD_WEBHOOK_URL"
            )
        if "gki-profiles.json" not in watch_workflow:
            raise RuntimeError(
                "watch-gki-updates must probe the profile catalog"
            )
        unknown_query = subprocess.run(
            [sys.executable, str(generator), "--query-profile", "999"],
            capture_output=True,
            text=True,
        )
        if (
            unknown_query.returncode == 0
            or "unsupported profile ID 999" not in unknown_query.stderr
        ):
            raise RuntimeError(
                "profile query accepted an unknown ID: "
                + unknown_query.stderr.strip()
            )

        catalog_path = RUNTIME_ROOT / "arm64/gki-profiles.json"
        catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
        certificate_path = (
            RUNTIME_ROOT / "arm64/gki-layout-certificates.json"
        )
        certificates = json.loads(
            certificate_path.read_text(encoding="utf-8")
        )
        live_certificate = None
        live_certificate_index = None
        for index, candidate in enumerate(certificates["certificates"]):
            if candidate.get("raw_btf", {}).get("sha256") == (
                "ae81b5a86e938c2d2db08e4c78c712143a61c1d823f96383a019b86e9e8b2e79"
            ):
                live_certificate = candidate
                live_certificate_index = index
                break
        if live_certificate is None:
            raise RuntimeError(
                "live layout certificate lacks the original 6.12 BTF identity"
            )
        profiles_by_id = {
            profile["legacy_id"]: profile for profile in catalog["profiles"]
        }
        private_certificate_fields = (
            "dir_context",
            "filename_name",
            "inode_times",
            "path_inode",
            "task_ref",
            "task_threads",
            "task_user_state",
            "task_walk",
            "user_ptmap",
        )

        def certificate_is_leftover(certificate):
            profile = profiles_by_id[certificate["profile_id"]]
            identity = certificate["identity"]
            return (
                identity["android_release"] != profile["android_release"]
                or identity["kmi_generation"] != profile["kmi_generation"]
            )

        leftover_count = 0
        for certificate in certificates["certificates"]:
            if not certificate_is_leftover(certificate):
                continue
            leftover_count += 1
            missing = [] if "runtime_layout" in certificate else [
                field for field in private_certificate_fields
                if field not in certificate
            ]
            if missing:
                raise RuntimeError(
                    "leftover layout certificate lacks private fields: "
                    + ", ".join(missing)
                )
        if leftover_count == 0:
            raise RuntimeError("catalog has no leftover Android/KMI certificates")

        same_generation_certificate = None
        same_generation_index = None
        for index, candidate in enumerate(certificates["certificates"]):
            if certificate_is_leftover(candidate):
                continue
            if (
                "filename_name" not in candidate
                and "runtime_layout" not in candidate
            ):
                continue
            same_generation_certificate = candidate
            same_generation_index = index
            break
        if same_generation_certificate is None:
            raise RuntimeError(
                "catalog has no same-generation filename certificate"
            )
        if not certificate_is_leftover(live_certificate):
            raise RuntimeError(
                "6.12 live certificate is no longer a leftover Android/KMI record"
            )
        user_ptmap = live_certificate.get("user_ptmap")
        if not isinstance(user_ptmap, dict):
            raise RuntimeError(
                "live layout certificate lacks opaque user_ptmap evidence"
            )
        expected_user_ptmap = {
            "geometry": {
                "page_shift": 12,
                "va_bits": 39,
                "pa_bits": 48,
                "pgtable_levels": 3,
                "pgd_shift": 30,
                "pmd_shift": 21,
                "pte_shift": 12,
                "index_bits": 9,
                "contiguous_bit": 52,
                "contiguous_entries": 16,
                "descriptor_address_mask": 0x0003FFFFFFFFF000,
                "physical_address_mask": 0x0000FFFFFFFFFFFF,
                "physical_page_mask": 0x0000FFFFFFFFF000,
                "tlbi_all_asid": 1,
            },
            "mm_struct": {
                "size": 1216,
                "members": {
                    "mm_count": 0,
                    "pgd": 104,
                    "page_table_lock": 132,
                    "mmap_lock": 136,
                },
                "member_sizes": {
                    "mm_count": 4,
                    "pgd": 8,
                    "page_table_lock": 4,
                    "mmap_lock": 64,
                },
            },
            "vm_area_struct": {
                "size": 256,
                "members": {"vm_start": 0, "vm_end": 8},
                "member_sizes": {"vm_start": 8, "vm_end": 8},
            },
            "pt_regs": {
                "size": 336,
                "members": {"regs": 0, "sp": 248, "pc": 256, "pstate": 264},
                "member_sizes": {"regs": 248, "sp": 8, "pc": 8, "pstate": 8},
            },
        }
        if user_ptmap != expected_user_ptmap:
            raise RuntimeError(
                "live user_ptmap evidence does not match raw device BTF"
            )

        def expect_catalog_error(name, document, diagnostic, extra_args=None):
            candidate = tmp_root / f"{name}.json"
            candidate.write_text(
                json.dumps(document, indent=2) + "\n", encoding="utf-8"
            )
            result = subprocess.run(
                [
                    sys.executable,
                    str(generator),
                    "--catalog",
                    str(candidate),
                    "--profile-ids-header",
                    str(tmp_root / f"{name}-ids.h"),
                    "--profile-header",
                    str(tmp_root / f"{name}-profile.h"),
                    "--profile-table",
                    str(tmp_root / f"{name}-profile.inc"),
                    "--output",
                    str(tmp_root / f"{name}-compat.inc"),
                    *(extra_args or []),
                ],
                capture_output=True,
                text=True,
            )
            if result.returncode == 0:
                raise RuntimeError(f"generator accepted invalid catalog: {name}")
            if diagnostic not in result.stderr:
                raise RuntimeError(
                    f"unexpected {name} diagnostic: " + result.stderr.strip()
                )

        def expect_certificate_error(name, document, diagnostic):
            candidate = tmp_root / f"{name}.json"
            candidate.write_text(
                json.dumps(document, indent=2) + "\n", encoding="utf-8"
            )
            result = subprocess.run(
                [
                    sys.executable,
                    str(generator),
                    "--layout-certificates",
                    str(candidate),
                    "--profile-ids-header",
                    str(tmp_root / f"{name}-ids.h"),
                    "--profile-header",
                    str(tmp_root / f"{name}-profile.h"),
                    "--profile-table",
                    str(tmp_root / f"{name}-profile.inc"),
                    "--output",
                    str(tmp_root / f"{name}-compat.inc"),
                ],
                capture_output=True,
                text=True,
            )
            if result.returncode == 0:
                raise RuntimeError(
                    f"generator accepted invalid certificate: {name}"
                )
            if diagnostic not in result.stderr:
                raise RuntimeError(
                    f"unexpected {name} diagnostic: " + result.stderr.strip()
                )

        def render_certificates(name, document):
            candidate = tmp_root / f"{name}.json"
            output = tmp_root / f"{name}-compat.inc"
            candidate.write_text(
                json.dumps(document, indent=2) + "\n", encoding="utf-8"
            )
            result = subprocess.run(
                [
                    sys.executable,
                    str(generator),
                    "--layout-certificates",
                    str(candidate),
                    "--profile-ids-header",
                    str(tmp_root / f"{name}-ids.h"),
                    "--profile-header",
                    str(tmp_root / f"{name}-profile.h"),
                    "--profile-table",
                    str(tmp_root / f"{name}-profile.inc"),
                    "--output",
                    str(output),
                ],
                capture_output=True,
                text=True,
            )
            if result.returncode != 0:
                raise RuntimeError(
                    f"generator rejected valid certificate {name}: "
                    + result.stderr.strip()
                )
            return output.read_text(encoding="utf-8")

        mismatched = copy.deepcopy(catalog)
        mismatched["profiles"].pop()
        expect_catalog_error(
            "mismatched-profile-set",
            mismatched,
            "catalog and manifest profile sets differ",
        )

        release_lock_path = RUNTIME_ROOT / "arm64/gki-release.json"
        mismatched_release = json.loads(
            release_lock_path.read_text(encoding="utf-8")
        )
        first_lock = next(iter(mismatched_release["profiles"]))
        mismatched_release["profiles"]["999"] = dict(
            mismatched_release["profiles"][first_lock]
        )
        mismatched_release_path = tmp_root / "mismatched-release-lock.json"
        mismatched_release_path.write_text(
            json.dumps(mismatched_release, indent=2) + "\n", encoding="utf-8"
        )
        release_result = subprocess.run(
            [
                sys.executable,
                str(generator),
                "--release-lock",
                str(mismatched_release_path),
                "--profile-ids-header",
                str(tmp_root / "mismatched-release-ids.h"),
                "--profile-header",
                str(tmp_root / "mismatched-release-profile.h"),
                "--profile-table",
                str(tmp_root / "mismatched-release-profile.inc"),
                "--output",
                str(tmp_root / "mismatched-release-compat.inc"),
            ],
            capture_output=True,
            text=True,
        )
        if (
            release_result.returncode == 0
            or "release-lock has profiles missing from the catalog"
            not in release_result.stderr
        ):
            raise RuntimeError(
                "generator accepted mismatched release-lock profile set: "
                + release_result.stderr.strip()
            )

        zero_id = copy.deepcopy(catalog)
        zero_id["profiles"][0]["legacy_id"] = 0
        expect_catalog_error("zero-profile-id", zero_id, "non-zero uint32")

        oversized_id = copy.deepcopy(catalog)
        oversized_id["profiles"][0]["legacy_id"] = 1 << 32
        expect_catalog_error(
            "oversized-profile-id", oversized_id, "non-zero uint32"
        )

        oversized_identity = copy.deepcopy(catalog)
        oversized_identity["profiles"][0]["linux_major"] = 1 << 32
        expect_catalog_error(
            "oversized-identity", oversized_identity, "must be a uint32"
        )

        invalid_page_size = copy.deepcopy(catalog)
        invalid_page_size["profiles"][0]["page_shift"] = 13
        expect_catalog_error(
            "invalid-page-shift", invalid_page_size, "unsupported page_shift"
        )

        invalid_capability = copy.deepcopy(catalog)
        invalid_capability["profiles"][0]["capabilities"][
            "filldir_abi"
        ] = "nearest_version"
        expect_catalog_error(
            "invalid-capability", invalid_capability, "invalid filldir_abi"
        )

        unsafe_ftrace_regs = copy.deepcopy(catalog)
        ftrace_regs_profile = next(
            profile
            for profile in unsafe_ftrace_regs["profiles"]
            if profile["capabilities"]["ftrace_callback_abi"]
            == "ftrace_regs"
        )
        ftrace_regs_profile["capabilities"]["ftrace_registration_api"] = True
        expect_catalog_error(
            "unsafe-ftrace-regs-registration",
            unsafe_ftrace_regs,
            "requires a modeled ftrace_regs-to-pt_regs layout",
        )

        missing_vermagic = copy.deepcopy(catalog)
        for profile in missing_vermagic["profiles"]:
            if profile.get("legacy_id") in (51013, 51514):
                profile.pop("vermagic", None)
        unlocked_local = json.loads(release_lock_path.read_text(encoding="utf-8"))
        unlocked_local["profiles"].pop("51013", None)
        unlocked_local["profiles"].pop("51514", None)
        unlocked_local_path = tmp_root / "unlocked-local-release.json"
        unlocked_local_path.write_text(
            json.dumps(unlocked_local, indent=2) + "\n", encoding="utf-8"
        )
        expect_catalog_error(
            "missing-local-vermagic",
            missing_vermagic,
            "needs a catalog vermagic",
            extra_args=["--release-lock", str(unlocked_local_path)],
        )

        invalid_certificate_schema = copy.deepcopy(certificates)
        invalid_certificate_schema["certificates"][live_certificate_index]["unexpected"] = True
        expect_certificate_error(
            "invalid-certificate-schema",
            invalid_certificate_schema,
            "certificate keys do not match schema",
        )

        invalid_certificate_hash = copy.deepcopy(certificates)
        invalid_certificate_hash["certificates"][live_certificate_index]["raw_btf"][
            "sha256"
        ] = "AE81"
        expect_certificate_error(
            "invalid-certificate-hash",
            invalid_certificate_hash,
            "raw_btf.sha256 must be lowercase SHA-256",
        )

        invalid_certificate_token = copy.deepcopy(certificates)
        invalid_certificate_token["certificates"][live_certificate_index]["release_token"] = (
            "6.12.39-android16-5-vendor-4k"
        )
        expect_certificate_error(
            "invalid-certificate-token",
            invalid_certificate_token,
            "release_token identity mismatch",
        )

        invalid_certificate_identity = copy.deepcopy(certificates)
        invalid_certificate_identity["certificates"][live_certificate_index]["identity"][
            "linux_major"
        ] = 5
        invalid_certificate_identity["certificates"][live_certificate_index]["identity"][
            "linux_minor"
        ] = 15
        invalid_certificate_identity["certificates"][live_certificate_index]["identity"][
            "linux_patch"
        ] = 164
        invalid_certificate_identity["certificates"][live_certificate_index]["identity"][
            "android_release"
        ] = 14
        invalid_certificate_identity["certificates"][live_certificate_index]["identity"][
            "kmi_generation"
        ] = 11
        invalid_certificate_identity["certificates"][live_certificate_index]["release_token"] = (
            "5.15.164-android14-11-vendor"
        )
        expect_certificate_error(
            "invalid-certificate-identity",
            invalid_certificate_identity,
            "identity is outside profile family",
        )

        invalid_certificate_layout = copy.deepcopy(certificates)
        invalid_certificate_layout["certificates"][live_certificate_index]["dir_context"][
            "members"
        ]["pos"] = 16
        expect_certificate_error(
            "invalid-certificate-layout",
            invalid_certificate_layout,
            "dir_context.pos is out of bounds",
        )

        invalid_certificate_abi = copy.deepcopy(certificates)
        invalid_certificate_abi["certificates"][live_certificate_index]["filldir_abi"] = (
            "returns_int"
        )
        expect_certificate_error(
            "invalid-certificate-abi",
            invalid_certificate_abi,
            "filldir_abi mismatches profile family",
        )

        invalid_inode_width = copy.deepcopy(certificates)
        invalid_inode_width["certificates"][live_certificate_index]["inode_times"][
            "member_sizes"
        ]["atime_sec"] = 4
        expect_certificate_error(
            "invalid-inode-times-width",
            invalid_inode_width,
            "unsupported inode_times.atime_sec field width",
        )

        overlapping_inode_fields = copy.deepcopy(certificates)
        overlapping_inode_fields["certificates"][live_certificate_index]["inode_times"][
            "members"
        ]["mtime_sec"] = overlapping_inode_fields["certificates"][live_certificate_index][
            "inode_times"
        ]["members"]["atime_sec"]
        expect_certificate_error(
            "overlapping-inode-times",
            overlapping_inode_fields,
            "inode_times fields overlap",
        )

        invalid_path_bounds = copy.deepcopy(certificates)
        invalid_path_bounds["certificates"][live_certificate_index]["path_inode"]["path"][
            "members"
        ]["dentry"] = 12
        expect_certificate_error(
            "invalid-path-inode-bounds",
            invalid_path_bounds,
            "path_inode.path.dentry is out of bounds",
        )

        invalid_filename_width = copy.deepcopy(certificates)
        invalid_filename_width["certificates"][live_certificate_index]["filename_name"][
            "member_sizes"
        ]["name"] = 4
        expect_certificate_error(
            "invalid-filename-name-width",
            invalid_filename_width,
            "unsupported filename_name.name field width",
        )

        invalid_filename_bounds = copy.deepcopy(certificates)
        invalid_filename_bounds["certificates"][live_certificate_index]["filename_name"][
            "members"
        ]["name"] = 25
        expect_certificate_error(
            "invalid-filename-name-bounds",
        invalid_filename_bounds,
            "filename_name.name is out of bounds",
        )

        invalid_task_thread_width = copy.deepcopy(certificates)
        invalid_task_thread_width["certificates"][live_certificate_index]["task_threads"][
            "task_struct"
        ]["member_sizes"]["thread_node"] = 8
        expect_certificate_error(
            "invalid-task-thread-width",
            invalid_task_thread_width,
            "invalid task_threads.task_struct.thread_node layout value",
        )

        invalid_task_thread_bounds = copy.deepcopy(certificates)
        invalid_task_thread_bounds["certificates"][live_certificate_index]["task_threads"][
            "signal_struct"
        ]["members"]["thread_head"] = 1112
        expect_certificate_error(
            "invalid-task-thread-bounds",
            invalid_task_thread_bounds,
            "task_threads.signal_struct.thread_head is out of bounds",
        )

        invalid_task_walk_width = copy.deepcopy(certificates)
        invalid_task_walk_width["certificates"][live_certificate_index]["task_walk"][
            "task_struct"
        ]["member_sizes"]["comm"] = 8
        expect_certificate_error(
            "invalid-task-walk-width",
            invalid_task_walk_width,
            "invalid task_walk.task_struct.comm layout value",
        )

        invalid_task_walk_cred_bounds = copy.deepcopy(certificates)
        invalid_task_walk_cred_bounds["certificates"][live_certificate_index]["task_walk"][
            "cred"
        ]["members"]["fsgid"] = 184
        expect_certificate_error(
            "invalid-task-walk-cred-bounds",
            invalid_task_walk_cred_bounds,
            "task_walk.cred.fsgid is out of bounds",
        )

        invalid_task_ref_width = copy.deepcopy(certificates)
        invalid_task_ref_width["certificates"][live_certificate_index]["task_ref"][
            "task_struct"
        ]["member_sizes"]["usage"] = 8
        expect_certificate_error(
            "invalid-task-ref-width",
            invalid_task_ref_width,
            "invalid task_ref.task_struct.usage layout value",
        )

        invalid_task_user_state_regs_width = copy.deepcopy(certificates)
        invalid_task_user_state_regs_width["certificates"][live_certificate_index][
            "task_user_state"
        ]["pt_regs"]["member_sizes"]["pc"] = 4
        expect_certificate_error(
            "invalid-task-user-state-regs-width",
            invalid_task_user_state_regs_width,
            "invalid task_user_state.pt_regs.pc layout value",
        )

        inconsistent_pt_regs_views = copy.deepcopy(certificates)
        inconsistent_pt_regs_views["certificates"][live_certificate_index]["task_user_state"][
            "pt_regs"
        ]["members"]["pc"] = 272
        expect_certificate_error(
            "inconsistent-pt-regs-views",
            inconsistent_pt_regs_views,
            "pt_regs certificate views disagree",
        )

        invalid_user_ptmap_mm_width = copy.deepcopy(certificates)
        invalid_user_ptmap_mm_width["certificates"][live_certificate_index]["user_ptmap"][
            "mm_struct"
        ]["member_sizes"]["pgd"] = 4
        expect_certificate_error(
            "invalid-user-ptmap-mm-width",
            invalid_user_ptmap_mm_width,
            "invalid user_ptmap.mm_struct.pgd layout value",
        )

        invalid_user_ptmap_regs_overlap = copy.deepcopy(certificates)
        invalid_user_ptmap_regs_overlap["certificates"][live_certificate_index]["user_ptmap"][
            "pt_regs"
        ]["members"]["sp"] = 240
        expect_certificate_error(
            "invalid-user-ptmap-regs-overlap",
            invalid_user_ptmap_regs_overlap,
            "user_ptmap.pt_regs fields overlap",
        )

        invalid_user_ptmap_vma_bounds = copy.deepcopy(certificates)
        invalid_user_ptmap_vma_bounds["certificates"][live_certificate_index]["user_ptmap"][
            "vm_area_struct"
        ]["members"]["vm_end"] = 256
        expect_certificate_error(
            "invalid-user-ptmap-vma-bounds",
            invalid_user_ptmap_vma_bounds,
            "user_ptmap.vm_area_struct.vm_end is out of bounds",
        )

        invalid_user_ptmap_geometry = copy.deepcopy(certificates)
        invalid_user_ptmap_geometry["certificates"][live_certificate_index]["user_ptmap"][
            "geometry"
        ]["va_bits"] = 48
        expect_certificate_error(
            "invalid-user-ptmap-geometry",
            invalid_user_ptmap_geometry,
            "unsupported user_ptmap geometry",
        )

        invalid_user_ptmap_descriptor_mask = copy.deepcopy(certificates)
        invalid_user_ptmap_descriptor_mask["certificates"][live_certificate_index]["user_ptmap"][
            "geometry"
        ]["descriptor_address_mask"] = 0x0000FFFFFFFFF000
        expect_certificate_error(
            "invalid-user-ptmap-descriptor-mask",
            invalid_user_ptmap_descriptor_mask,
            "user_ptmap geometry mismatches profile family",
        )

        invalid_user_ptmap_mm_count_width = copy.deepcopy(certificates)
        invalid_user_ptmap_mm_count_width["certificates"][live_certificate_index]["user_ptmap"][
            "mm_struct"
        ]["member_sizes"]["mm_count"] = 8
        expect_certificate_error(
            "invalid-user-ptmap-mm-count-width",
            invalid_user_ptmap_mm_count_width,
            "invalid user_ptmap.mm_struct.mm_count layout value",
        )

        leftover_incomplete = copy.deepcopy(certificates)
        leftover_incomplete["certificates"][live_certificate_index].pop(
            "inode_times"
        )
        leftover_rendered = render_certificates(
            "leftover-partial-fields", leftover_incomplete
        )
        token_anchor = leftover_rendered.index(
            f'.release_token = "{live_certificate["release_token"]}"'
        )
        record_start = leftover_rendered.index("\t\t.field_bits =", token_anchor)
        record_end = leftover_rendered.index("\n", record_start)
        if "NEVERC_KRT_LAYOUT_CERT_INODE_TIMES" in leftover_rendered[
            record_start:record_end
        ]:
            raise RuntimeError(
                "partial leftover certificate published an absent inode bit"
            )

        def filename_only_from_full(certificate):
            fields = certificate["runtime_layout"]["fields"]
            result = {
                key: copy.deepcopy(certificate[key])
                for key in (
                    "identity", "profile_id", "release_token"
                )
            }
            for evidence_key in ("raw_btf", "raw_dwarf"):
                if evidence_key in certificate:
                    result[evidence_key] = copy.deepcopy(
                        certificate[evidence_key]
                    )
            result["filename_name"] = {
                "size": fields["filename_size"],
                "members": {"name": fields["filename_name"]},
                "member_sizes": {
                    "name": fields["filename_name_size"]
                },
            }
            return result

        independent_fields = copy.deepcopy(certificates)
        independent_fields["certificates"][same_generation_index] = (
            filename_only_from_full(same_generation_certificate)
        )
        independent_path = tmp_root / "independent-field-certificate.json"
        independent_output = tmp_root / "independent-field-compat.inc"
        independent_path.write_text(
            json.dumps(independent_fields, indent=2) + "\n", encoding="utf-8"
        )
        independent_result = subprocess.run(
            [
                sys.executable,
                str(generator),
                "--layout-certificates",
                str(independent_path),
                "--profile-ids-header",
                str(tmp_root / "independent-field-ids.h"),
                "--profile-header",
                str(tmp_root / "independent-field-profile.h"),
                "--profile-table",
                str(tmp_root / "independent-field-profile.inc"),
                "--output",
                str(independent_output),
            ],
            capture_output=True,
            text=True,
        )
        if independent_result.returncode != 0:
            raise RuntimeError(
                "generator rejected independent field certificate: "
                + independent_result.stderr.strip()
            )
        independent_rendered = independent_output.read_text(encoding="utf-8")
        token_anchor = independent_rendered.index(
            f'.release_token = "{same_generation_certificate["release_token"]}"'
        )
        record_start = independent_rendered.index(
            "\t\t.field_bits =", token_anchor
        )
        record_end = independent_rendered.index("\n", record_start)
        if "NEVERC_KRT_LAYOUT_CERT_INODE_TIMES" in independent_rendered[
            record_start:record_end
        ]:
            raise RuntimeError(
                "generator published an absent inode_times certificate bit"
            )

        filename_only_fields = copy.deepcopy(certificates)
        filename_only_fields["certificates"][same_generation_index] = (
            filename_only_from_full(same_generation_certificate)
        )
        filename_only_path = tmp_root / "filename-only-certificate.json"
        filename_only_output = tmp_root / "filename-only-compat.inc"
        filename_only_path.write_text(
            json.dumps(filename_only_fields, indent=2) + "\n",
            encoding="utf-8",
        )
        filename_only_result = subprocess.run(
            [
                sys.executable,
                str(generator),
                "--layout-certificates",
                str(filename_only_path),
                "--profile-ids-header",
                str(tmp_root / "filename-only-ids.h"),
                "--profile-header",
                str(tmp_root / "filename-only-profile.h"),
                "--profile-table",
                str(tmp_root / "filename-only-profile.inc"),
                "--output",
                str(filename_only_output),
            ],
            capture_output=True,
            text=True,
        )
        if filename_only_result.returncode != 0:
            raise RuntimeError(
                "generator rejected filename-only certificate: "
                + filename_only_result.stderr.strip()
            )
        filename_only_rendered = filename_only_output.read_text(
            encoding="utf-8"
        )
        token_anchor = filename_only_rendered.index(
            f'.release_token = "{same_generation_certificate["release_token"]}"'
        )
        record_start = filename_only_rendered.index(
            "\t\t.field_bits =", token_anchor
        )
        record_end = filename_only_rendered.index("\n", record_start)
        if filename_only_rendered[record_start:record_end] != (
            "\t\t.field_bits = NEVERC_KRT_LAYOUT_CERT_FILENAME_NAME,"
        ):
            raise RuntimeError(
                "generator did not publish an independent filename bit: "
                + filename_only_rendered[record_start:record_end]
            )

        duplicate_identity = copy.deepcopy(catalog)
        duplicate_identity["profiles"][1].pop("vermagic", None)
        for field in (
            "linux_major",
            "linux_minor",
            "linux_patch",
            "android_release",
            "kmi_generation",
            "page_shift",
        ):
            duplicate_identity["profiles"][1][field] = duplicate_identity[
                "profiles"
            ][0][field]
        expect_catalog_error(
            "duplicate-identity", duplicate_identity, "duplicate semantic identity"
        )

        fixture_root = tmp_root / "policy-fixture"
        compiler_source = (
            fixture_root / "neverc/lib/Compiler/ProfileLeak.cpp"
        )
        plugin_source = (
            fixture_root / "neverc/lib/Plugin/Link/ProfileLeak.cpp"
        )
        runtime_source = (
            fixture_root / "runtime/android/kernel/src/profile_leak.c"
        )
        sdk_header = (
            fixture_root
            / "runtime/android/kernel/arm64/include/linux/profile_leak.h"
        )
        runtime_tool = (
            fixture_root / "runtime/android/kernel/tools/profile_order.py"
        )
        compiler_source.parent.mkdir(parents=True)
        plugin_source.parent.mkdir(parents=True)
        runtime_source.parent.mkdir(parents=True)
        sdk_header.parent.mkdir(parents=True)
        runtime_tool.parent.mkdir(parents=True)
        leaked_id = catalog["profiles"][0]["legacy_id"]
        compiler_source.write_text(
            f"unsigned profile = {leaked_id};\n", encoding="utf-8"
        )
        plugin_source.write_text(
            f"unsigned profile = {leaked_id};\n", encoding="utf-8"
        )
        runtime_source.write_text(
            "unsigned profile = NVK_KERNEL;\n", encoding="utf-8"
        )
        sdk_header.write_text(
            "#if NEVERC_KRT_KERNEL >= 612\n#endif\n", encoding="utf-8"
        )
        runtime_tool.write_text(
            "if manifest['profile'] >= 612:\n    pass\n", encoding="utf-8"
        )
        checker_command = [
            sys.executable,
            str(TOOLS_ROOT / "check-profile-policy.py"),
            "--repo-root",
            str(fixture_root),
            "--runtime-root",
            str(fixture_root / "runtime/android/kernel"),
            "--catalog",
            str(catalog_path),
        ]
        rejected = subprocess.run(
            checker_command, capture_output=True, text=True
        )
        if rejected.returncode == 0:
            raise RuntimeError("profile policy checker accepted fixture violations")
        if "5 violation(s)" not in rejected.stderr:
            raise RuntimeError(
                "profile policy checker missed fixture violations: "
                + rejected.stderr.strip()
            )
        compiler_source.write_text("unsigned profile;\n", encoding="utf-8")
        plugin_source.write_text("unsigned profile;\n", encoding="utf-8")
        runtime_source.write_text("unsigned profile;\n", encoding="utf-8")
        sdk_header.write_text("#if NEVERC_KRT_LINUX_AT_LEAST(6, 12, 0)\n#endif\n")
        runtime_tool.write_text("profile = manifest['profile']\n", encoding="utf-8")
        subprocess.run(checker_command, check=True)
    print("test-profile-policy: OK")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)

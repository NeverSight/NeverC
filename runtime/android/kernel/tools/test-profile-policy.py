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

        def expect_catalog_error(name, document, diagnostic):
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
        mismatched_release["profiles"].pop(
            next(iter(mismatched_release["profiles"]))
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
            or "catalog and release-lock profile sets differ"
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

        duplicate_identity = copy.deepcopy(catalog)
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

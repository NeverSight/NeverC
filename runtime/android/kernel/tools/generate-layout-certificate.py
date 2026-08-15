#!/usr/bin/env python3
"""Emit a compatible-identity layout certificate from a live GKI vmlinux.

Family manifests already cover EXACT and same-Android-generation COMPAT.
Certificates overlay measured offsets for a live identity, including a later
patch of the same Android/KMI.  A different Android generation that also
changes loader-visible struct module / vermagic is its own compile-time
family (51013, 51514) and must not be certified as a leftover overlay on
510/515.  COMPAT uses the family layout from series + Android
generation + page; a certificate may overlay measured offsets.
"""

import argparse
import json
import mmap
from pathlib import Path
import re
import sys

from elftools.elf.elffile import ELFFile


TOOLS_ROOT = Path(__file__).resolve().parent
RUNTIME_ROOT = TOOLS_ROOT.parent
DEFAULT_CATALOG = RUNTIME_ROOT / "arm64/gki-profiles.json"
DEFAULT_MANIFEST_ROOT = RUNTIME_ROOT / "arm64/gki-manifests"
DEFAULT_CERTIFICATES = RUNTIME_ROOT / "arm64/gki-layout-certificates.json"

LINUX_RELEASE_RE = re.compile(rb"Linux version ([0-9][^ \x00]*)")
SHA256_HEX = re.compile(r"^[0-9a-f]{64}$")


def load_tool(name, filename):
    import importlib.util

    spec = importlib.util.spec_from_file_location(name, TOOLS_ROOT / filename)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {filename}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


COMPAT = load_tool("nvk_generate_compat_table", "generate-compat-table.py")
MANIFEST = load_tool("nvk_generate_gki_manifest", "generate-gki-manifest.py")
EXTRACT = load_tool("nvk_extract_btf_layouts", "extract-btf-layouts.py")
CERTIFICATE_STRUCTURES = tuple(COMPAT.neverc_read_members_by_struct())


def narrow_layout(layout, members):
    missing = sorted(name for name in members if name not in layout["members"]
                     or name not in layout["member_sizes"])
    if missing:
        raise ValueError(
            "layout is missing required members: " + ", ".join(missing)
        )
    return {
        "size": layout["size"],
        "members": {name: layout["members"][name] for name in sorted(members)},
        "member_sizes": {
            name: layout["member_sizes"][name] for name in sorted(members)
        },
    }


def identity_from_token(release_token, page_shift):
    match = COMPAT.RELEASE_TOKEN_IDENTITY.fullmatch(release_token)
    if match is None:
        raise ValueError(f"invalid release_token: {release_token}")
    return {
        "linux_major": int(match.group("major")),
        "linux_minor": int(match.group("minor")),
        "linux_patch": int(match.group("patch")),
        "android_release": int(match.group("android")),
        "kmi_generation": int(match.group("kmi")),
        "page_shift": page_shift,
    }


def release_token_from_vmlinux_bytes(image):
    """Return the one release token measured from a vmlinux banner."""
    try:
        tokens = sorted({
            match.decode("ascii", errors="strict")
            for match in LINUX_RELEASE_RE.findall(image)
        })
    except UnicodeDecodeError as error:
        raise ValueError("vmlinux release token is not ASCII") from error
    if len(tokens) != 1:
        raise ValueError(
            "vmlinux must contain exactly one Linux release token; "
            f"found {tokens}"
        )
    return tokens[0]


def release_token_from_vmlinux(vmlinux):
    with vmlinux.open("rb") as stream:
        with mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as image:
            return release_token_from_vmlinux_bytes(image)


def assert_expected_release_token(measured, expected):
    """Treat a caller token as an assertion, never as identity evidence."""
    if expected is not None and measured != expected:
        raise ValueError(
            f"release token mismatch: vmlinux has {measured!r}, "
            f"expected {expected!r}"
        )
    return measured


def layout_evidence(vmlinux):
    """Pin the live layout blob: BTF when present, otherwise DWARF."""
    with vmlinux.open("rb") as stream:
        elf = ELFFile(stream)
        btf = elf.get_section_by_name(".BTF")
        if btf is not None:
            return "raw_btf", {
                "sha256": MANIFEST.elf_section_sha256(btf),
                "size": int(btf["sh_size"]),
            }

    evidence = MANIFEST.elf_evidence(vmlinux)
    if evidence["layout_format"] != "DWARF":
        raise ValueError(f"{vmlinux}: ELF has neither .BTF nor DWARF evidence")
    size = 0
    with vmlinux.open("rb") as stream:
        elf = ELFFile(stream)
        for section_name in (".debug_info", ".debug_abbrev", ".debug_str"):
            section = elf.get_section_by_name(section_name)
            if section is not None:
                size += int(section["sh_size"])
    if size <= 0:
        raise ValueError(f"{vmlinux}: DWARF evidence sections are empty")
    return "raw_dwarf", {
        "sha256": evidence["layout_sha256"],
        "size": size,
    }


def build_certificate(profile, manifest, release_token, layouts, evidence_name,
                      evidence, catalog_profiles=None):
    """Assemble one certificate object from already-extracted layouts."""
    page_shift = manifest["config"]["PAGE_SHIFT"]
    identity = identity_from_token(release_token, page_shift)
    family = (
        profile["linux_major"],
        profile["linux_minor"],
        profile["page_shift"],
    )
    observed = (
        identity["linux_major"],
        identity["linux_minor"],
        identity["page_shift"],
    )
    if observed != family:
        raise ValueError(
            f"{release_token}: identity {observed} is outside profile family "
            f"{family}"
        )
    if catalog_profiles:
        owner = COMPAT.dedicated_compile_family(catalog_profiles, identity)
        if owner is not None and owner != profile["legacy_id"]:
            raise ValueError(
                f"{release_token}: Android {identity['android_release']} "
                f"KMI {identity['kmi_generation']} is compile family {owner}, "
                f"not a leftover overlay on {profile['legacy_id']}"
            )

    inode_times = COMPAT.normalize_inode_times_layout(
        layouts, f"profile {profile['legacy_id']}"
    )
    if inode_times is None:
        raise ValueError("inode timestamp evidence is missing")

    family_ptmap = COMPAT.normalize_user_ptmap_layout(
        manifest, f"profile {profile['legacy_id']}"
    )
    vma_members = (
        COMPAT.USER_PTMAP_VMA_REQUIRED_KEYS | COMPAT.USER_PTMAP_VMA_PUBLIC_KEYS
    )
    user_ptmap = {
        "geometry": family_ptmap["geometry"],
        "mm_struct": narrow_layout(
            layouts["mm_struct"], COMPAT.USER_PTMAP_MM_MEMBER_KEYS
        ),
        "vm_area_struct": narrow_layout(layouts["vm_area_struct"], vma_members),
        "pt_regs": narrow_layout(
            layouts["pt_regs"], COMPAT.USER_PTMAP_REGS_MEMBER_KEYS
        ),
    }

    certificate = {
        "profile_id": profile["legacy_id"],
        "release_token": release_token,
        "identity": identity,
        evidence_name: evidence,
        "dir_context": narrow_layout(
            layouts["dir_context"], COMPAT.DIR_CONTEXT_MEMBER_KEYS
        ),
        "filldir_abi": profile["capabilities"]["filldir_abi"],
        "filename_name": narrow_layout(
            layouts["filename"], COMPAT.FILENAME_NAME_MEMBER_KEYS
        ),
        "inode_times": inode_times,
        "path_inode": {
            "path": narrow_layout(
                layouts["path"], COMPAT.PATH_DENTRY_MEMBER_KEYS
            ),
            "dentry": narrow_layout(
                layouts["dentry"], COMPAT.DENTRY_INODE_MEMBER_KEYS
            ),
        },
        "task_walk": {
            "task_struct": narrow_layout(
                layouts["task_struct"], COMPAT.TASK_WALK_TASK_MEMBER_KEYS
            ),
            "cred": narrow_layout(
                layouts["cred"], COMPAT.TASK_WALK_CRED_MEMBER_KEYS
            ),
        },
        "task_ref": {
            "task_struct": narrow_layout(
                layouts["task_struct"], COMPAT.TASK_REF_TASK_MEMBER_KEYS
            ),
        },
        "task_user_state": {
            "task_struct": narrow_layout(
                layouts["task_struct"], COMPAT.TASK_USER_STATE_TASK_MEMBER_KEYS
            ),
            "pt_regs": narrow_layout(
                layouts["pt_regs"], COMPAT.TASK_USER_STATE_PT_REGS_MEMBER_KEYS
            ),
        },
        "task_threads": {
            "task_struct": narrow_layout(
                layouts["task_struct"], COMPAT.TASK_THREADS_TASK_MEMBER_KEYS
            ),
            "signal_struct": narrow_layout(
                layouts["signal_struct"], COMPAT.TASK_THREADS_SIGNAL_MEMBER_KEYS
            ),
        },
        "user_ptmap": user_ptmap,
        "file_dentry": (
            COMPAT.member(layouts, "file", "f_path")
            + COMPAT.member(layouts, "path", "dentry")
        ),
    }
    return certificate


def _validate_target_manifest_evidence(observed_manifest, evidence_name,
                                       evidence):
    manifest_evidence = observed_manifest.get("evidence")
    required = ("config_sha256", "layout_sha256", "vmlinux_build_id")
    if (
        not isinstance(manifest_evidence, dict)
        or any(not manifest_evidence.get(name) for name in required)
        or not all(
            isinstance(manifest_evidence[name], str)
            for name in required
        )
        or not SHA256_HEX.fullmatch(manifest_evidence["config_sha256"])
        or not SHA256_HEX.fullmatch(manifest_evidence["layout_sha256"])
    ):
        raise ValueError(
            "full certificate requires a target-bound manifest with "
            "config, layout, and vmlinux build evidence"
        )
    expected_format = "BTF" if evidence_name == "raw_btf" else "DWARF"
    if manifest_evidence.get("layout_format") != expected_format:
        raise ValueError(
            "target-bound manifest layout format does not match vmlinux"
        )
    if manifest_evidence["layout_sha256"] != evidence.get("sha256"):
        raise ValueError(
            "target-bound manifest layout digest does not match vmlinux"
        )
    return {
        "config_sha256": manifest_evidence["config_sha256"],
        "layout_sha256": manifest_evidence["layout_sha256"],
        "vmlinux_build_id": manifest_evidence["vmlinux_build_id"],
    }


def build_full_certificate(profile, family_manifest, observed_manifest,
                           release_token, evidence_name, evidence,
                           catalog_profiles=None):
    """Build a complete exact-token runtime layout certificate."""
    provenance = _validate_target_manifest_evidence(
        observed_manifest, evidence_name, evidence
    )
    page_shift = observed_manifest["config"]["PAGE_SHIFT"]
    identity = identity_from_token(release_token, page_shift)
    family = (
        profile["linux_major"],
        profile["linux_minor"],
        profile["page_shift"],
    )
    observed_family = (
        identity["linux_major"],
        identity["linux_minor"],
        identity["page_shift"],
    )
    if observed_family != family:
        raise ValueError(
            f"{release_token}: identity {observed_family} is outside "
            f"profile family {family}"
        )
    if catalog_profiles:
        owner = COMPAT.dedicated_compile_family(catalog_profiles, identity)
        if owner is not None and owner != profile["legacy_id"]:
            raise ValueError(
                f"{release_token}: Android {identity['android_release']} "
                f"KMI {identity['kmi_generation']} is compile family {owner}, "
                f"not an overlay on {profile['legacy_id']}"
            )

    expected_loader = COMPAT.compile_loader_abi_contract(family_manifest)
    observed_loader = COMPAT.compile_loader_abi_contract(observed_manifest)
    if observed_loader != expected_loader:
        differences = ", ".join(
            f"{name}={expected_loader[name]}->{observed_loader[name]}"
            for name in COMPAT.LOADER_ABI_FIELD_NAMES
            if expected_loader[name] != observed_loader[name]
        )
        raise ValueError(
            f"{release_token}: loader ABI differs from compile profile "
            f"{profile['legacy_id']}: {differences}; add a dedicated profile"
        )

    return {
        "profile_id": profile["legacy_id"],
        "release_token": release_token,
        "identity": identity,
        evidence_name: evidence,
        "manifest_evidence": provenance,
        "runtime_layout": {
            "schema": 1,
            "fields": COMPAT.compile_runtime_layout(observed_manifest),
        },
    }


def merge_certificate(document, certificate):
    records = list(document.get("certificates", []))
    identity_key = (certificate["profile_id"], certificate["release_token"])
    records = [
        record for record in records
        if (record.get("profile_id"), record.get("release_token"))
        != identity_key
    ]
    records.append(certificate)
    records.sort(
        key=lambda record: (record["profile_id"], record["release_token"])
    )
    schema = 2 if any("runtime_layout" in record for record in records) else 1
    return {"schema": schema, "certificates": records}


def load_profile(catalog_path, profile_id):
    catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
    for profile in catalog.get("profiles", []):
        if profile.get("legacy_id") == profile_id:
            return profile
    raise ValueError(f"unknown profile family {profile_id}")


def main():
    parser = argparse.ArgumentParser(
        description="generate a compatible-identity GKI layout certificate"
    )
    parser.add_argument("--profile", required=True, type=int)
    parser.add_argument("--vmlinux", required=True, type=Path)
    parser.add_argument(
        "--release-token",
        help="optional expected token; identity is always read from vmlinux",
    )
    parser.add_argument(
        "--config",
        type=Path,
        help="target build .config/auto.conf required for a full certificate",
    )
    parser.add_argument(
        "--legacy-partial",
        action="store_true",
        help="emit the historical partial-field certificate schema",
    )
    parser.add_argument("--catalog", type=Path, default=DEFAULT_CATALOG)
    parser.add_argument(
        "--manifest-root", type=Path, default=DEFAULT_MANIFEST_ROOT
    )
    parser.add_argument("--output", type=Path)
    parser.add_argument(
        "--merge",
        type=Path,
        nargs="?",
        const=DEFAULT_CERTIFICATES,
        help="merge into a certificate document (default: checked-in file)",
    )
    args = parser.parse_args()
    if args.output is None and args.merge is None:
        parser.error("one of --output or --merge is required")

    try:
        catalog = json.loads(args.catalog.read_text(encoding="utf-8"))
        profile = load_profile(args.catalog, args.profile)
        manifest_path = args.manifest_root / f"{args.profile}.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if manifest.get("profile") != args.profile:
            raise ValueError(
                f"{manifest_path}: profile does not match --profile"
            )
        measured_token = assert_expected_release_token(
            release_token_from_vmlinux(args.vmlinux), args.release_token
        )
        layouts = EXTRACT.extract_layouts(str(args.vmlinux), CERTIFICATE_STRUCTURES)
        missing = sorted(set(CERTIFICATE_STRUCTURES) - set(layouts))
        # timespec64 is only required when inode still uses compound timestamps.
        if "timespec64" in missing and "inode" in layouts:
            inode_members = layouts["inode"].get("members", {})
            if all(
                name in inode_members
                for name in (
                    "i_atime_sec", "i_mtime_sec",
                    "i_atime_nsec", "i_mtime_nsec",
                )
            ):
                missing = [name for name in missing if name != "timespec64"]
        if missing:
            raise ValueError(
                "layout evidence is missing required structures: "
                + ", ".join(missing)
            )
        evidence_name, evidence = layout_evidence(args.vmlinux)
        if args.legacy_partial:
            certificate = build_certificate(
                profile,
                manifest,
                measured_token,
                layouts,
                evidence_name,
                evidence,
                catalog.get("profiles"),
            )
        else:
            if args.config is None:
                raise ValueError(
                    "--config is required for a target-bound full certificate"
                )
            config = MANIFEST.bind_arm64_runtime_config(
                MANIFEST.read_config(args.config), args.vmlinux
            )
            target_evidence = MANIFEST.elf_evidence(args.vmlinux)
            target_evidence["config_sha256"] = MANIFEST.sha256_file(args.config)
            if target_evidence["layout_sha256"] != evidence["sha256"]:
                raise ValueError(
                    "vmlinux layout evidence changed during certificate build"
                )
            observed_manifest = {
                "profile": args.profile,
                "config": config,
                "layouts": layouts,
                "evidence": target_evidence,
            }
            certificate = build_full_certificate(
                profile,
                manifest,
                observed_manifest,
                measured_token,
                evidence_name,
                evidence,
                catalog.get("profiles"),
            )
        if args.merge is not None:
            if args.merge.exists():
                document = json.loads(args.merge.read_text(encoding="utf-8"))
            else:
                document = {"schema": 2, "certificates": []}
            document = merge_certificate(document, certificate)
            args.merge.write_text(
                json.dumps(document, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            print(
                f"generate-layout-certificate: merged {args.profile} "
                f"{measured_token} ({evidence_name}) -> {args.merge}"
            )
        if args.output is not None:
            args.output.write_text(
                json.dumps(certificate, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            print(
                f"generate-layout-certificate: wrote {args.profile} "
                f"{measured_token} ({evidence_name}) -> {args.output}"
            )
    except (
        json.JSONDecodeError,
        OSError,
        RuntimeError,
        ValueError,
    ) as error:
        print(f"generate-layout-certificate: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

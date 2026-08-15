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
from pathlib import Path
import sys

from elftools.elf.elffile import ELFFile


TOOLS_ROOT = Path(__file__).resolve().parent
RUNTIME_ROOT = TOOLS_ROOT.parent
DEFAULT_CATALOG = RUNTIME_ROOT / "arm64/gki-profiles.json"
DEFAULT_MANIFEST_ROOT = RUNTIME_ROOT / "arm64/gki-manifests"
DEFAULT_CERTIFICATES = RUNTIME_ROOT / "arm64/gki-layout-certificates.json"

CERTIFICATE_STRUCTURES = (
    "cred",
    "dentry",
    "dir_context",
    "filename",
    "inode",
    "mm_struct",
    "path",
    "pt_regs",
    "signal_struct",
    "task_struct",
    "timespec64",
    "vm_area_struct",
)


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
    }
    return certificate


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
    return {"schema": 1, "certificates": records}


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
    parser.add_argument("--release-token", required=True)
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
        layouts = EXTRACT.extract_layouts(
            str(args.vmlinux), CERTIFICATE_STRUCTURES
        )
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
        certificate = build_certificate(
            profile,
            manifest,
            args.release_token,
            layouts,
            evidence_name,
            evidence,
            catalog.get("profiles"),
        )
        if args.merge is not None:
            if args.merge.exists():
                document = json.loads(args.merge.read_text(encoding="utf-8"))
            else:
                document = {"schema": 1, "certificates": []}
            document = merge_certificate(document, certificate)
            args.merge.write_text(
                json.dumps(document, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            print(
                f"generate-layout-certificate: merged {args.profile} "
                f"{args.release_token} ({evidence_name}) -> {args.merge}"
            )
        if args.output is not None:
            args.output.write_text(
                json.dumps(certificate, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            print(
                f"generate-layout-certificate: wrote {args.profile} "
                f"{args.release_token} ({evidence_name}) -> {args.output}"
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

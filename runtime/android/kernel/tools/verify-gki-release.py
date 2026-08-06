#!/usr/bin/env python3
"""Validate one pinned NeverC GKI release archive against runtime evidence."""

import argparse
import hashlib
import json
import mmap
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

from elftools.common.exceptions import ELFError
from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection


EXPECTED_PROFILES = ("510", "515", "601", "606", "612", "618")
MAX_ARCHIVE_MEMBERS = 10_000
MAX_ARCHIVE_FILE_SIZE = 1024 * 1024 * 1024
MAX_ARCHIVE_TOTAL_SIZE = 2 * 1024 * 1024 * 1024
SHA256_RE = re.compile(r"[0-9a-f]{64}\Z")
KCFI_TYPEID_RE = re.compile(r"0x[0-9a-f]{8}\Z")
LINUX_RELEASE_RE = re.compile(rb"Linux version ([0-9][^ \x00]*)")
MODULE_ENTRY_SYMBOLS = ("init_module", "cleanup_module")
SKIP_KCFI_VALIDATION = object()


class ValidationError(RuntimeError):
    """A release input does not match the checked evidence."""


def sha256_file(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def normalize_member_name(name):
    if not isinstance(name, str) or not name:
        raise ValidationError("archive member has an empty or non-string name")
    if "\\" in name or "\0" in name:
        raise ValidationError(f"unsafe archive member path: {name!r}")
    if any(ord(character) < 32 for character in name):
        raise ValidationError(f"archive member path contains a control byte: {name!r}")
    if name.startswith("/"):
        raise ValidationError(f"absolute archive member path: {name!r}")

    while name.startswith("./"):
        name = name[2:]
    if name in ("", "."):
        return "."
    raw_parts = name.split("/")
    if ".." in raw_parts:
        raise ValidationError(f"parent traversal in archive member path: {name!r}")
    normalized = str(PurePosixPath(name))
    if normalized == ".." or normalized.startswith("../"):
        raise ValidationError(f"parent traversal in archive member path: {name!r}")
    if PurePosixPath(normalized).is_absolute():
        raise ValidationError(f"absolute archive member path: {name!r}")
    return normalized.rstrip("/") or "."


def validate_kcfi_typeids(value, profile):
    if value is None:
        return
    required = set(MODULE_ENTRY_SYMBOLS)
    if not isinstance(value, dict) or set(value) != required:
        raise ValidationError(
            f"profile {profile} KCFI type IDs must be null or exactly "
            "cleanup_module and init_module"
        )
    for symbol in MODULE_ENTRY_SYMBOLS:
        typeid = value[symbol]
        if (
            not isinstance(typeid, str)
            or not KCFI_TYPEID_RE.fullmatch(typeid)
            or typeid == "0x00000000"
        ):
            raise ValidationError(
                f"profile {profile} has an invalid canonical KCFI type ID for {symbol}"
            )


def validate_lock(lock):
    if not isinstance(lock, dict):
        raise ValidationError("release lock must be a JSON object")
    if lock.get("schema") != 1:
        raise ValidationError("release lock schema must be 1")
    repository = lock.get("repository")
    if not isinstance(repository, dict):
        raise ValidationError("release lock repository must be an object")
    if not isinstance(repository.get("id"), int) or repository["id"] <= 0:
        raise ValidationError("release lock repository.id must be a positive integer")
    if not re.fullmatch(r"[^/]+/[^/]+", str(repository.get("name", ""))):
        raise ValidationError("release lock repository.name must be owner/name")
    if not isinstance(lock.get("tag"), str) or not lock["tag"]:
        raise ValidationError("release lock tag must be a non-empty string")

    profiles = lock.get("profiles")
    if not isinstance(profiles, dict) or set(profiles) != set(EXPECTED_PROFILES):
        raise ValidationError(
            "release lock profiles must be exactly " + ", ".join(EXPECTED_PROFILES)
        )
    assets = set()
    identities = set()
    ordered = {}
    required = {
        "asset",
        "kcfi_typeids",
        "kernel_name",
        "offset_module",
        "sha256",
        "size",
        "vermagic",
    }
    for profile in EXPECTED_PROFILES:
        entry = profiles[profile]
        if not isinstance(entry, dict) or set(entry) != required:
            raise ValidationError(
                f"profile {profile} fields must be exactly {', '.join(sorted(required))}"
            )
        asset = entry["asset"]
        if (
            not isinstance(asset, str)
            or Path(asset).name != asset
            or not asset.endswith(".tar.gz")
        ):
            raise ValidationError(f"profile {profile} has an invalid asset name")
        if asset in assets:
            raise ValidationError(f"duplicate asset in release lock: {asset}")
        assets.add(asset)
        if not isinstance(entry["size"], int) or entry["size"] <= 0:
            raise ValidationError(f"profile {profile} has an invalid asset size")
        if not isinstance(entry["sha256"], str) or not SHA256_RE.fullmatch(
            entry["sha256"]
        ):
            raise ValidationError(f"profile {profile} has an invalid SHA-256")
        if not isinstance(entry["kernel_name"], str) or not entry["kernel_name"]:
            raise ValidationError(f"profile {profile} has an invalid kernel name")
        if not isinstance(entry["vermagic"], str) or " " not in entry["vermagic"]:
            raise ValidationError(f"profile {profile} has an invalid vermagic")
        validate_kcfi_typeids(entry["kcfi_typeids"], profile)
        member = entry["offset_module"]
        if not isinstance(member, str):
            raise ValidationError(f"profile {profile} has an invalid offset module")
        normalized = normalize_member_name(member)
        if normalized != member or not member.endswith(".ko"):
            raise ValidationError(
                f"profile {profile} offset_module must be a normalized .ko path"
            )
        identity = (asset, member)
        if identity in identities:
            raise ValidationError(
                f"duplicate asset/member identity in release lock: {asset}:{member}"
            )
        identities.add(identity)
        ordered[profile] = entry
    lock["profiles"] = ordered
    return lock


def load_lock(path):
    try:
        value = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValidationError(f"cannot read release lock {path}: {error}") from error
    return validate_lock(value)


def verify_archive_identity(path, expected_size, expected_sha256):
    path = Path(path)
    try:
        actual_size = path.stat().st_size
    except OSError as error:
        raise ValidationError(f"cannot stat release archive {path}: {error}") from error
    if actual_size != expected_size:
        raise ValidationError(
            f"release archive size mismatch: expected {expected_size}, got {actual_size}"
        )
    actual_sha256 = sha256_file(path)
    if actual_sha256 != expected_sha256:
        raise ValidationError(
            "release archive SHA-256 mismatch: "
            f"expected {expected_sha256}, got {actual_sha256}"
        )
    print(f"[archive] identity OK size={actual_size} sha256={actual_sha256}")


def validate_tar_members(
    members,
    *,
    max_members=MAX_ARCHIVE_MEMBERS,
    max_file_size=MAX_ARCHIVE_FILE_SIZE,
    max_total_size=MAX_ARCHIVE_TOTAL_SIZE,
):
    if len(members) > max_members:
        raise ValidationError(
            f"archive has too many members: {len(members)} > {max_members}"
        )
    validated = []
    seen = {}
    total_size = 0
    regular_names = set()
    all_names = set()
    for member in members:
        normalized = normalize_member_name(member.name)
        if normalized == ".":
            if not member.isdir():
                raise ValidationError("archive root member must be a directory")
            if normalized in seen:
                raise ValidationError("duplicate normalized archive root member")
            seen[normalized] = member.name
            continue
        is_regular = member.type in (tarfile.REGTYPE, tarfile.AREGTYPE)
        if not (is_regular or member.isdir()):
            raise ValidationError(
                f"archive member is not a regular file/directory: {member.name!r}"
            )
        if normalized in seen:
            raise ValidationError(
                f"duplicate normalized archive member: {normalized!r} "
                f"({seen[normalized]!r}, {member.name!r})"
            )
        if member.size < 0:
            raise ValidationError(f"archive member has negative size: {member.name!r}")
        if is_regular and member.size > max_file_size:
            raise ValidationError(
                f"archive member exceeds individual size limit: {normalized}"
            )
        if member.isdir() and member.size != 0:
            raise ValidationError(f"archive directory has non-zero size: {normalized}")
        total_size += member.size
        if total_size > max_total_size:
            raise ValidationError(
                f"archive expanded size exceeds {max_total_size} bytes"
            )
        seen[normalized] = member.name
        all_names.add(normalized)
        if is_regular:
            regular_names.add(normalized)
        validated.append((member, normalized))

    for regular in regular_names:
        prefix = regular + "/"
        conflict = next((name for name in all_names if name.startswith(prefix)), None)
        if conflict is not None:
            raise ValidationError(
                f"archive file/directory path conflict: {regular!r}, {conflict!r}"
            )
    return validated


def safe_extract_archive(archive, destination):
    archive = Path(archive)
    destination = Path(destination)
    if destination.is_symlink():
        raise ValidationError(
            f"extraction destination must not be a symlink: {destination}"
        )
    if destination.exists() and any(destination.iterdir()):
        raise ValidationError(f"extraction destination is not empty: {destination}")
    destination.mkdir(parents=True, exist_ok=True)
    try:
        with tarfile.open(archive, mode="r:gz") as stream:
            validated = validate_tar_members(stream.getmembers())
            for member, normalized in validated:
                target = destination.joinpath(*PurePosixPath(normalized).parts)
                if member.isdir():
                    target.mkdir(parents=True, exist_ok=True)
                    os.chmod(target, member.mode & 0o777)
                    continue
                target.parent.mkdir(parents=True, exist_ok=True)
                source = stream.extractfile(member)
                if source is None:
                    raise ValidationError(f"cannot read archive member: {member.name}")
                with source, target.open("xb") as output:
                    shutil.copyfileobj(source, output, length=1024 * 1024)
                os.chmod(target, member.mode & 0o777)
    except (OSError, tarfile.TarError) as error:
        raise ValidationError(f"cannot safely extract {archive}: {error}") from error
    print(f"[archive] safely extracted {len(validated)} members to {destination}")
    return destination


def recursive_differences(expected, actual, path="", limit=20):
    differences = []

    def visit(left, right, current):
        if len(differences) >= limit:
            return
        if type(left) is not type(right):
            differences.append(f"{current or '$'}: expected {left!r}, actual {right!r}")
            return
        if isinstance(left, dict):
            for key in sorted(set(left) | set(right)):
                child = f"{current}.{key}" if current else str(key)
                if key not in left:
                    differences.append(f"{child}: unexpected {right[key]!r}")
                elif key not in right:
                    differences.append(f"{child}: missing (expected {left[key]!r})")
                else:
                    visit(left[key], right[key], child)
                if len(differences) >= limit:
                    return
            return
        if isinstance(left, list):
            for index in range(max(len(left), len(right))):
                child = f"{current}[{index}]"
                if index >= len(left):
                    differences.append(f"{child}: unexpected {right[index]!r}")
                elif index >= len(right):
                    differences.append(f"{child}: missing (expected {left[index]!r})")
                else:
                    visit(left[index], right[index], child)
                if len(differences) >= limit:
                    return
            return
        if left != right:
            differences.append(f"{current or '$'}: expected {left!r}, actual {right!r}")

    visit(expected, actual, path)
    return differences


def compare_manifest_bytes(expected_path, actual_path):
    expected_path = Path(expected_path)
    actual_path = Path(actual_path)
    expected_bytes = expected_path.read_bytes()
    actual_bytes = actual_path.read_bytes()
    if expected_bytes == actual_bytes:
        print(f"[manifest] byte-for-byte match: {expected_path.name}")
        return
    try:
        expected = json.loads(expected_bytes)
        actual = json.loads(actual_bytes)
    except json.JSONDecodeError as error:
        raise ValidationError(
            f"manifest byte-for-byte mismatch and invalid JSON: {error}"
        ) from error
    differences = recursive_differences(expected, actual)
    if differences:
        detail = "\n  ".join(differences)
        raise ValidationError(
            "manifest byte-for-byte mismatch; structural differences:\n  " + detail
        )
    raise ValidationError(
        "manifest byte-for-byte mismatch despite equal JSON; canonical bytes drifted"
    )


def find_evidence_files(root, profile):
    root = Path(root)
    configs = []
    symvers = []
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        if path.name in (".config", f"config-{profile}"):
            configs.append(path)
        if path.name == "Module.symvers":
            symvers.append(path)
    return sorted(configs), sorted(symvers)


def verify_evidence_hashes(paths, expected_sha256, label, root):
    root = Path(root)
    for path in paths:
        actual = sha256_file(path)
        if actual != expected_sha256:
            try:
                display = path.relative_to(root)
            except ValueError:
                display = path
            raise ValidationError(
                f"{label} evidence hash mismatch for {display}: "
                f"expected {expected_sha256}, got {actual}"
            )
        print(f"[evidence] {label} hash OK: {path.relative_to(root)}")


def parse_vermagic(modinfo, expected):
    try:
        entries = [
            value[len(b"vermagic=") :].decode("utf-8")
            for value in modinfo.split(b"\0")
            if value.startswith(b"vermagic=")
        ]
    except UnicodeDecodeError as error:
        raise ValidationError(f"module vermagic is not UTF-8: {error}") from error
    if len(entries) != 1:
        raise ValidationError(
            f"module must contain exactly one vermagic entry, found {len(entries)}"
        )
    if entries[0] != expected:
        raise ValidationError(
            f"module vermagic mismatch: expected {expected!r}, got {entries[0]!r}"
        )
    return entries[0]


def entry_prefix_records_from_elf(elf, path):
    path = Path(path)
    symbols = elf.get_section_by_name(".symtab")
    if not isinstance(symbols, SymbolTableSection):
        raise ValidationError(f"module symbol table is missing: {path}")

    records = {}
    for name in MODULE_ENTRY_SYMBOLS:
        matches = [symbol for symbol in symbols.iter_symbols() if symbol.name == name]
        if len(matches) != 1:
            raise ValidationError(
                f"module must define exactly one {name} symbol, "
                f"found {len(matches)}: {path}"
            )
        symbol = matches[0]
        if symbol["st_info"]["type"] != "STT_FUNC":
            raise ValidationError(f"module {name} is not a function symbol: {path}")
        section_index = symbol["st_shndx"]
        if not isinstance(section_index, int):
            raise ValidationError(
                f"module {name} does not reference a concrete section: {path}"
            )
        section = elf.get_section(section_index)
        if section is None or not (int(section["sh_flags"]) & 0x4):
            raise ValidationError(
                f"module {name} does not reference executable code: {path}"
            )
        section_offset = int(symbol["st_value"]) - int(section["sh_addr"])
        symbol_size = int(symbol["st_size"])
        if (
            section_offset < 0
            or section_offset > int(section["sh_size"])
            or section_offset + symbol_size > int(section["sh_size"])
        ):
            raise ValidationError(
                f"module {name} lies outside its executable section: {path}"
            )
        if section_offset >= 4:
            prefix = section.data()[section_offset - 4 : section_offset]
            if len(prefix) != 4:
                raise ValidationError(f"module {name} has a truncated prefix: {path}")
            file_offset = int(section["sh_offset"]) + section_offset - 4
        else:
            prefix = None
            file_offset = None
        records[name] = {
            "file_offset": file_offset,
            "prefix": prefix,
            "section": section.name,
            "section_offset": section_offset,
        }
    return records


def entry_prefix_records(path):
    path = Path(path)
    try:
        with path.open("rb") as stream:
            elf = ELFFile(stream)
            byteorder = "little" if elf.little_endian else "big"
            return entry_prefix_records_from_elf(elf, path), byteorder
    except (OSError, ELFError) as error:
        raise ValidationError(
            f"module is not a usable ELF file {path}: {error}"
        ) from error


def derive_pinned_kcfi_typeids(records, byteorder="little"):
    if set(records) != set(MODULE_ENTRY_SYMBOLS):
        raise ValidationError("pinned module entry records are incomplete")
    offsets = {record["section_offset"] for record in records.values()}
    if offsets == {0}:
        if any(record["prefix"] is not None for record in records.values()):
            raise ValidationError("non-KCFI entry unexpectedly has prefix bytes")
        return None
    if 0 in offsets:
        raise ValidationError(
            "pinned module entry symbols do not use a uniform KCFI prefix layout"
        )
    if offsets != {4}:
        rendered = ", ".join(str(value) for value in sorted(offsets))
        raise ValidationError(
            "pinned KCFI entry symbols must start at section offset 4, "
            f"found {rendered}"
        )

    typeids = {}
    for name in sorted(MODULE_ENTRY_SYMBOLS):
        prefix = records[name]["prefix"]
        if not isinstance(prefix, bytes) or len(prefix) != 4:
            raise ValidationError(f"pinned KCFI entry prefix is missing for {name}")
        value = int.from_bytes(prefix, byteorder=byteorder)
        if value == 0:
            raise ValidationError(f"pinned KCFI entry type ID is zero for {name}")
        typeids[name] = f"0x{value:08x}"
    return typeids


def inspect_offset_module(
    path, expected_vermagic, expected_kcfi_typeids=SKIP_KCFI_VALIDATION
):
    path = Path(path)
    try:
        with path.open("rb") as stream:
            elf = ELFFile(stream)
            module = elf.get_section_by_name(".gnu.linkonce.this_module")
            modinfo = elf.get_section_by_name(".modinfo")
            relocations = elf.get_section_by_name(
                ".rela.gnu.linkonce.this_module"
            ) or elf.get_section_by_name(".rel.gnu.linkonce.this_module")
            if module is None or modinfo is None or relocations is None:
                raise ValidationError(
                    f"pinned module lacks required module/relocation/modinfo sections: {path}"
                )
            symbols = elf.get_section(relocations["sh_link"])
            if symbols is None:
                raise ValidationError(
                    f"pinned module relocation symbol table is missing: {path}"
                )
            found = {"init_module": [], "cleanup_module": []}
            for relocation in relocations.iter_relocations():
                symbol = symbols.get_symbol(relocation["r_info_sym"]).name
                if symbol in found:
                    found[symbol].append(relocation["r_offset"])
            for symbol, offsets in found.items():
                if len(offsets) != 1:
                    raise ValidationError(
                        f"pinned module must have exactly one {symbol} relocation, "
                        f"found {len(offsets)}: {path}"
                    )
            vermagic = parse_vermagic(modinfo.data(), expected_vermagic)
            result = {
                "size": module.data_size,
                "init": found["init_module"][0],
                "exit": found["cleanup_module"][0],
                "vermagic": vermagic,
            }
            if expected_kcfi_typeids is not SKIP_KCFI_VALIDATION:
                actual_typeids = derive_pinned_kcfi_typeids(
                    entry_prefix_records_from_elf(elf, path),
                    byteorder="little" if elf.little_endian else "big",
                )
                if actual_typeids != expected_kcfi_typeids:
                    raise ValidationError(
                        "pinned module KCFI type IDs mismatch: "
                        f"expected {expected_kcfi_typeids!r}, got {actual_typeids!r}"
                    )
                result["kcfi_typeids"] = actual_typeids
    except (OSError, ELFError) as error:
        raise ValidationError(
            f"pinned module is not a usable ELF file {path}: {error}"
        ) from error
    print(
        "[module] ELF evidence "
        f"size={result['size']} init={result['init']} exit={result['exit']}"
    )
    print(f"[module] vermagic OK: {result['vermagic']}")
    if "kcfi_typeids" in result:
        if result["kcfi_typeids"] is None:
            print("[module] KCFI entry ABI OK: disabled")
        else:
            rendered = " ".join(
                f"{name}={value}"
                for name, value in sorted(result["kcfi_typeids"].items())
            )
            print(f"[module] KCFI entry ABI OK: {rendered}")
    return result


def extract_linux_releases(data):
    return sorted(
        {
            match.decode("ascii", errors="strict")
            for match in LINUX_RELEASE_RE.findall(data)
        }
    )


def verify_linux_banner(data, expected_vermagic):
    expected_release = expected_vermagic.split(" ", 1)[0]
    try:
        releases = extract_linux_releases(data)
    except UnicodeDecodeError as error:
        raise ValidationError(f"vmlinux linux_banner is not ASCII: {error}") from error
    if releases != [expected_release]:
        raise ValidationError(
            f"vmlinux linux_banner mismatch: expected {expected_release!r}, "
            f"found {releases!r}"
        )
    print(f"[vmlinux] linux_banner release OK: {expected_release}")
    return expected_release


def verify_linux_banner_path(path, expected_vermagic):
    with Path(path).open("rb") as stream:
        with mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as mapped:
            return verify_linux_banner(mapped, expected_vermagic)


def resolve_pinned_module(root, member):
    root = Path(root).resolve()
    normalized = normalize_member_name(member)
    candidate = root.joinpath(*PurePosixPath(normalized).parts)
    if not candidate.is_file():
        raise ValidationError(f"missing pinned offset module: {normalized}")
    try:
        candidate.resolve().relative_to(root)
    except ValueError as error:
        raise ValidationError(
            f"pinned offset module escaped extraction root: {member}"
        ) from error
    return candidate


def run_offset_verifier(profile, module, verifier, header, readelf):
    module = Path(module)
    verifier = Path(verifier)
    with tempfile.TemporaryDirectory(prefix=f"gki-{profile}-offset-") as directory:
        isolated = Path(directory)
        pinned = isolated / "pinned.ko"
        shutil.copyfile(module, pinned)
        command = [
            str(verifier),
            "--header",
            str(header),
            "--readelf",
            str(readelf),
            profile,
            str(isolated),
        ]
        try:
            result = subprocess.run(
                command,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=False,
            )
        except OSError as error:
            raise ValidationError(f"cannot run offset verifier: {error}") from error
        if result.returncode != 0:
            raise ValidationError(
                f"offset verifier failed with status {result.returncode}:\n{result.stdout}"
            )
        selected = None
        for line in result.stdout.splitlines():
            if line.startswith("[verify] object="):
                selected = Path(line.split("=", 1)[1]).resolve()
        if selected != pinned.resolve():
            raise ValidationError(
                f"offset verifier selected {selected}, expected isolated {pinned.resolve()}"
            )
        if "[verify] PASS:" not in result.stdout:
            raise ValidationError(
                "offset verifier returned zero without its PASS marker"
            )
        print(result.stdout, end="" if result.stdout.endswith("\n") else "\n")
        return result.stdout


def find_readelf(explicit=None):
    if explicit:
        result = shutil.which(explicit)
        if result is None:
            raise ValidationError(f"requested readelf is not on PATH: {explicit}")
        return result
    for candidate in (
        "llvm-readelf",
        "llvm-readelf-22",
        "llvm-readelf-21",
        "llvm-readelf-20",
        "readelf",
    ):
        result = shutil.which(candidate)
        if result is not None:
            return result
    raise ValidationError("no GNU/LLVM readelf found on PATH")


def find_single_dist_file(root, filename):
    root = Path(root)
    matches = []
    for path in root.rglob(filename):
        if not path.is_file():
            continue
        relative = path.relative_to(root)
        if len(relative.parts) >= 2 and relative.parts[-2:] == ("dist", filename):
            matches.append(path)
    if len(matches) != 1:
        raise ValidationError(
            f"expected exactly one dist/{filename}, found {len(matches)}"
        )
    return matches[0]


def run_manifest_generator(
    *,
    repo_root,
    profile,
    kernel_name,
    vmlinux,
    base_manifest,
    output,
    compiler,
    config=None,
    symvers=None,
):
    generator = repo_root / "runtime/android/kernel/tools/generate-gki-manifest.py"
    command = [
        sys.executable,
        str(generator),
        "--profile",
        profile,
        "--kernel-name",
        kernel_name,
        "--vmlinux",
        str(vmlinux),
        "--base-manifest",
        str(base_manifest),
        "--compiler",
        str(compiler),
        "--output",
        str(output),
    ]
    if config is not None:
        command.extend(("--config", str(config)))
    if symvers is not None:
        command.extend(("--symvers", str(symvers)))
    try:
        print(
            f"[manifest] regenerate profile={profile} from {vmlinux}",
            flush=True,
        )
        result = subprocess.run(
            command,
            cwd=repo_root,
            check=False,
        )
    except OSError as error:
        raise ValidationError(f"cannot run manifest generator: {error}") from error
    if result.returncode != 0:
        raise ValidationError(
            f"manifest generator failed with status {result.returncode}"
        )


def verify_extracted_release(*, repo_root, root, profile, entry, compiler, readelf):
    repo_root = Path(repo_root).resolve()
    root = Path(root).resolve()
    manifest = (
        repo_root / "runtime/android/kernel/arm64/gki-manifests" / f"{profile}.json"
    )
    if not manifest.is_file():
        raise ValidationError(f"checked manifest is missing: {manifest}")
    try:
        manifest_value = json.loads(manifest.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValidationError(
            f"cannot read checked manifest {manifest}: {error}"
        ) from error
    if manifest_value.get("profile") != int(profile):
        raise ValidationError(f"checked manifest profile does not match {profile}")
    if manifest_value.get("kernel_name") != entry["kernel_name"]:
        raise ValidationError(
            "checked manifest kernel_name does not match release lock"
        )

    vmlinux = find_single_dist_file(root, "vmlinux")
    image = find_single_dist_file(root, "Image")
    print(f"[release] vmlinux={vmlinux.relative_to(root)}")
    print(f"[release] Image={image.relative_to(root)}")
    configs, symvers_files = find_evidence_files(root, profile)
    evidence = manifest_value.get("evidence", {})
    verify_evidence_hashes(configs, evidence.get("config_sha256", ""), "config", root)
    verify_evidence_hashes(
        symvers_files,
        evidence.get("symvers_sha256", ""),
        "Module.symvers",
        root,
    )
    if not configs:
        print(
            "[evidence] config absent: inherited from checked manifest, not revalidated"
        )
    if not symvers_files:
        print(
            "[evidence] Module.symvers absent: inherited from checked manifest, "
            "not revalidated"
        )

    with tempfile.TemporaryDirectory(prefix=f"gki-{profile}-manifest-") as directory:
        generated = Path(directory) / f"{profile}.json"
        run_manifest_generator(
            repo_root=repo_root,
            profile=profile,
            kernel_name=entry["kernel_name"],
            vmlinux=vmlinux,
            base_manifest=manifest,
            output=generated,
            compiler=compiler,
            config=configs[0] if configs else None,
            symvers=symvers_files[0] if symvers_files else None,
        )
        compare_manifest_bytes(manifest, generated)

    module = resolve_pinned_module(root, entry["offset_module"])
    print(f"[module] pinned member={module.relative_to(root)}")
    inspect_offset_module(module, entry["vermagic"], entry["kcfi_typeids"])
    verify_linux_banner_path(vmlinux, entry["vermagic"])
    verifier = repo_root / "utils/build/verify_gki_offsets.sh"
    header = repo_root / "runtime/android/kernel/include/nvkmod_version.h"
    run_offset_verifier(profile, module, verifier, header, readelf)
    print(f"[release] PASS profile={profile} kernel={entry['kernel_name']}")
    return {"vmlinux": vmlinux, "image": image, "module": module}


def parse_args(argv=None):
    repo_root = Path(__file__).resolve().parents[4]
    parser = argparse.ArgumentParser(
        description="validate one pinned NeverC GKI release archive"
    )
    parser.add_argument("--profile", required=True, choices=EXPECTED_PROFILES)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--archive", type=Path)
    source.add_argument("--extracted-root", type=Path)
    parser.add_argument("--extract-to", type=Path)
    parser.add_argument(
        "--skip-archive-identity",
        action="store_true",
        help="allow an already-extracted local fixture (never use in CI)",
    )
    parser.add_argument(
        "--lock",
        type=Path,
        default=repo_root / "runtime/android/kernel/arm64/gki-release.json",
    )
    parser.add_argument("--repo-root", type=Path, default=repo_root)
    parser.add_argument("--compiler", default="clang")
    parser.add_argument("--readelf")
    args = parser.parse_args(argv)
    if args.archive is not None and args.skip_archive_identity:
        parser.error("--skip-archive-identity is only valid with --extracted-root")
    if args.extracted_root is not None and not args.skip_archive_identity:
        parser.error("--extracted-root requires --skip-archive-identity")
    if args.extracted_root is not None and args.extract_to is not None:
        parser.error("--extract-to is only valid with --archive")
    return args


def main(argv=None):
    args = parse_args(argv)
    try:
        lock = load_lock(args.lock)
        entry = lock["profiles"][args.profile]
        print(
            f"[release] repository={lock['repository']['name']} "
            f"id={lock['repository']['id']} tag={lock['tag']} profile={args.profile}"
        )
        readelf = find_readelf(args.readelf)
        if args.archive is not None:
            if args.archive.name != entry["asset"]:
                raise ValidationError(
                    f"archive filename mismatch: expected {entry['asset']}, "
                    f"got {args.archive.name}"
                )
            verify_archive_identity(args.archive, entry["size"], entry["sha256"])
            if args.extract_to is not None:
                root = safe_extract_archive(args.archive, args.extract_to)
                verify_extracted_release(
                    repo_root=args.repo_root,
                    root=root,
                    profile=args.profile,
                    entry=entry,
                    compiler=args.compiler,
                    readelf=readelf,
                )
            else:
                with tempfile.TemporaryDirectory(
                    prefix=f"gki-{args.profile}-release-"
                ) as directory:
                    root = safe_extract_archive(args.archive, directory)
                    verify_extracted_release(
                        repo_root=args.repo_root,
                        root=root,
                        profile=args.profile,
                        entry=entry,
                        compiler=args.compiler,
                        readelf=readelf,
                    )
        else:
            verify_extracted_release(
                repo_root=args.repo_root,
                root=args.extracted_root,
                profile=args.profile,
                entry=entry,
                compiler=args.compiler,
                readelf=readelf,
            )
    except (OSError, ValidationError) as error:
        print(f"verify-gki-release: error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

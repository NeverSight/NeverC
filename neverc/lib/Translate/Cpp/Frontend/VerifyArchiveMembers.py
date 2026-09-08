#!/usr/bin/env python3
"""Check the private frontend's archive member names before its ABI audit.

The reader must print one complete member name per line. This deliberately
rejects banners, blank lines and unknown records; the name check does not
validate object contents or replace the subsequent symbol-closure audit.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys


_KINDS = {"ar", "darwin", "msvc"}
_DARWIN_METADATA = {
    "__.SYMDEF", "__.SYMDEF SORTED", "__.SYMDEF_64", "__.SYMDEF_64 SORTED",
}
_EXCLUDED_MEMBERS = {
    stem + suffix
    for stem in ("AllTUsExecution", "Execution", "StandaloneExecution",
                 "BalancedPartitioning")
    for suffix in (".cpp.o", ".cpp.obj", ".o", ".obj")
}
_READER_TIMEOUT = 120


def _error(message: str) -> ValueError:
    return ValueError("Archive member verification: " + message)


def _check_kind(kind: str) -> None:
    if kind not in _KINDS:
        raise _error(f"unsupported reader kind {kind!r}")


def _leaf(member: str) -> str:
    return member.replace("\\", "/").rsplit("/", 1)[-1]


def parse_members(text: str, kind: str) -> list[str]:
    """Preserve object names and duplicates; omit only Darwin's first index."""
    _check_kind(kind)
    # Do not use splitlines(): it also consumes control characters that could
    # conceal a malformed reader record. Only LF and paired CRLF delimit names.
    text = text.replace("\r\n", "\n")
    if any((ord(char) < 32 and char != "\n") or ord(char) == 127
           for char in text):
        raise _error("control character in member listing")
    lines = text.split("\n")
    if lines[-1] == "":
        lines.pop()
    members = []
    for index, line in enumerate(lines):
        if line in _DARWIN_METADATA:
            if kind != "darwin" or index != 0:
                raise _error(f"unexpected archive metadata on line {index + 1}: {line!r}")
            continue
        leaf = _leaf(line)
        if (not line or leaf in (".o", ".obj")
                or not leaf.endswith((".o", ".obj"))):
            raise _error(f"unknown non-object member on line {index + 1}: {line!r}")
        members.append(line)
    if not members:
        raise _error("listing contains no object members")
    return members


def verify_archive(archive: Path | str, archiver: Path | str,
                   kind: str) -> list[str]:
    """Run the explicit read-only listing mode and reject excluded leaf names."""
    _check_kind(kind)
    tool = str(archiver)
    if _leaf(tool).lower() in {"libtool", "glibtool", "libtool.exe", "glibtool.exe"}:
        raise _error("libtool/glibtool is not an archive listing reader; select ar")
    archive = Path(archive).absolute()
    arguments = ["/NOLOGO", "/LIST"] if kind == "msvc" else ["t"]
    command = [tool, *arguments, str(archive)]
    try:
        # Keep bytes until parsing: text=True would normalize isolated CRs.
        result = subprocess.run(command, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, timeout=_READER_TIMEOUT,
                                check=False, shell=False)
    except subprocess.TimeoutExpired as error:
        raise _error(f"reader timed out after {_READER_TIMEOUT}s for {archive}") from error
    except OSError as error:
        raise _error(f"cannot run reader {tool!r}: {error}") from error
    if result.returncode:
        diagnostic = (result.stderr or result.stdout).decode("utf-8", "surrogateescape")
        raise _error(f"reader exited with status {result.returncode} for {archive}: "
                     f"{diagnostic[:2000]!r}")
    members = parse_members(result.stdout.decode("utf-8", "surrogateescape"), kind)
    excluded = [member for member in members if _leaf(member) in _EXCLUDED_MEMBERS]
    if excluded:
        raise _error("excluded frontend object members: " +
                     ", ".join(repr(member) for member in excluded))
    return members


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--archiver", type=Path, required=True)
    parser.add_argument("--kind", choices=sorted(_KINDS), required=True)
    args = parser.parse_args(argv)
    try:
        members = verify_archive(args.archive, args.archiver, args.kind)
    except ValueError as error:
        print(error, file=sys.stderr)
        return 1
    print(f"Builtin C++ frontend: checked {len(members)} object members; "
          "excluded implementation members are absent")
    return 0


if __name__ == "__main__":
    sys.exit(main())

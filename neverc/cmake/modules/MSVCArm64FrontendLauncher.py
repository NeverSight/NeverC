"""Apply the MSVC19.52 ARM64 dependency workaround selected by BuiltinCppFrontend.cmake."""
from pathlib import Path
import argparse
import os
import subprocess
import sys


def command_for(source, command):
    if not command or Path(command[0]).name.casefold() != "cl.exe":
        raise ValueError("The private MSVC launcher requires cl.exe")
    identity = os.path.abspath(os.path.normpath(os.fspath(source))).casefold()
    selected = [arg for arg in command[1:]
                if os.path.abspath(os.path.normpath(arg)).casefold() == identity]
    if selected:
        if len(selected) != 1:
            raise ValueError("The private compilation source must occur exactly once")
        return [*command, "/Od", "/Ob0", "/Oi-"]
    return list(command)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not args.source.is_file() or args.source.name != "ASTWriterDecl.cpp":
        parser.error("The pinned private ASTWriterDecl.cpp source is required")
    try:
        rewritten = command_for(args.source, command)
    except ValueError as error:
        parser.error(str(error))
    if rewritten != command:
        print("NeverC: disable MSVC19.52 ARM64 optimization for private ASTWriterDecl.cpp",
              file=sys.stderr, flush=True)
    return subprocess.call(rewritten)


if __name__ == "__main__":
    raise SystemExit(main())

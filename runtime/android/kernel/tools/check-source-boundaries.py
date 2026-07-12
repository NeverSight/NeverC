#!/usr/bin/env python3
"""Check NVK runtime public-header and source-boundary invariants."""

from pathlib import Path
import re
import sys


RUNTIME_ROOT = Path(__file__).resolve().parents[1]
PUBLIC_HEADER_ROOTS = (
    RUNTIME_ROOT / "include",
    RUNTIME_ROOT / "arm64" / "include",
)
CALLER_SIDE_PUBLIC_FUNCTIONS = {
    "nvk_cpu.h": {
        "_neverc_krt_cpu_idx_safe",
        "neverc_krt_cpu_id",
    },
}
BARE_ALWAYS_INLINE = re.compile(r"^\s*__always_inline\b")
VERSION_BRANCH = re.compile(
    r"^\s*#\s*(?:if|elif)\b.*\bNEVERC_KRT_KERNEL\b"
)
RAW_PRIVATE_ASM_REFERENCE = re.compile(
    r'"\s*(?:b|bl|adr|adrp|ldr)\s+_neverc_krt_[A-Za-z0-9_]+'
)
PRIVATE_HEADER_INCLUDE = re.compile(
    r"^\s*#\s*include\s*[<\"]nvk_internal\.h[>\"]"
)
PRIVATE_EXTERN = re.compile(
    r"^\s*extern\b.*\b_neverc_krt_[A-Za-z0-9_]+\b"
)
PRIVATE_FUNCTION_DECL = re.compile(
    r"^\s*(?!(?:return|if|while|for|switch)\b)"
    r"(?:[A-Za-z_][A-Za-z0-9_]*[\s*]+)+"
    r"_neverc_krt_[A-Za-z0-9_]+\s*\("
)
NON_CODE = re.compile(
    r"//[^\n]*|/\*.*?\*/|"
    r'"(?:\\.|[^"\\])*"|'
    r"'(?:\\.|[^'\\])*'",
    re.DOTALL,
)
MACRO_DEFINITION = re.compile(r"^\s*#\s*define\b")
STATIC_RUNTIME_FUNCTION = re.compile(
    r"^[ \t]*static\b"
    r"(?:(?![;{}]).)*?"
    r"\b(?P<name>_?neverc_krt_[A-Za-z0-9_]+)\s*"
    r"\((?:(?![;{}]).)*?\)\s*\{",
    re.MULTILINE | re.DOTALL,
)
FUNCTION_DEFINITION = re.compile(
    r"^[ \t]*(?P<prefix>"
    r"(?:[A-Za-z_][A-Za-z0-9_]*[ \t*]+)+)"
    r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*"
    r"\((?:(?![;{}]).)*?\)\s*\{",
    re.MULTILINE | re.DOTALL,
)
INTERNAL_VARIABLE_DECLARATION = re.compile(
    r"^[ \t]*extern\b[^\n;]*"
    r"\b(?P<name>_neverc_krt_[A-Za-z0-9_]+)\b[^\n;]*;",
    re.MULTILINE,
)
TOP_LEVEL_PRIVATE_VARIABLE = re.compile(
    r"^(?P<prefix>[A-Za-z_][^\n;{}]*?)"
    r"\b(?P<name>_neverc_krt_[A-Za-z0-9_]+)\b"
    r"(?P<tail>[^\n;{}]*);",
    re.MULTILINE,
)


def relative(path):
    return path.relative_to(RUNTIME_ROOT)


def code_only(text):
    def blank_non_code(match):
        return "".join(
            "\n" if char == "\n" else " " for char in match.group(0)
        )

    return NON_CODE.sub(blank_non_code, text)


def without_macro_definitions(text):
    output = []
    in_macro = False

    for line in text.splitlines(keepends=True):
        if in_macro or MACRO_DEFINITION.match(line):
            in_macro = line.rstrip().endswith("\\")
            output.append("".join(
                "\n" if char == "\n" else " " for char in line
            ))
        else:
            output.append(line)
    return "".join(output)


def check_public_header(path):
    violations = []
    text = path.read_text(encoding="utf-8")
    for line_number, line in enumerate(
        text.splitlines(), 1
    ):
        if BARE_ALWAYS_INLINE.match(line):
            violations.append(
                (path, line_number,
                 "header function must begin with static __always_inline")
            )
        if "_NEVERC_KRT_IMPL" in line:
            violations.append(
                (path, line_number,
                 "public header must not depend on _NEVERC_KRT_IMPL")
            )
        if PRIVATE_HEADER_INCLUDE.match(line):
            violations.append(
                (path, line_number,
                 "public header must not include nvk_internal.h")
            )
        if PRIVATE_EXTERN.match(line):
            violations.append(
                (path, line_number,
                 "private runtime state must not be declared in public header")
            )
        if (PRIVATE_FUNCTION_DECL.match(line)
                and not line.lstrip().startswith("static ")):
            violations.append(
                (path, line_number,
                 "private runtime function must not be declared in public header")
            )
    if path.parent == RUNTIME_ROOT / "include":
        code = code_only(without_macro_definitions(text))
        allowed = CALLER_SIDE_PUBLIC_FUNCTIONS.get(path.name, set())
        seen = set()
        for match in FUNCTION_DEFINITION.finditer(code):
            name = match.group("name")
            seen.add(name)
            line_number = code.count("\n", 0, match.start("name")) + 1
            if name not in allowed:
                violations.append(
                    (
                        path,
                        line_number,
                        f"public runtime function {name} must be declared in "
                        "the header and defined in a C source",
                    )
                )
            elif not re.search(
                r"\bstatic\s+__always_inline\b", match.group("prefix")
            ):
                violations.append(
                    (
                        path,
                        line_number,
                        f"caller-side function {name} must use "
                        "static __always_inline",
                    )
                )
        for missing in sorted(allowed - seen):
            violations.append(
                (
                    path,
                    1,
                    f"caller-side function allowlist entry {missing} has no "
                    "matching header definition",
                )
            )
    return violations


def check_source(path):
    violations = []
    for line_number, line in enumerate(
        path.read_text(encoding="utf-8").splitlines(), 1
    ):
        if VERSION_BRANCH.match(line):
            violations.append(
                (path, line_number,
                 "source must use compatibility APIs, not version branches")
            )
        if RAW_PRIVATE_ASM_REFERENCE.search(line):
            violations.append(
                (path, line_number,
                 "inline asm must reference private symbols through operands")
            )
    return violations


def check_private_header(path):
    violations = []
    code = code_only(without_macro_definitions(
        path.read_text(encoding="utf-8")
    ))
    for match in FUNCTION_DEFINITION.finditer(code):
        line_number = code.count("\n", 0, match.start("name")) + 1
        violations.append(
            (
                path,
                line_number,
                f"private function {match.group('name')} must be declared "
                "here and defined in a C source",
            )
        )
    return violations


def check_file_local_functions(source_paths):
    violations = []
    source_code = {
        path: code_only(path.read_text(encoding="utf-8"))
        for path in source_paths
    }
    static_definitions = {}

    for path, code in source_code.items():
        for match in STATIC_RUNTIME_FUNCTION.finditer(code):
            name = match.group("name")
            line_number = code.count("\n", 0, match.start("name")) + 1
            static_definitions.setdefault(name, []).append(
                (path, line_number)
            )

    for name, definitions in sorted(static_definitions.items()):
        token = re.compile(rf"\b{re.escape(name)}\b")
        defining_paths = {path for path, _ in definitions}
        for path, code in source_code.items():
            if path in defining_paths:
                continue
            match = token.search(code)
            if match is None:
                continue
            line_number = code.count("\n", 0, match.start()) + 1
            definition_list = ", ".join(
                f"{relative(def_path)}:{def_line}"
                for def_path, def_line in definitions
            )
            violations.append(
                (
                    path,
                    line_number,
                    f"file-local function {name} is referenced across "
                    f"translation units (defined at {definition_list})",
                )
            )

    return violations


def check_function_interfaces(source_paths):
    violations = []
    source_code = {
        path: code_only(path.read_text(encoding="utf-8"))
        for path in source_paths
    }
    public_code = "\n".join(
        code_only(path.read_text(encoding="utf-8"))
        for root in PUBLIC_HEADER_ROOTS
        for path in sorted(root.rglob("*.h"))
    )
    internal_path = RUNTIME_ROOT / "src" / "nvk_internal.h"
    internal_code = code_only(internal_path.read_text(encoding="utf-8"))

    for path, code in source_code.items():
        for match in FUNCTION_DEFINITION.finditer(code):
            prefix = match.group("prefix")
            if re.search(r"\bstatic\b", prefix):
                continue

            name = match.group("name")
            line_number = code.count("\n", 0, match.start("name")) + 1
            token = re.compile(rf"\b{re.escape(name)}\b")
            declared_public = token.search(public_code) is not None
            declared_internal = token.search(internal_code) is not None
            referenced_elsewhere = any(
                other_path != path and token.search(other_code) is not None
                for other_path, other_code in source_code.items()
            )
            if not declared_public and not declared_internal:
                violations.append(
                    (
                        path,
                        line_number,
                        f"non-static function {name} has no public "
                        "or internal interface declaration",
                    )
                )
            elif declared_internal and not declared_public and not referenced_elsewhere:
                violations.append(
                    (
                        path,
                        line_number,
                        f"file-local function {name} must be static and removed "
                        "from nvk_internal.h",
                    )
                )

    return violations


def check_variable_interfaces(source_paths):
    violations = []
    source_code = {
        path: code_only(path.read_text(encoding="utf-8"))
        for path in source_paths
    }
    internal_path = RUNTIME_ROOT / "src" / "nvk_internal.h"
    internal_code = code_only(internal_path.read_text(encoding="utf-8"))
    declarations = {
        match.group("name")
        for match in INTERNAL_VARIABLE_DECLARATION.finditer(internal_code)
    }
    definitions = {}

    for path, code in source_code.items():
        for match in TOP_LEVEL_PRIVATE_VARIABLE.finditer(code):
            prefix = match.group("prefix")
            tail = match.group("tail")
            if re.search(r"\b(?:extern|static|typedef)\b", prefix):
                continue
            if tail.lstrip().startswith("("):
                continue
            name = match.group("name")
            line_number = code.count("\n", 0, match.start("name")) + 1
            definitions.setdefault(name, []).append((path, line_number))
            if name not in declarations:
                violations.append(
                    (
                        path,
                        line_number,
                        f"non-static private variable {name} has no "
                        "nvk_internal.h declaration",
                    )
                )

    for name in sorted(declarations):
        locations = definitions.get(name, [])
        if len(locations) != 1:
            line_match = re.search(rf"\b{re.escape(name)}\b", internal_code)
            line_number = (
                internal_code.count("\n", 0, line_match.start()) + 1
                if line_match is not None
                else 1
            )
            violations.append(
                (
                    internal_path,
                    line_number,
                    f"private variable {name} must have exactly one C "
                    f"definition (found {len(locations)})",
                )
            )
            continue

        defining_path, _ = locations[0]
        token = re.compile(rf"\b{re.escape(name)}\b")
        if not any(
            path != defining_path and token.search(code) is not None
            for path, code in source_code.items()
        ):
            line_match = token.search(internal_code)
            line_number = internal_code.count(
                "\n", 0, line_match.start()
            ) + 1
            violations.append(
                (
                    internal_path,
                    line_number,
                    f"file-local variable {name} must be static and removed "
                    "from nvk_internal.h",
                )
            )

    return violations


def main():
    violations = []
    for root in PUBLIC_HEADER_ROOTS:
        for path in sorted(root.rglob("*.h")):
            violations.extend(check_public_header(path))
    source_paths = sorted((RUNTIME_ROOT / "src").glob("*.c"))
    internal_path = RUNTIME_ROOT / "src" / "nvk_internal.h"
    violations.extend(check_private_header(internal_path))
    for path in source_paths:
        violations.extend(check_source(path))
    violations.extend(check_file_local_functions(source_paths))
    violations.extend(check_function_interfaces(source_paths))
    violations.extend(check_variable_interfaces(source_paths))

    for path, line_number, message in violations:
        print(f"{relative(path)}:{line_number}: {message}", file=sys.stderr)
    if violations:
        print(
            f"check-source-boundaries: {len(violations)} violation(s)",
            file=sys.stderr,
        )
        return 1

    print("check-source-boundaries: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())

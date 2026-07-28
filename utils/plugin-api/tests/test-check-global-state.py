#!/usr/bin/env python3
"""Self-test for check-global-state.py.

Guards the fragile parts of the checker so the release gate does not silently
degrade into a single easy-to-miss ``rg``:

* comment/string stripping (a forbidden token in a comment or literal must not
  count, but the same token in real code must);
* forbidden-symbol detection across the plugin/dyncode/linker trees;
* the composite regex spellings (e.g. ``static CommonLinkerContext *lctx``).
"""

from __future__ import annotations

import contextlib
import importlib.util
import io
import pathlib
import sys
import tempfile


HERE = pathlib.Path(__file__).resolve().parent
CHECKER = HERE.parent / "check-global-state.py"


def load_checker():
    spec = importlib.util.spec_from_file_location("check_global_state", CHECKER)
    mod = importlib.util.module_from_spec(spec)
    # Register before exec so @dataclass can resolve the module namespace.
    sys.modules[spec.name] = mod
    spec.loader.exec_module(mod)
    return mod


_PERFORMED = 0


def expect(cond: bool, message: str, failures: list[str]) -> None:
    global _PERFORMED
    _PERFORMED += 1
    if not cond:
        failures.append(message)


def main() -> int:
    mod = load_checker()
    failures: list[str] = []

    strip = mod.strip_comments_and_strings

    # 1. Line comment hides the token.
    code = strip("int x; // getGlobalPluginLoader\n")
    expect("getGlobalPluginLoader" not in code,
           "line-comment token leaked past stripper", failures)

    # 2. Block comment hides the token.
    code = strip("/* getCurrentDynCodeOptions */ int y;\n")
    expect("getCurrentDynCodeOptions" not in code,
           "block-comment token leaked past stripper", failures)

    # 3. String literal hides the token.
    code = strip('const char *s = "setDynCodeModeState";\n')
    expect("setDynCodeModeState" not in code,
           "string-literal token leaked past stripper", failures)

    # 4. Real code keeps the token.
    code = strip("auto &o = currentDynCodeOptionsStorage();\n")
    expect("currentDynCodeOptionsStorage" in code,
           "real-code token wrongly stripped", failures)

    # 5. Newlines preserved so line numbers stay accurate.
    code = strip("a\n/* c\nc */\nb\n")
    expect(code.count("\n") == 4,
           f"line count changed by stripper: {code.count(chr(10))}", failures)

    # 6. Composite forbidden regex spelling.
    matched = any(
        pat.search("static CommonLinkerContext *lctx = nullptr;")
        for pat, _ in mod.FORBIDDEN_REGEXES
    )
    expect(matched, "composite lctx regex did not match", failures)

    # 7. Forbidden symbol table is non-empty and includes the dyncode singleton.
    expect("getCurrentDynCodeOptions" in mod.FORBIDDEN_SYMBOLS,
           "FORBIDDEN_SYMBOLS missing dyncode current-options", failures)
    expect("getGlobalPluginLoader" in mod.FORBIDDEN_SYMBOLS,
           "FORBIDDEN_SYMBOLS missing prototype loader", failures)

    # 8. Allowlist parses and only documents thread_local/static facades.
    allowlist = mod.load_allowlist()
    expect(isinstance(allowlist.get("entries"), list) and allowlist["entries"],
           "allowlist did not load any entries", failures)

    # 9. Writability classification. ELF reports .data.rel.ro as `d` even though
    # RELRO maps it read-only, so the section must win over the class letter;
    # Mach-O carries no section and must still be classified by the letter.
    writable = mod.symbol_is_writable
    expect(not writable("d", ".data.rel.ro"),
           ".data.rel.ro treated as writable", failures)
    expect(not writable("d", ".data.rel.ro.local"),
           ".data.rel.ro.local treated as writable", failures)
    expect(not writable("r", ".rodata"), ".rodata treated as writable", failures)
    expect(writable("b", ".bss"), ".bss not treated as writable", failures)
    expect(writable("d", ".data"), ".data not treated as writable", failures)
    expect(writable("b", ".bss.someSymbol"),
           "per-symbol .bss section not treated as writable", failures)
    expect(writable("b", ""), "Mach-O bss letter not treated as writable",
           failures)
    expect(not writable("s", ""),
           "Mach-O read-only letter treated as writable", failures)

    # 10. Thread-local recognition. The binary layer skips TLS and leaves it to
    # the source layer, so both platforms' spellings must be recognised: ELF
    # names the section, Mach-O only decorates the symbol.
    is_tls = mod.symbol_is_thread_local
    expect(is_tls("neverc::plugin::(anonymous namespace)::ActivePhases",
                  ".tbss"), "ELF .tbss not recognised as TLS", failures)
    expect(is_tls("whatever", ".tdata"), "ELF .tdata not recognised as TLS",
           failures)
    expect(is_tls("__ZN6neverc6plugin12_GLOBAL__N_112ActivePhasesE$tlv$init",
                  ""), "Mach-O $tlv$ suffix not recognised as TLS", failures)
    expect(is_tls("neverc::plugin::(anonymous namespace)::ActivePhases",
                  "__DATA,__thread_vars"),
           "Mach-O __thread_vars not recognised as TLS", failures)
    expect(not is_tls("neverc::plugin::(anonymous namespace)::Table", ".bss"),
           "plain .bss misclassified as TLS", failures)

    # 11. Scope matching must survive a failed demangle: nm gives up on Mach-O
    # decorations and hands back the raw Itanium spelling.
    in_scope = mod.symbol_in_audited_scope
    expect(in_scope("neverc::plugin::(anonymous namespace)::Table"),
           "demangled plugin scope not matched", failures)
    expect(in_scope("__ZN6neverc7dyncode12_GLOBAL__N_19IncompatsE"),
           "mangled dyncode scope not matched", failures)
    expect(not in_scope("llvm::cl::(anonymous namespace)::Opt"),
           "unrelated namespace matched as in-scope", failures)

    # 12. Same source, both hosts, same verdict. ELF and Mach-O describe the
    # identical set of objects very differently -- section names vs. bare class
    # letters, one TLS symbol vs. a descriptor plus a `$tlv$init` storage
    # symbol -- and the gate once passed on one host while failing on the other.
    allowed = "neverc::plugin::(anonymous namespace)::PluginMachinePass::ID"
    rogue = "neverc::plugin::(anonymous namespace)::RogueCache"
    elf_listing = [
        ("neverc::plugin::(anonymous namespace)::ActivePhases", "b", ".tbss"),
        ("neverc::plugin::(anonymous namespace)::GateOwnership", "b", ".tbss"),
        (allowed, "b", ".bss"),
        ("neverc::plugin::(anonymous namespace)::PassAPI", "d",
         ".data.rel.ro"),
        (rogue, "b", ".bss"),
    ]
    macho_listing = [
        ("neverc::plugin::(anonymous namespace)::ActivePhases", "s", ""),
        ("__ZN6neverc6plugin12_GLOBAL__N_112ActivePhasesE$tlv$init", "s", ""),
        ("neverc::plugin::(anonymous namespace)::GateOwnership", "s", ""),
        ("__ZN6neverc6plugin12_GLOBAL__N_113GateOwnershipE$tlv$init", "s", ""),
        (allowed, "b", ""),
        ("neverc::plugin::(anonymous namespace)::PassAPI", "s", ""),
        (rogue, "b", ""),
    ]
    elf_hits = mod.audit_symbols(elf_listing, allowlist)
    macho_hits = mod.audit_symbols(macho_listing, allowlist)
    expect(len(elf_hits) == 1 and rogue in elf_hits[0],
           f"ELF listing should report only the rogue global, got {elf_hits}",
           failures)
    expect(len(macho_hits) == 1 and rogue in macho_hits[0],
           f"Mach-O listing should report only the rogue global, "
           f"got {macho_hits}", failures)

    # 13. A thread_local belongs to exactly one list. Re-adding TLS to
    # `binary_symbols` would resurrect the split-verdict bug that 12 pins down.
    tls_symbols = {e.get("symbol") for e in allowlist.get("entries", [])}
    overlap = [e.get("symbol") for e in allowlist.get("binary_symbols", [])
               if any(t and t in e.get("symbol", "") for t in tls_symbols)]
    expect(not overlap,
           f"thread_local symbols duplicated into binary_symbols: {overlap}",
           failures)

    # 13b. Allowlist paths are matched independently of the host's path
    # separator. Windows produced `neverc\lib\Plugin\...`, which matched none of
    # the forward-slash entries, so every documented thread_local was reported
    # and the gate failed there while passing on Linux and macOS.
    documented = allowlist["entries"][0]
    posix_rel = documented["files"][0]
    windows_rel = posix_rel.replace("/", "\\")
    snippet = f"thread_local int {documented['symbol']};"
    expect(mod.allowlisted(posix_rel, snippet, allowlist),
           "documented entry not matched via posix path", failures)
    expect(mod.allowlisted(windows_rel, snippet, allowlist),
           f"documented entry not matched via windows path {windows_rel}",
           failures)
    expect(not mod.allowlisted("neverc/lib/Plugin/Core/Undocumented.cpp",
                               snippet, allowlist),
           "undocumented file wrongly allowlisted", failures)

    # 14. Locating the compiler must not depend on the host's executable
    # suffix: looking only for the extensionless name made Windows report a
    # missing binary, which the scan then treated as "nothing to do".
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        (root / "bin").mkdir()
        expect(mod.locate_compiler(root) is None,
               "empty build dir should locate no compiler", failures)
        win = root / "bin" / "neverc.exe"
        win.write_bytes(b"MZ")
        expect(mod.locate_compiler(root) == win,
               "neverc.exe not located", failures)
        posix = root / "bin" / "neverc"
        posix.write_bytes(b"\x7fELF")
        expect(mod.locate_compiler(root) == posix,
               "extensionless neverc not preferred once present", failures)

    # 15. A missing compiler is reported rather than skipped, so a mistyped
    # --build-dir cannot read as a clean run. It lands in `unscannable`, not
    # `binary`: an unbuilt tree is not a bad symbol.
    with tempfile.TemporaryDirectory() as tmp:
        report = mod.Report()
        with contextlib.redirect_stdout(io.StringIO()):
            mod.scan_binary(report, pathlib.Path(tmp), allowlist)
        expect(bool(report.unscannable),
               "missing compiler silently skipped the binary scan", failures)
        expect(not report.binary,
               "missing compiler misreported as a writable symbol", failures)
        expect(report.failed(strict=False),
               "missing compiler did not fail the gate", failures)

    # 16. A PE image is skipped explicitly, not reported: the classifier does
    # not speak COFF sections or Microsoft mangling.
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        (root / "bin").mkdir()
        (root / "bin" / "neverc.exe").write_bytes(b"MZ")
        report = mod.Report()
        with contextlib.redirect_stdout(io.StringIO()) as noise:
            mod.scan_binary(report, root, allowlist)
        expect(not report.binary, "PE image wrongly reported as a finding",
               failures)
        expect(not report.unscannable,
               "PE image wrongly reported as unscannable", failures)
        expect(not report.failed(strict=False),
               "PE image wrongly failed the gate", failures)
        expect("PE image" in noise.getvalue(),
               "PE skip did not say why", failures)

    # 17. C++14 digit separators. `0x3b00'0000` is one number; reading that
    # apostrophe as a character literal makes the scanner run to the next quote
    # and drop everything between -- a forbidden symbol there goes unseen.
    # ARM64Common.h already spells constants this way, so an odd quote count is
    # one edit away.
    token = "getGlobalPluginLoader"
    separated = ("constexpr uint64_t Mask = 0x1000'0000;\n"
                 "char Sep = ',';\n"
                 f"void f() {{ {token}(); }}\n")
    expect(token in strip(separated),
           "digit separator swallowed a forbidden symbol", failures)
    expect(mod.is_digit_separator("0x10'00", 4),
           "digit separator not recognised", failures)
    expect(not mod.is_digit_separator("u8'x'", 2),
           "u8 prefix misread as a digit separator", failures)
    expect(not mod.is_digit_separator("L'x'", 1),
           "L prefix misread as a digit separator", failures)
    expect(token in strip(f"char c = '\\''; {token}();"),
           "escaped quote in a char literal swallowed real code", failures)
    expect("x" not in strip("char c = 'x';"),
           "character literal no longer stripped", failures)

    # 18. Raw strings ignore escapes and may embed quotes, so the ordinary
    # literal scanner ends one early and then reads its contents as code.
    raw = ('const char *s = R"(say "hi" // not a comment)";\n'
           f"void f() {{ {token}(); }}\n")
    stripped = strip(raw)
    expect("say" not in stripped, "raw string contents leaked as code",
           failures)
    expect(token in stripped, "raw string swallowed the code after it",
           failures)
    expect(token not in strip(f'auto s = R"({token})";'),
           "forbidden symbol inside a raw string counted as code", failures)
    expect(token in strip(f'auto x = FOOR"notraw"; {token}();'),
           "R preceded by an identifier char misread as a raw string", failures)
    expect(token in strip(f'auto j = R"json({{"a":1}})json"; {token}();'),
           "custom raw delimiter mishandled", failures)
    multiline = 'a\nauto x = R"(\nmulti\n)";\nb\n'
    expect(strip(multiline).count("\n") == multiline.count("\n"),
           "multiline raw string changed the line count", failures)

    # 19. A backslash-newline inside a literal is a line continuation. Skipping
    # it as a plain two-character escape drops the newline, and every line
    # reported after that one is off by one.
    continued = 'int a;\nconst char *s = "abc\\\ndef";\nint b;\n'
    expect(strip(continued).count("\n") == continued.count("\n"),
           "line continuation inside a literal shifted the line numbers",
           failures)

    # 20. Header internal-linkage scan. This is the layer the binary scan
    # cannot cover: such objects demangle to a bare `(anonymous namespace)::X`
    # with no `neverc::` scope to match, and two copies of one object look
    # exactly like two same-named objects from different .cpp files.
    def kinds(source: str) -> list[str]:
        return [kind for _, kind, _ in mod.header_internal_state(source)]

    # `static` at namespace scope in a header: one object per includer.
    expect(kinds("namespace llvm {\nstatic bool Flag = false;\n}\n")
           == ["file-scope static"],
           "namespace-scope static in a header not reported", failures)

    # `inline static` reads as "one per program" but `static` wins, so it is
    # the same bug wearing a disguise -- this is how every real instance in
    # this tree was spelled.
    expect(kinds("namespace llvm {\ninline static bool Flag = false;\n}\n")
           == ["file-scope static"],
           "`inline static` not reported", failures)

    # The same declaration without `static` is correct and must stay silent.
    expect(kinds("namespace llvm {\ninline bool Flag = false;\n}\n") == [],
           "correct inline variable wrongly reported", failures)

    # An anonymous namespace in a header has the opposite meaning it has in the
    # .cpp this code was ported from.
    expect(kinds("namespace {\nstruct G { int x; };\n"
                 "inline G &globals() { static G g; return g; }\n}\n")
           == ["function-local static of an unmergeable function"],
           "anonymous-namespace function-local static not reported", failures)

    # The same helper in a named namespace is one object program-wide.
    expect(kinds("namespace llvm::detail {\nstruct G { int x; };\n"
                 "inline G &globals() { static G g; return g; }\n}\n") == [],
           "named-namespace function-local static wrongly reported", failures)

    # Constant-initialized data is exempt: every copy holds the same bytes.
    expect(kinds("namespace llvm {\nstatic constexpr int N = 4;\n"
                 "static const char Env[] = \"X\";\n"
                 "static constexpr char const *Name = \"X\";\n}\n") == [],
           "read-only data wrongly reported", failures)

    # In `const char *Msg` the const qualifies the characters, not the
    # pointer, so the pointer is still assignable -- and this is exactly how
    # the real BugReportMsg, which has a setter, was spelled.
    expect(kinds('namespace llvm {\ninline static const char *Msg = "x";\n}\n')
           == ["file-scope static"],
           "mutable pointer to const wrongly treated as read-only", failures)
    expect(kinds('namespace llvm {\ninline static char *const P = nullptr;\n}\n')
           == [],
           "`* const` pointer wrongly reported", failures)

    # A variable whose *type* comes from an anonymous namespace is internally
    # linked however the variable itself is spelled, so `inline` cannot make
    # the program share one copy. Only the binary shows this; the declaration
    # looks correct in isolation.
    borrowed = kinds("namespace llvm {\n"
                     "namespace { struct Impl { int x; }; }\n"
                     "inline Impl Registry;\n}\n")
    expect(len(borrowed) == 1 and "anonymous-namespace type" in borrowed[0],
           f"anonymous-namespace type not reported: {borrowed}", failures)
    expect(kinds("namespace llvm {\nstruct Impl { int x; };\n"
                 "inline Impl Registry;\n}\n") == [],
           "named type wrongly reported as anonymous", failures)

    # `static` on a member function means "no implicit object", not internal
    # linkage, so its locals are still one per program.
    expect(kinds("namespace llvm {\nstruct T {\n"
                 "  static int &get() { static int V = 0; return V; }\n};\n}\n")
           == [],
           "static member function's local wrongly reported", failures)

    # A static data member is declared `inline static` on purpose; that is a
    # member declaration, not a namespace-scope definition.
    expect(kinds("namespace llvm {\nstruct T { inline static bool F = false; };\n}\n")
           == [],
           "static data member wrongly reported", failures)

    # A directive must not be absorbed into the statement below it, and a
    # function declaration is not an object.
    expect(kinds("namespace llvm {\n#endif\ninline static bool Flag = false;\n}\n")
           == ["file-scope static"],
           "preprocessor directive broke statement recognition", failures)
    expect(kinds("namespace llvm {\ninline static void fn(const char *m);\n}\n")
           == [],
           "function declaration misread as an object", failures)

    # The reported line must point at the definition, not at the block start.
    found = list(mod.header_internal_state(
        "namespace llvm {\n\n\ninline static bool Flag = false;\n}\n"))
    expect(found and found[0][0] == 4,
           f"wrong line reported: {found[0][0] if found else 'none'}", failures)

    if failures:
        for f in failures:
            print(f"FAIL: {f}", file=sys.stderr)
        return 1
    print(f"test-check-global-state: OK ({_PERFORMED} checks passed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

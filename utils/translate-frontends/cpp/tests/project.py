#!/usr/bin/env python3
"""Independent cpp-project-v1 frontend evidence and isolation regression tests."""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path


def sha(text):
    return hashlib.sha256(text.encode()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--neverc", required=True, type=Path)
    parser.add_argument("--target", default="x86_64-apple-darwin24.6.0")
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    args.neverc = args.neverc.resolve()
    args.output_dir.mkdir(parents=True, exist_ok=False)
    count = 0

    def project(name, files):
        root = args.output_dir / name
        root.mkdir()
        for path, text in files.items():
            file = root / path
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text(text)
        return root

    def check(root, source="a.cpp", code=None, options=(), env=None, suffix="",
              working_directory=None, path_spelling=None):
        nonlocal count
        spell = path_spelling or (lambda text: text)
        arguments = [spell(option) if index and options[index - 1] in ("-I", "-iquote", "-isystem")
                     else option for index, option in enumerate(options)]
        request = root / (source.replace("/", "_") + suffix + ".request.json")
        response = root / (source.replace("/", "_") + suffix + ".response.json")
        request.write_text(json.dumps({"protocol": 1, "profile": "cpp-project-v1",
            "root": spell(str(root.resolve())), "source": spell(str((root / source).resolve())),
            "translation_unit": source, "working_directory": spell(str((working_directory or root).resolve())),
            "configuration_id": sha(source + repr(tuple(options))),
            "target": args.target, "arguments": ["-std=c++17", *arguments]}))
        process = subprocess.run([str(args.neverc), "__neverc_cpp_frontend", "--request", str(request),
            "--output", str(response)], text=True, capture_output=True, env=env, timeout=120)
        assert response.exists(), (source, process.returncode, process.stderr)
        result = json.loads(response.read_text())
        codes = {d["code"] for d in result["diagnostics"]}
        if code:
            assert process.returncode and code in codes, (source, code, result)
        else:
            assert process.returncode == 0 and not codes, (source, process.returncode, result)
            assert result["translation_unit"] == source
            assert result["configuration_id"] == sha(source + repr(tuple(options)))
            assert result["profile"] == "cpp-project-v1" and result["project_schema"] == 1
            for item in result["dependencies"]:
                assert item["sha256"] == hashlib.sha256((root / item["path"]).read_bytes()).hexdigest()
                assert not Path(item["path"]).is_absolute()
                assert "\\" not in item["path"]
            for item in [*result["odr"], *result["function_declarations"], *result["global_declarations"]]:
                assert len(item["semantic_id"]) == 64 and all(c in "0123456789abcdef" for c in item["semantic_id"])
        count += 1
        return result

    header = """#pragma once
namespace sample {
struct Pair { int first; unsigned int second; };
extern const int adjustment;
unsigned int combine(unsigned int value);
static int private_step(int value) { return value + 1; }
inline unsigned int twice(unsigned int value) {
  unsigned int result = value + value;
  return result;
}
}
"""
    root = project("shared", {"include/shared.hpp": header,
        "a.cpp": '#include "shared.hpp"\nnamespace sample { const int adjustment=3; unsigned int combine(unsigned int x){Pair p{private_step(1),twice(x)};return p.second+(unsigned int)adjustment+(unsigned int)p.first;} }',
        "b.cpp": '#include "shared.hpp"\nint main(){return (int)sample::combine(4u)-13;}'})
    a = check(root, options=("-I", str(root / "include")))
    b = check(root, "b.cpp", options=("-I", str(root / "include")))
    assert a["records"] == b["records"], "shared record or field IDs differ across units"
    assert len(a["globals"]) == 1 and not b["globals"] and len(b["global_declarations"]) == 1
    assert a["global_declarations"][0]["semantic_id"] == b["global_declarations"][0]["semantic_id"]
    def shared_odr(data):
        return {e["semantic_id"]: e for e in data["odr"] if e["origin"]["file"] == "include/shared.hpp" and not e["owner_tu"]}
    assert shared_odr(a) == shared_odr(b), "shared external evidence differs across units"
    private_a = [e for e in a["odr"] if e["owner_tu"]]
    private_b = [e for e in b["odr"] if e["owner_tu"]]
    assert len(private_a) == len(private_b) == 1
    assert private_a[0]["name"] != private_b[0]["name"]
    assert private_a[0]["owner_tu"] == "a.cpp" and private_b[0]["owner_tu"] == "b.cpp"
    inline = next(e["name"] for e in a["odr"] if e["inline"])
    assert next(f for f in a["functions"] if f["name"] == inline) == next(f for f in b["functions"] if f["name"] == inline)

    paths = project("canonical-paths", {
        "src/nested/main.cpp": '#include <angle.hpp>\n#include "quoted.hpp"\n#include <system.hpp>\n#include <root.hpp>\nint main(){return angle()+quoted()+system_value()+root_value()-10;}',
        "include/angle/angle.hpp": "static int angle(){return 1;}\n",
        "include/quoted/quoted.hpp": "inline int quoted(){return 2;}\n",
        "include/system/system.hpp": "inline int system_value(){return 3;}\n",
        "root.hpp": "static int root_value(){return 4;}\n",
        "work/nested/placeholder": ""})
    working = paths / "work/nested"
    path_options = ("-I", str((paths / "include/angle").resolve()),
                    "-iquote", str((paths / "include/quoted").resolve()),
                    "-isystem", str((paths / "include/system").resolve()), "-I", str(paths.resolve()))
    canonical = check(paths, "src/nested/main.cpp", options=path_options, working_directory=working)
    assert {item["path"] for item in canonical["dependencies"]} == {
        "src/nested/main.cpp", "include/angle/angle.hpp", "include/quoted/quoted.hpp",
        "include/system/system.hpp", "root.hpp"}
    spellings = [("forward", lambda text: Path(text).as_posix())]
    if os.name == "nt":
        spellings += [("backslash", lambda text: text.replace("/", "\\")),
                      ("mixed", lambda text: text.replace("\\", "/").replace("/", "\\", 1))]
    for label, spelling in spellings:
        assert check(paths, "src/nested/main.cpp", options=path_options, working_directory=working,
                     suffix="-" + label, path_spelling=spelling) == canonical
    relative = check(paths, "src/nested/main.cpp", options=("-I", "../../include/angle",
        "-iquote", "../../include/quoted", "-isystem", "../../include/system", "-I", "../.."),
        working_directory=working, suffix="-relative")
    assert relative == dict(canonical, configuration_id=relative["configuration_id"])
    path_sibling = project("canonical-paths-sibling", {"outside.hpp": "int outside(){return 0;}"})
    check(paths, "src/nested/main.cpp", code="TR0103", options=path_options,
          working_directory=path_sibling, suffix="-working-sibling")
    for label, flag in (("include", "-I"), ("quote", "-iquote"), ("system", "-isystem")):
        check(paths, "src/nested/main.cpp", code="TR0203", options=(flag, str(path_sibling.resolve())),
              suffix="-" + label + "-sibling")

    # Pure token spelling can violate ODR while resolved values and lowered IR
    # remain identical. Both evidence layers are required.
    macros = project("macro-tokens", {"value.hpp": "#pragma once\ninline int value(){return VALUE;}\n",
        "a.cpp": '#include "value.hpp"\n', "b.cpp": '#include "value.hpp"\n'})
    ma = check(macros, options=("-DVALUE=1",))
    mb = check(macros, "b.cpp", options=("-DVALUE=01",))
    assert ma["functions"] == mb["functions"]
    assert ma["odr"][0]["bindings_sha256"] == mb["odr"][0]["bindings_sha256"]
    assert ma["odr"][0]["tokens_sha256"] != mb["odr"][0]["tokens_sha256"]

    bindings = project("private-binding", {"value.hpp": "static int helper(){return 1;}\ninline int value(){return helper();}\n",
        "a.cpp": '#include "value.hpp"\n', "b.cpp": '#include "value.hpp"\n'})
    ba, bb = check(bindings), check(bindings, "b.cpp")
    ea = next(e for e in ba["odr"] if e["inline"])
    eb = next(e for e in bb["odr"] if e["inline"])
    assert ea["tokens_sha256"] == eb["tokens_sha256"]
    assert ea["bindings_sha256"] != eb["bindings_sha256"]

    private_c = project("private-c-linkage", {"value.hpp": 'extern "C" { static int local(int x){return x+1;} }\n',
        "a.cpp": '#include "value.hpp"\n', "b.cpp": '#include "value.hpp"\n'})
    ca, cb = check(private_c), check(private_c, "b.cpp")
    assert ca["functions"][0]["internal"] and not ca["functions"][0]["c_export"]
    assert ca["functions"][0]["name"] != cb["functions"][0]["name"]

    strong = project("strong-duplicate", {"a.cpp": "int same(int x){return x;}\n", "b.cpp": "int same(int x){return x;}\n"})
    sa, sb = check(strong), check(strong, "b.cpp")
    assert sa["odr"][0]["semantic_id"] == sb["odr"][0]["semantic_id"]
    assert sa["odr"][0]["tokens_sha256"] == sb["odr"][0]["tokens_sha256"]
    signatures = project("signature-conflict", {"a.cpp": "int conflict(int x){return x;}\n", "b.cpp": "unsigned int conflict(int x){return (unsigned int)x;}\n"})
    siga, sigb = check(signatures), check(signatures, "b.cpp")
    assert siga["function_declarations"][0]["semantic_id"] == sigb["function_declarations"][0]["semantic_id"]
    assert siga["function_declarations"][0]["result"] != sigb["function_declarations"][0]["result"]

    for name, header_text, code in (
        ("unused-pointer", "int unused(int *x){return 1;}\n", "TR0201"),
        ("unused-template", "template<class T> int unused(T x){return 1;}\n", "TR0201"),
        ("header-time", "#define UNUSED __DATE__\n", "TR0201"),
        ("header-pragma", "#pragma pack(1)\n", "TR0201"),
        ("header-line", '#line 9 "wrong.cpp"\n', "TR0201"),
    ):
        r = project(name, {"a.cpp": '#include "header.hpp"\nint main(){}', "header.hpp": header_text})
        data = check(r, code=code)
        assert any(d["file"] == "header.hpp" for d in data["diagnostics"])
    check(project("missing", {"a.cpp": '#include "missing.hpp"\n'}), code="TR0203")
    check(project("internal-missing", {"a.cpp": 'static int missing(int);\n'}), code="TR0203")
    check(project("inline-missing", {"a.cpp": 'inline int missing(int); int use(){return missing(1);}\n'}), code="TR0203")
    check(project("external-declaration", {"a.cpp": 'int missing(int); extern const int value;\n'}))
    foreign = project("foreign", {"other.hpp": "int outside(){return 1;}\n"})
    check(project("foreign-absolute", {"a.cpp": '#include "' + str(foreign / "other.hpp") + '"\n'}), code="TR0203")
    symlink = project("foreign-symlink", {"a.cpp": '#include "header.hpp"\n'})
    (symlink / "header.hpp").symlink_to(foreign / "other.hpp")
    check(symlink, code="TR0203")
    check(project("foreign-option", {"a.cpp": "int main(){}"}), code="TR0203", options=("-I", str(foreign)))

    poison = project("environment", {"clean/chosen.hpp": "const int selected=3;\n",
        "poison/chosen.hpp": "const int selected=91;\n", "poison/hidden.hpp": "int hidden(){return 0;}\n",
        "a.cpp": '#include <chosen.hpp>\nint main(){return selected-3;}', "b.cpp": '#include <hidden.hpp>\n'})
    environment = dict(os.environ, CPATH=str(poison / "poison"), CPLUS_INCLUDE_PATH=str(poison / "poison"),
        C_INCLUDE_PATH=str(poison / "poison"), SDKROOT=str(poison / "poison"), CCC_OVERRIDE_OPTIONS="+-I" + str(poison / "poison"))
    clean = check(poison, options=("-I", str(poison / "clean")))
    poisoned = check(poison, options=("-I", str(poison / "clean")), env=environment, suffix="-poisoned")
    assert clean == poisoned
    check(poison, "b.cpp", code="TR0203", env=environment)

    flags = project("flags", {"a.cpp": "int main(){return VALUE-4;}"})
    check(flags, options=("-O2", "-DVALUE=4", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-Wno-error", "-Wno-unused-parameter"))
    for name, flag in (("fast-math", "-ffast-math"), ("forced-include", "-include"), ("target", "--target=arm64-apple-darwin"), ("warning", "-Weverything")):
        check(project(name, {"a.cpp": "int main(){}"}), code="TR0004", options=(flag,))
    relocated = args.output_dir / "relocated"
    shutil.copytree(macros, relocated)
    for path in relocated.glob("*.json"):
        path.unlink()
    assert check(relocated, options=("-DVALUE=1",)) == ma, "absolute roots leaked into project evidence"
    print(json.dumps({"status": "passed", "cases": count, "target": args.target,
        "shared_responses": [str(root / "a.cpp.response.json"), str(root / "b.cpp.response.json")]}, indent=2))


if __name__ == "__main__":
    main()

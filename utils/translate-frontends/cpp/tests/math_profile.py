#!/usr/bin/env python3
"""Approved SDK provenance and bounded binary64 helper regression tests.

Keep this filename distinct from Python's math standard-library module.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path


def walk(value):
    if isinstance(value, dict):
        yield value
        for child in value.values():
            yield from walk(child)
    elif isinstance(value, list):
        for child in value:
            yield from walk(child)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--helper", required=True, type=Path)
    parser.add_argument("--sdk", required=True, type=Path, help="neverc.cpp.sdk descriptor")
    parser.add_argument("--target", default="x86_64-apple-macosx15.0.0")
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    args.helper = args.helper.resolve()
    args.output_dir.mkdir(parents=True, exist_ok=False)
    repository = Path(__file__).resolve().parents[4]
    catalog_path = repository / "utils/translate-frontends/cpp/sdk/approved-sdk.json"
    catalog = json.loads(catalog_path.read_text())
    descriptor = json.loads(args.sdk.read_text())
    sdk = {"distribution_id": descriptor["distribution_id"],
           "catalog_sha256": hashlib.sha256(catalog_path.read_bytes()).hexdigest(),
           "roots": {k: str(Path(v).resolve()) for k, v in descriptor["roots"].items()}}
    count = 0

    def check(name, source, code=None, options=(), selected_sdk=None, target=None, env=None):
        nonlocal count
        root = args.output_dir / name
        root.mkdir()
        (root / "input.cpp").write_text(source)
        request = {"protocol": 1, "profile": "cpp-math-v1", "root": str(root.resolve()),
            "source": str((root / "input.cpp").resolve()), "translation_unit": "input.cpp",
            "configuration_id": "a" * 64, "working_directory": str(root.resolve()),
            "target": target or args.target, "arguments": ["-std=c++17", *options], "sdk": selected_sdk or sdk}
        (root / "request.json").write_text(json.dumps(request))
        p = subprocess.run([str(args.helper), "--request", str(root / "request.json"),
            "--output", str(root / "response.json")], capture_output=True, text=True, env=env)
        assert (root / "response.json").exists(), (name, p.returncode, p.stderr)
        data = json.loads((root / "response.json").read_text())
        codes = {d["code"] for d in data["diagnostics"]}
        if code:
            assert p.returncode and code in codes, (name, code, data)
        else:
            assert not p.returncode and not codes, (name, p.returncode, data)
            assert data["fp_contract"] == "cpp.math.binary64.masked.v1"
            assert data["sdk_distribution_id"] == sdk["distribution_id"]
            assert data["sdk_catalog_sha256"] == sdk["catalog_sha256"]
            approved = {(e["root"], e["path"]): e["sha256"] for e in catalog["headers"]}
            for entry in data["sdk_dependencies"]:
                assert approved[(entry["root"], entry["path"])] == entry["sha256"]
            used_mappings = {node["mapping"] for node in walk(data["functions"])
                             if node.get("op") == "mapped_call"}
            assert {entry["id"] for entry in data["mappings"]} == used_mappings
            for node in walk(data):
                if node.get("kind") == "literal" and node.get("type") == "double":
                    assert "value" not in node and len(node["bits"]) == 16
                    assert all(c in "0123456789abcdef" for c in node["bits"])
        count += 1
        return data

    module = '#include <cmath>\nextern "C" double absolute(double value){return std::fabs(value);}\nextern "C" double round_down(double value){return std::floor(value);}\n'
    main = check("module", module)
    assert {m["id"] for m in main["mappings"]} == {"cpp.math.fabs.f64.v1", "cpp.math.floor.f64.v1"}
    assert len(main["sdk_dependencies"]) >= 190
    for m in main["mappings"]:
        operation = "fabs" if ".fabs." in m["id"] else "floor"
        expected = sdk["distribution_id"] + "\nplatform\nusr/include/math.h\nc:@F@" + operation + "\ndouble(double)"
        assert m["declaration_id"] == hashlib.sha256(expected.encode()).hexdigest()
        assert m["origin"]["column"] == 15 and m["origin"]["line"] == (423 if operation == "fabs" else 466)
    check("module-o2", module, options=("-O2",))
    check("module-arm64", module, target="arm64-apple-macosx15.0.0")
    check("native-darwin", module, target="x86_64-apple-darwin24.6.0")
    check("unapproved-deployment", module, "TR0204", target="x86_64-apple-macosx15.5.0")
    bits = check("bits", "const double a = -0.0; const double b = 0x1p-1074; const double c = 0x1.fffffffffffffp1023; double zero(){return double();}")
    values = {n["bits"] for n in walk(bits["globals"]) if n.get("type") == "double" and n.get("kind") == "literal"}
    assert values == {"8000000000000000", "0000000000000001", "7fefffffffffffff"}
    check("casts", 'extern "C" double from_int(int x){return (double)x;} extern "C" double from_uint(unsigned int x){return (double)x;} extern "C" double from_bool(bool x){return (double)x;} extern "C" int to_int(double x){return (int)x;} extern "C" unsigned int to_uint(double x){return (unsigned int)x;} extern "C" bool to_bool(double x){return (bool)x;}')
    check("comparison", 'extern "C" bool equal(double a,double b){return a==b;} extern "C" bool unequal(double a,double b){return a!=b;} extern "C" bool lower(double a,double b){return a<b;} extern "C" double negate(double x){return -x;}')
    check("aggregate", 'struct P{double x;int y;}; double f(bool c){P a;P b; a.x=1.5;b.x=-2.0;return(c?a:b).x;}')
    effect = check("sequencing", '#include <cmath>\nextern "C" double effect(double x){return std::fabs((x=-2.5,x));}')
    body = effect["functions"][0]["body"]
    mapped_index = next(i for i, n in enumerate(body) if n["op"] == "mapped_call")
    assert mapped_index > 1 and body[mapped_index]["args"][0]["kind"] == "var"
    dead_branch = check("dead-branch-mapping", '#include <cmath>\nextern "C" double probe(double x){if(false)return std::floor(x);return std::fabs(x);}')
    assert [entry["id"] for entry in dead_branch["mappings"]] == ["cpp.math.fabs.f64.v1"]
    after_return = check("after-return-mapping", '#include <cmath>\nextern "C" double probe(double x){return x;std::floor(x);std::fabs(x);}')
    assert not after_return["mappings"]
    for name, source, code in (
        ("float", 'float unused(float x){return x;}', "TR0201"),
        ("long-double", 'long double unused(long double x){return x;}', "TR0201"),
        ("add", 'double unused(double x){return x+1.0;}', "TR0201"),
        ("multiply", 'double unused(double x){return x*1.0;}', "TR0201"),
        ("increment", 'double unused(double x){return x++;}', "TR0201"),
        ("compound", 'double unused(double x){x+=1;return x;}', "TR0201"),
        ("constant-arithmetic", 'const double x=1.0+2.0;', "TR0201"),
        ("fake-std", 'namespace std { double fabs(double x){return x;} } double f(double x){return std::fabs(x);}', "TR0201"),
        ("owned-redeclaration", '#include <cmath>\nextern "C" double fabs(double); double f(double x){return std::fabs(x);}', "TR0203"),
        ("bare-fabs", '#include <cmath>\ndouble f(double x){return fabs(x);}', "TR0203"),
        ("integer-overload", '#include <cmath>\ndouble f(int x){return std::fabs(x);}', "TR0203"),
        ("float-overload", '#include <cmath>\ndouble f(){return std::fabs(1.0f);}', "TR0201"),
        ("sin", '#include <cmath>\ndouble f(double x){return std::sin(x);}', "TR0203"),
        ("dead-library-call", '#include <cmath>\ndouble f(double x){return x;std::sin(x);}', "TR0203"),
        ("constant-snan", '#include <cmath>\ndouble f(){return std::floor(__builtin_nans("0x123"));}', "TR0203"),
        ("builtin-nan", 'double f(){return __builtin_nan("0x123");}', "TR0203"),
        ("builtin-inf", 'double f(){return __builtin_inf();}', "TR0203"),
        ("using", '#include <cmath>\nusing std::fabs; double f(double x){return fabs(x);}', "TR0201"),
        ("alias", '#include <cmath>\nnamespace m=std; double f(double x){return m::fabs(x);}', "TR0201"),
        ("vector", '#include <vector>\nint main(){}', "TR0203"),
        ("fenv", '#include <fenv.h>\nint main(){return fegetround();}', "TR0203"),
        ("fp-pragma", '#pragma STDC FENV_ACCESS ON\nint main(){}', "TR0201"),
    ):
        check(name, source, code)
    check("fast-math", module, "TR0004", options=("-ffast-math",))
    check("sdk-name-macro", module, "TR0202", options=("-Dfabs=floor",))
    check("non-macos", module, "TR0204", target="x86_64-linux-gnu")
    wrong = json.loads(json.dumps(sdk)); wrong["catalog_sha256"] = "0" * 64
    check("wrong-catalog", module, "TR0203", selected_sdk=wrong)
    wrong = json.loads(json.dumps(sdk)); wrong["distribution_id"] = "not-approved"
    check("wrong-distribution", module, "TR0203", selected_sdk=wrong)

    # Copy only cataloged inputs; never mutate the installed SDK. A request
    # cannot bless edited bytes by supplying its own file hashes.
    clone = args.output_dir / "sdk-copy"
    copied = json.loads(json.dumps(sdk))
    for root in sdk["roots"]:
        (clone / root).mkdir(parents=True)
        copied["roots"][root] = str((clone / root).resolve())
    for entry in [*catalog["headers"], *catalog.get("metadata", [])]:
        path = clone / entry["root"] / entry["path"]
        path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(Path(sdk["roots"][entry["root"]]) / entry["path"], path)
    relocated = check("sdk-relocated", module, selected_sdk=copied)
    assert relocated == main, "SDK absolute paths leaked into semantic metadata"
    math_header = clone / "platform/usr/include/math.h"
    math_header.write_text(math_header.read_text() + "\n// changed SDK bytes\n")
    check("sdk-tampered", module, "TR0203", selected_sdk=copied)
    math_header.unlink()
    check("sdk-missing", module, "TR0203", selected_sdk=copied)
    environment = dict(os.environ, CPATH=str(clone), CPLUS_INCLUDE_PATH=str(clone), SDKROOT=str(clone))
    assert check("environment", module, env=environment) == main
    isolated_helper = args.output_dir / "isolated-helper"
    isolated_helper.mkdir()
    helper_copy = isolated_helper / "neverc-cpp-frontend"
    shutil.copy2(args.helper, helper_copy)
    for filename in ("clang-tool.cfg", "neverc-cpp-frontend.cfg", "clang++.cfg", "x86_64-apple-macosx15.0.0-clang++.cfg"):
        (isolated_helper / filename).write_text("--invalid-config-must-not-be-loaded\n")
    args.helper = helper_copy
    config_environment = dict(os.environ, HOME=str(isolated_helper), CLANG_CONFIG_FILE_USER_DIR=str(isolated_helper),
                              CLANG_CONFIG_FILE_SYSTEM_DIR=str(isolated_helper))
    assert check("default-config-isolation", module, env=config_environment) == main
    print(json.dumps({"status": "passed", "cases": count, "target": args.target,
        "module_response": str(args.output_dir / "module/response.json")}, indent=2))


if __name__ == "__main__":
    main()

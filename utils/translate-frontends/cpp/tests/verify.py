#!/usr/bin/env python3
"""Independent request/protocol/allowlist checks for the pinned internal C++ frontend."""
import argparse
import json
import os
import subprocess
import tempfile
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
    parser.add_argument("--neverc", required=True, type=Path)
    parser.add_argument("--target", default="arm64-apple-darwin24.6.0")
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    args.neverc = args.neverc.resolve()
    args.output_dir.mkdir(parents=True, exist_ok=False)
    repository = Path(__file__).resolve().parents[4]
    count = 0

    def check(name, source, code=None, options=(), root=None):
        nonlocal count
        directory = root or (args.output_dir / name)
        directory.mkdir(parents=True)
        file = directory / "input.cpp"
        file.write_text(source)
        request = directory / "request.json"
        response = directory / "response.json"
        request.write_text(json.dumps({"protocol": 1, "profile": "cpp-core-v1",
            "root": str(directory.resolve()), "source": str(file.resolve()),
            "target": args.target, "arguments": ["-std=c++17", *options]}))
        result = subprocess.run([str(args.neverc), "__neverc_cpp_frontend", "--request", str(request),
            "--output", str(response)], text=True, capture_output=True, timeout=120)
        assert response.exists(), (name, result.returncode, result.stderr)
        data = json.loads(response.read_text())
        codes = {d["code"] for d in data["diagnostics"]}
        if code:
            assert result.returncode and code in codes, (name, result.returncode, data)
        else:
            assert result.returncode == 0 and not codes, (name, result.returncode, data)
            assert data["target"]["triple"] == args.target
            assert data["dependencies"][0]["path"] == "input.cpp"
            before = response.read_bytes()
            again = subprocess.run([str(args.neverc), "__neverc_cpp_frontend", "--request", str(request),
                "--output", str(response)], capture_output=True, timeout=120)
            assert again.returncode and response.read_bytes() == before, "existing response overwritten"
        count += 1
        return data

    for fixture in ("program.cpp", "module.cpp", "unspecified-order.cpp"):
        check(fixture[:-4], (repository / "tests/neverc/Inputs/translate/cpp" / fixture).read_text())
    check("example", (repository / "examples/translate-cpp/input.cpp").read_text())
    check("globals", "struct Pair { int x; unsigned int y; }; constexpr Pair p{2, 3u}; const int k = p.x + 4; int main(){return k - 6;}")
    check("empty-main", "int main() {}")
    private_c = check("private-c-linkage", 'extern "C" {static int private_value(){return 3;}} int main(){return private_value()-3;}')
    internal = next(f for f in private_c["functions"] if f["internal"])
    assert not internal["c_export"] and internal["name"].startswith("nct_")
    check("infinite", "int loop() { for (;;) {} }")
    check("while-infinite", "int loop() { while (true) {} }")
    check("while-do-void", "void f(){int x=0; do { ++x; } while(x<3); while(x){--x;} } int main(){ f(); }")
    check("copy-assign", "struct P {int x;int y;}; int main(){P a{1,2};P b; b=a; return b.x-1;}")
    member_source = "struct P{int x;int y;}; int probe(){P p; p.x=4; return p.x;}"
    member = check("partial-member", member_source)
    record_id = member["records"][0]["id"]
    record_reads = [n for n in walk(member["functions"]) if n.get("op") == "assign"
        and n["value"].get("kind") == "var" and n["value"].get("type") == record_id]
    assert not record_reads, record_reads
    discarded = check("discarded-glvalues", "struct P{int a,b;}; int main(){int x; int y; P p; x; p; p.a; (x,0); (true ? x : y); return 0;}")
    reads = [n for n in walk(discarded["functions"]) if n.get("op") == "assign"
             and n["value"].get("kind") in ("var", "member")]
    assert not reads, reads
    projected = check("conditional-member", "struct P{int x,y;};struct Outer{P p;}; int f(bool c){Outer a;Outer b;a.p.x=1;b.p.x=2;return (c?a:b).p.x;}")
    projected_records = {r["id"] for r in projected["records"]}
    copies = [n for n in walk(projected["functions"]) if n.get("op") == "assign"
              and n["value"].get("type") in projected_records]
    assert not copies, copies
    with tempfile.TemporaryDirectory(prefix="neverc-cpp-relocated-") as temp:
        relocated = check("relocated", member_source, root=Path(temp) / "project")
        assert relocated == member, "semantic IDs or locations depend on absolute root"

    cases = {
        "include": '#include "missing.hpp"\nint main(){}',
        "inactive-include": '#if 0\n#include "missing.hpp"\n#endif\nint main(){}',
        "inactive-empty-directive": '#if 0\n#\n#include "missing.hpp"\n#endif\nint main(){}',
        "unused-time-macro": '#define UNUSED __DATE__\nint main(){}',
        "pragma": '#pragma pack(1)\nint main(){}',
        "line": '#line 99 "other.cpp"\nint main(){}',
        "numeric-linemarker": '# 99 "other.cpp"\nint main(){}',
        "date": 'const char *value=__DATE__; int main(){}',
        "path": 'const char *value=__FILE__; int main(){}',
        "counter": 'const int value=__COUNTER__; int main(){}',
        "pointer": 'int unused(int *value) {return 0;} int main(){}',
        "reference": 'int unused(int &value) {return 0;} int main(){}',
        "reference-cast": 'int main(){int x=0;static_cast<int&>(x)=2;return x;}',
        "c-reference-cast": 'int main(){int x=0;(int&)x=2;return x;}',
        "array": 'int unused(){int a[2]={1,2};return a[0];} int main(){}',
        "float": 'double unused(){return 1.0;} int main(){}',
        "dead-long": 'int f(){return 0; 1L;} int main(){}',
        "enum": 'enum E {V}; int main(){}',
        "union": 'union U {int x;bool y;}; int main(){}',
        "bitfield": 'struct P{int x:2;}; int main(){}',
        "constructor": 'struct P{P(){} int x;}; int main(){}',
        "template": 'template<class T> int f(T x){return 0;} int main(){}',
        "lambda": 'int main(){auto f=[](){return 1;};return f();}',
        "exception": 'int main(){throw 1;}',
        "switch": 'int f(int x){switch(x){default:return 0;}}',
        "static-local": 'int main(){static int x=1;return x;}',
        "mutable-global": 'int x=1;int main(){return x;}',
        "dynamic-global": 'int f(){return 1;}const int x=f();int main(){}',
        "reserved-export": 'extern "C" int string(){return 0;}',
        "runtime-export": 'extern "C" int nc_reserved(){return 0;}',
        "aggregate-export": 'struct P{int x;};extern "C" P f(){return P{1};}',
    }
    for name, source in cases.items():
        check(name, source, "TR0201")
    check("external", 'int missing(int); int main(){}', "TR0203")
    check("syntax", 'int main( {', "TR0202")
    check("target-option", 'int main(){}', "TR0004", ("--target=x86_64-linux-gnu",))
    check("fastmath-option", 'int main(){}', "TR0004", ("-ffast-math",))
    check("macro-option", 'int main(){return ANSWER-4;}', options=("-DANSWER=4",))
    check("reserved-macro-option", 'int main(){}', "TR0004", ("-D__cplusplus=1",))
    boundary = args.output_dir / "request-boundary"
    boundary.mkdir()
    source = boundary / "input.cpp"
    source.write_text("int main(){}")
    valid = {"protocol": 1, "profile": "cpp-core-v1", "root": str(boundary.resolve()),
             "source": str(source.resolve()), "target": args.target, "arguments": ["-std=c++17"]}

    def protocol_input(name, request_path, succeeds=False):
        nonlocal count
        response = boundary / (name + ".response.json")
        process = subprocess.run([str(args.neverc), "__neverc_cpp_frontend", "--request", str(request_path),
                                  "--output", str(response)], capture_output=True, text=True, timeout=5)
        assert response.is_file(), (name, process.returncode, process.stderr)
        data = json.loads(response.read_text())
        codes = {item["code"] for item in data["diagnostics"]}
        if succeeds:
            assert process.returncode == 0 and not codes, (name, data)
        else:
            assert process.returncode and "TR0103" in codes, (name, data)
        count += 1
        return data

    oversized = boundary / "oversized-request.json"
    with oversized.open("wb") as stream:
        stream.truncate(1024 * 1024 + 1)
    protocol_input("oversized", oversized)
    deep = boundary / "deep.json"
    deep.write_text('[' * 100000 + '0' + ']' * 100000)
    protocol_input("deep", deep)
    protocol_input("directory", boundary)
    maximum_depth = boundary / "unknown-at-depth-limit.json"
    maximum_depth.write_text(json.dumps(valid)[:-1] + ',"ignored":' + '[' * 31 + '0' + ']' * 31 + '}')
    protocol_input("unknown-at-depth-limit", maximum_depth)
    escaped = boundary / "escaped.json"
    escaped_source = boundary / 'escaped-[{quote}].cpp'
    escaped_source.write_text("int main(){}")
    escaped.write_text(json.dumps(dict(valid, source=str(escaped_source.resolve()),
        arguments=["-std=c++17", "-DJSON_TEXT=" + json.dumps('[{"quote"}]\\tail')])))
    protocol_input("escaped", escaped, succeeds=True)
    nested_source = boundary / "nested" / "source.cpp"
    nested_source.parent.mkdir()
    nested_source.write_text("int value(){return 7;}")
    native_root, native_source = str(boundary.resolve()), str(nested_source.resolve())
    spellings = [("native", native_root, native_source),
                 ("forward", boundary.resolve().as_posix(), nested_source.resolve().as_posix()),
                 ("trailing-root", native_root + os.sep, native_source)]
    if os.name == "nt":
        forward_root, forward_source = boundary.resolve().as_posix(), nested_source.resolve().as_posix()
        spellings += [("backslash", forward_root.replace("/", "\\"), forward_source.replace("/", "\\")),
                      ("mixed", forward_root.replace("/", "\\", 1), forward_source.replace("/", "\\", 1))]
    path_baseline = None
    for name, root_path, source_path in spellings:
        selected = boundary / ("paths-" + name + ".json")
        selected.write_text(json.dumps(dict(valid, root=root_path, source=source_path)))
        data = protocol_input("paths-" + name, selected, succeeds=True)
        assert data["dependencies"][0]["path"] == "nested/source.cpp"
        assert all("\\" not in node["file"] for node in walk(data) if "file" in node)
        if path_baseline is None:
            path_baseline = data
        else:
            assert data == path_baseline, "path separators changed semantic IDs or source locations"
    # The filesystem/drive root already ends with a separator. Containment
    # must still produce a relative path, without creating a double slash.
    filesystem_root = Path(native_source).anchor
    root_relative = nested_source.resolve().relative_to(filesystem_root).as_posix()
    for profile in ("cpp-core-v1", "cpp-project-v1"):
        selected = boundary / ("filesystem-root-" + profile + ".json")
        request = dict(valid, profile=profile, root=filesystem_root, source=native_source)
        if profile == "cpp-project-v1":
            request.update(translation_unit=root_relative, configuration_id="b" * 64,
                           working_directory=str(nested_source.parent.resolve()))
        selected.write_text(json.dumps(request))
        data = protocol_input("filesystem-root-" + profile, selected, succeeds=True)
        assert data["dependencies"][0]["path"] == root_relative
    sibling = boundary.with_name(boundary.name + "-sibling")
    sibling.mkdir()
    sibling_source = sibling / "source.cpp"
    sibling_source.write_text("int main(){}")
    selected = boundary / "prefix-sibling.json"
    selected.write_text(json.dumps(dict(valid, source=str(sibling_source.resolve()))))
    protocol_input("prefix-sibling", selected)
    for name, extra in (("unknown-key", {"ignored": 0}), ("core-sdk", {"sdk": {}}),
                        ("core-tu", {"translation_unit": "input.cpp"}),
                        ("core-configuration", {"configuration_id": "a" * 64}),
                        ("core-working-directory", {"working_directory": str(boundary.resolve())})):
        selected = boundary / (name + ".json")
        selected.write_text(json.dumps(dict(valid, **extra)))
        protocol_input(name, selected)
    project_context = dict(valid, profile="cpp-project-v1", translation_unit="input.cpp",
                           configuration_id="a" * 64, working_directory=str(boundary.resolve()))
    for name, request in (("project-sdk", dict(project_context, sdk={})),
                          ("project-unknown", dict(project_context, ignored=0)),
                          ("project-missing-context", dict(valid, profile="cpp-project-v1"))):
        selected = boundary / (name + ".json")
        selected.write_text(json.dumps(request))
        protocol_input(name, selected)
    ordinary = boundary / "valid.json"
    ordinary.write_text(json.dumps(valid))
    linked = boundary / "regular-link.json"
    linked.symlink_to(ordinary.name)
    protocol_input("regular-link", linked, succeeds=True)
    huge_source = boundary / "huge.cpp"
    with huge_source.open("wb") as stream:
        stream.truncate(8 * 1024 * 1024 + 1)
    huge_request = boundary / "huge-source.json"
    huge_request.write_text(json.dumps(dict(valid, source=str(huge_source.resolve()))))
    protocol_input("oversized-source", huge_request)
    if os.name != "nt":
        protocol_input("device", Path("/dev/null"))
        device = boundary / "device-link"
        device.symlink_to("/dev/null")
        protocol_input("device-link", device)
        fifo = boundary / "request.fifo"
        os.mkfifo(fifo)
        protocol_input("fifo", fifo)
        fifo_link = boundary / "fifo-link"
        fifo_link.symlink_to(fifo.name)
        protocol_input("fifo-link", fifo_link)
        for name, path in (("source-fifo", fifo), ("source-fifo-link", fifo_link)):
            selected = boundary / (name + ".json")
            selected.write_text(json.dumps(dict(valid, source=str(path))))
            protocol_input(name, selected)
    print(json.dumps({"status": "passed", "cases": count, "target": args.target}, indent=2))


if __name__ == "__main__":
    main()

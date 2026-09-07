#!/usr/bin/env python3
"""Independent request/protocol/allowlist checks for the pinned C++ helper."""
import argparse
import json
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
    parser.add_argument("--helper", required=True, type=Path)
    parser.add_argument("--target", default="arm64-apple-darwin24.6.0")
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
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
        result = subprocess.run([str(args.helper), "--request", str(request),
            "--output", str(response)], text=True, capture_output=True)
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
            again = subprocess.run([str(args.helper), "--request", str(request),
                "--output", str(response)], capture_output=True)
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
    print(json.dumps({"status": "passed", "cases": count, "target": args.target}, indent=2))


if __name__ == "__main__":
    main()

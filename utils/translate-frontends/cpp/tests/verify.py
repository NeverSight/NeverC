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

    def check(name, source, code=None, options=(), root=None, profile="cpp-core-v1"):
        nonlocal count
        directory = root or (args.output_dir / name)
        directory.mkdir(parents=True)
        file = directory / "input.cpp"
        file.write_text(source)
        request = directory / "request.json"
        response = directory / "response.json"
        request.write_text(json.dumps({"protocol": 1, "profile": profile,
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
            assert data["profile"] == profile
            assert data["target"]["triple"] == args.target
            if profile == "cpp-core-v2":
                layout = data["target"]["carrier_layout"]
                widths = {"i8": 8, "u8": 8, "i16": 16, "u16": 16,
                          "int": 32, "uint": 32, "i64": 64, "u64": 64,
                          "bool": 8, "default-pointer": data["target"]["pointer_bits"]}
                assert set(layout) == {"char_bits", *widths}
                assert layout["char_bits"] == 8
                for carrier, width in widths.items():
                    entry = layout[carrier]
                    assert set(entry) == {"size_bits", "abi_align_bits"}
                    assert entry["size_bits"] == width
                    align = entry["abi_align_bits"]
                    assert 8 <= align <= width and align & (align - 1) == 0
                for record in data["records"]:
                    record_layout = record["layout"]
                    assert set(record_layout) == {"size_bits", "abi_align_bits", "field_offsets_bits"}
                    assert len(record_layout["field_offsets_bits"]) == len(record["fields"])
            else:
                assert "carrier_layout" not in data["target"]
                assert all("layout" not in record for record in data["records"])
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

    # The explicit next core profile admits declarations without changing v1.
    core_v2 = {
        "alias-chain": "typedef int I; using J=I; J f(J x){using K=J; K y=x; return y;}",
        "void-alias": "using Nothing=void; Nothing f(){} int main(){f();}",
        "scoped-enum": "enum class E:unsigned int{v=0xffffffffu}; unsigned int f(){return static_cast<unsigned int>(E::v);}",
        "signed-enum": "enum class E:int{v=-2147483647-1}; int f(){return static_cast<int>(E::v);}",
        "unscoped-enum": "enum E{a=2,b=a+3}; int f(){return b;}",
        "enum-global-field": "enum class E:int{v=4}; struct P{E e;}; constexpr P p{E::v}; int f(){return static_cast<int>(p.e);}",
        "enum-overload": "enum class E:int{v=4}; int f(E x){return 1;} int f(int x){return 2;} int main(){return f(E::v)-1;}",
        "local-declarations": "int main(){using I=int; enum class E:I{v=4}; static_assert(static_cast<I>(E::v)==4,\"text\"); return 0;}",
        "assertion-no-message": "static_assert(true); int main(){}",
        "assertion-message": "static_assert(true,\"diagnostic text only\"); int main(){}",
        "enum-initialization": "enum class E:unsigned int{v=1}; int main(){E direct{1u}; E zero{}; return direct==E::v && static_cast<unsigned int>(zero)==0u ? 0:1;}",
        "opaque-enum": "enum class E:unsigned int; unsigned int f(){E e=static_cast<E>(23u); return static_cast<unsigned int>(e);}",
        "unsigned-unscoped": "enum E:unsigned int{v=0xffffffffu}; int main(){return v+1==0u && v==-1 ? 0:1;}",
    }
    core_v2.update({
        "pointer-alias": "using P=int*; void f(P p){*p=3;}",
        "reference-return": "int& f(int&x){return x;} int main(){int x=1; f(x)=7; return x-7;}",
        "pointer-reference": "int*& f(int*&p){return p;} int main(){int x=3; int *p=nullptr; f(p)=&x; return *p-3;}",
        "const-pointee": "int f(const int*p){return *p;} int main(){const int x=7; return f(&x)-7;}",
        "null-pointer": "int main(){int*p=((nullptr)); int*q{}; return p!=q || p!=0 || !!p;}",
        "pointer-cast": "int main(){int x=7; void*p=&x; return *static_cast<int*>(p)-7;}",
        "const-cast": "int main(){int x=7; const int& r=x; const_cast<int&>(r)=9; return x-9;}",
        "self-pointer": "struct R{R*next; int value;}; int main(){R r{nullptr,7}; r.next=&r; return r.next->value-7;}",
        "forward-pointer": "struct B; struct A{B*b;}; struct B{A*a;}; int main(){A a{}; B b{&a}; a.b=&b; return a.b->a!=&a;}",
        "enum-pointer": "enum class E:int{v=7}; int main(){E e=E::v; E*p=&e; return static_cast<int>(*p)-7;}",
    })
    core_v2.update({
        "functional-scalar-cast": "int main(){return int{7}-int(7);}",
        "conditional-record-self-observation": "struct P{int x;int y;}; int f(bool b){P p=b?P{3,p.x+4}:P{5,p.x+6}; return p.y;} int main(){return f(true)==7 && f(false)==11 ? 0:1;}",
        "comma-record-self-observation": "struct P{int x;int y;}; int main(){int n=0; P p=(++n,P{3,p.x+4}); return p.y==7 && n==1 ? 0:1;}",
        "typed-record-self-observation": "struct R{int a[2];int m;}; int main(){R r=R{{5,r.a[0]+2},r.a[1]+3}; return r.a[1]==7 && r.m==10 ? 0:1;}",
        "braced-record-array": "struct R{int a[2];}; int main(){return R{{7,8}}.a[0]-7;}",
        "temporary-record-array": "struct R{int a[2];}; R make(){return {{7,8}};} int main(){return make().a[0]-7;}",
        "adjusted-array-parameter": "int f(int a[2]){return a[1];} int main(){int a[2]={1,2}; return f(a)-2;}",
        "fixed-array": "int main(){int a[3]={1}; return a[0]==1 && a[1]==0 && a[2]==0 ? 0:1;}",
        "array-reference": "using Row=int[3]; Row& f(Row&r){return r;} int main(){Row a{}; f(a)[1]=7; return a[1]-7;}",
        "array-pointer-result": "using Row=int[3]; Row*f(Row&r){return &r;} int main(){Row a{}; (*f(a))[2]=9; return a[2]-9;}",
        "multidimensional-array": "int main(){int a[2][3]={{1},{2}}; return a[0][0]==1 && a[1][0]==2 && a[1][2]==0 ? 0:1;}",
        "array-self-observation": "struct R{int a[2];int m;}; int main(){R r{{5,r.a[0]+2},r.a[1]+3}; return r.a[1]==7 && r.m==10 ? 0:1;}",
        "const-array": "int main(){const int a[2][3]={{1},{2}}; const int(*p)[3]=a; return p[1][0]-2;}",
        "array-of-records": "struct R{int a;int b;}; int main(){R r[2]={{1},{2}}; return r[0].b+r[1].b;}",
        "array-in-record-copy": "struct R{int a[2];}; int main(){R r{{1,2}}; R s=r; s.a[1]=7; return r.a[1]==2 && s.a[1]==7 ? 0:1;}",
        "pointer-call-index-reference": "int*f(int*p){return p;} int main(){int a[2]={1,2}; int&r=f(a)[1]; r=3; return a[1]-3;}",
    })
    core_v2.update({
        "switch-basic": "int f(int n){switch(n){case 1:return 7;default:return 9;}} int main(){return f(1)-7;}",
        "switch-fallthrough": "int f(int n){int r=0;switch(n){case 0:r=1;[[fallthrough]];case 1:r+=2;break;default:r=3;}return r;} int main(){return f(0)-3;}",
        "switch-constant-return": "int f(){switch(1){case 1:return 7;}} int main(){return f()-7;}",
        "switch-constant-no-match": "int main(){switch(1){case 2:return 9;}return 0;}",
        "switch-nested-entry": "int f(int n){switch(n){return 9;int x;{case 1:x=7;return x;}default:return 3;}} int main(){return f(1)-7;}",
        "switch-continue": "int main(){int s=0;for(int i=0;i<3;++i){switch(i){case 1:continue;default:s+=i;}}return s-2;}",
        "switch-condition-variable": "int main(){switch(int n=7){case 7:return n-7;default:return 9;}}",
        "switch-init": "int main(){int calls=0;switch(int n=++calls;n){case 1:break;default:return 9;}return calls-1;}",
        "switch-unsigned-enum": "enum class E:unsigned int{top=0xffffffffu};int f(E n){switch(n){case E::top:return 7;default:return 9;}}int main(){return f(E::top)-7;}",
        "switch-empty": "int main(){int n=0;switch(++n){n=9;}return n-1;}",
    })
    for name, source in core_v2.items():
        check("v2-" + name, source, profile="cpp-core-v2")
    for name, source in {
        "alias": "using I=int; int main(){}",
        "typedef": "typedef int I; int main(){}",
        "assertion": "static_assert(true,\"message\"); int main(){}",
        "assertion-joined-message": "static_assert(true,\"joined \" \"message\"); int main(){}",
        "assertion-no-message": "static_assert(true); int main(){}",
        "assertion-namespace": "namespace N{static_assert(true,\"message\");} int main(){}",
        "assertion-local": "int main(){static_assert(true,\"message\");}",
    }.items():
        check("v1-still-rejects-" + name, source, "TR0201")
    v2_rejections = {
        "untyped-assembly-string": 'asm(""); int main(){}',
        "unsupported-pointer-alias": "using Hidden=char*; int main(){}",
        "unused-volatile-alias": "using Hidden=volatile int; int main(){}",
        "unused-function-alias": "using Hidden=void(); int main(){}",
        "alias-template": "template<class T> using Hidden=T; int main(){}",
        "narrow-enum": "enum class E:unsigned char{v=1}; int main(){}",
        "wide-enum": "enum class E:unsigned long long{v=1}; int main(){}",
        "bool-enum": "enum class E:bool{v=true}; int main(){}",
        "folded-enum-cast": "enum E:int{v=(static_cast<void>(0),1)}; int main(){}",
        "folded-assert-cast": "static_assert((static_cast<void>(0),true),\"condition\"); int main(){}",
        "folded-assert-type": "static_assert(1L==1L,\"condition\"); int main(){}",
        "runtime-string": "static_assert(true,\"message\"); const char *s=\"runtime\"; int main(){}",
    }
    v2_rejections.update({
        "temporary-reference": "int f(){const int&r=1; return r;}",
        "dead-temporary-reference": "int f(){if(false){const int&r=1;} return 0;}",
        "conversion-temporary": "int f(){int x=1; const unsigned int&r=x; return r;}",
        "temporary-subobject": "struct R{int x;}; int f(){const int&r=R{1}.x; return r;}",
        "temporary-reference-argument": "int f(const int&r){return r;} int main(){return f(1);}",
        "rvalue-reference": "int f(int&&r){return r;}",
        "reference-field": "struct R{int&r;};",
        "pointer-global": "int*const p=nullptr;",
        "reference-global": "const int x=1; const int&r=x;",
        "pointer-arithmetic": "int*f(int*p){return p+1;}",
        "pointer-ordering": "bool f(int*a,int*b){return a<b;}",
        "pointer-integer": "unsigned long long f(int*p){return (unsigned long long)p;}",
        "integer-pointer": "int*f(int x){return (int*)x;}",
        "unrelated-pointer-cast": "bool*f(int*p){return (bool*)p;}",
        "standalone-null-type": "int main(){auto p=nullptr;}",
    })
    v2_rejections.update({
        "zero-array": "int f(){int a[0]; return 0;}",
        "variable-array": "int f(int n){int a[n]; return 0;}",
        "dead-variable-array": "int f(int n){if(false){int a[n];} return 0;}",
        "global-array": "const int a[2]={1,2};",
        "global-array-field": "struct R{int a[2];}; constexpr R r{{1,2}};",
        "array-bound": "using Large=int[65537]; int main(){}",
        "array-product": "using Large=int[65536][65536]; int main(){}",
        "folded-functional-void": "static_assert((void(0),true)); int main(){}",
        "array-initialization-budget": "int f(){int a[65536]={}; return a[0];}",
        "array-temporary-comma": "struct R{int a[2];}; int f(){int n=0; const int&r=(++n,R{{1,2}}.a)[0]; return r;}",
        "array-temporary-dereference": "struct R{int a[2];}; int f(){const int&r=*R{{1,2}}.a; return r;}",
        "array-temporary-subobject": "struct R{int a[2];}; int f(){const int&r=R{{1,2}}.a[0]; return r;}",
    })
    v2_rejections.update({
        "switch-case-range": "int f(int n){switch(n){case 1 ... 3:return 7;default:return 9;}}",
        "switch-dead-range": "int f(int n){if(false){switch(n){case 1 ... 3:return 7;}}return 0;}",
        "switch-other-attribute": "int f(int n){switch(n){case 0:[[likely]];case 1:return 7;default:return 9;}}",
        "switch-folded-cast": "int f(int n){switch(n){case (void(0),1):return 7;default:return 9;}}",
    })
    for name, source in v2_rejections.items():
        check("v2-rejects-" + name, source, "TR0201", profile="cpp-core-v2")
    check("v2-failed-assert", "static_assert(false,\"must fail\"); int main(){}",
          "TR0202", profile="cpp-core-v2")

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
        "untyped-assembly-string": 'asm(""); int main(){}',
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

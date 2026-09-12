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
    core_v2.update({
        "narrow-enum": "enum class E:unsigned char{v=255}; bool f(E e){return e==E::v;}",
        "wide-enum": "enum class E:unsigned long long{v=0xffffffffffffffffull}; E f(){return E::v;}",
        "bool-enum": "enum class E:bool{off=false,on=true}; bool f(){return E::off<E::on;}",
        "long-assertion": "static_assert(1L==1L); int main(){}",
        "character-alias": "using Character=char; Character*f(Character*p){return p;}",
        "narrow-array": "int main(){unsigned char a[2]={255}; ++a[0]; return a[0]+a[1];}",
        "wide-array": "long long f(){long long a[2]={0x100000001ll}; return a[0];}",
        "signed-narrow": "signed char f(long long x){return static_cast<signed char>(x);}",
        "unsigned-wide": "unsigned long long f(int x){return static_cast<unsigned long long>(x);}",
        "narrow-promotion": "int f(unsigned char x){return x+1;}",
        "narrow-increment": "short f(short x){return ++x;}",
        "wide-shift": "long long f(long long x){return x>>63;}",
        "wide-switch": "int f(unsigned long long x){switch(x){case 0xffffffffffffffffull:return 1;default:return 0;}}",
        "bool-switch": "enum class E:bool{off=false,on=true}; int f(E e){switch(e){case E::on:return 1;default:return 0;}}",
        "narrow-switch": "enum class E:unsigned char{v=255}; int f(E e){switch(e){case E::v:return 1;default:return 0;}}",
        "size-alias": "using Size=decltype(sizeof(int)); Size f(){return sizeof(int);}",
        "unevaluated-size": "int f(){int n=0; auto size=sizeof(++n); return n;}",
        "reference-size": "int f(){int n=0;int&r=n;return sizeof(r)==sizeof(int&) ? 0:1;}",
        "record-alignment": "struct R{char c;long long n;}; auto f(){return alignof(R);}",
    })
    core_v2.update({
        "pointer-offset": "int*f(int*p,int n){return p+n;}",
        "integer-pointer-offset": "int*f(int*p,int n){return n+p;}",
        "pointer-back": "int*f(int*p,long long n){return p-n;}",
        "pointer-difference": "auto f(const int*a,int*b){return a-b;}",
        "pointer-row-difference": "auto f(int(*a)[3],const int(*b)[3]){return a-b;}",
        "pointer-increment": "int*f(int*p){return p++;}",
        "pointer-preincrement-reference": "int*&f(int*&p){return ++p;}",
        "pointer-compound-narrow": "int*f(int*p,unsigned char n){p+=n;return p;}",
        "pointer-zero": "int*f(int*p){return p+0;}",
        "pointer-null-difference": "auto f(){int*p=nullptr;return p-p;}",
        "pointer-address-cancel": "int*f(int*p,int n){return &p[n];}",
        "pointer-deref-cancel": "int*f(int*p){return &*p;}",
        "pointer-record-offset": "struct R{long long n;char c;};R*f(R*p){return p+1;}",
        "temporary-pointer-container": "struct E{int n;};struct H{E*p;};int f(E*p){int&r=H{p}.p->n;return r;}",
        "pointer-iterator-loop": "int f(int*p,int*end){int n=0;for(;p!=end;++p)n+=*p;return n;}",
    })
    core_v2.update({
        'method-out-of-line': 'struct R{int n;int get()const;};int R::get()const{return n;}int f(){R r{7};return r.get();}',
        'method-mutating-reference': 'struct R{int n;R&add(int v){n+=v;return *this;}};int f(){R r{1};return r.add(6).n;}',
        'method-array-reference': 'struct R{int a[2];int&front(){return a[0];}};int f(){R r{{1,2}};r.front()=7;return r.a[0];}',
        'method-static-temporary': 'struct R{int n;static int add(int x){return x+1;}};R make(int&n){++n;return {1};}int f(){int n=0;return make(n).add(n);}',
        'method-pointer-prvalue': 'struct R{int n;int get(){return n;}};R*id(R*p){return p;}int f(){R r{7};return id(&r)->get();}',
        'method-live-pointer-temporary-container': 'struct R{int n;int get(){return n;}};struct H{R*p;};int f(){R r{7};return H{&r}.p->get();}',
        'method-private-helper': 'struct R{int n;private:int impl()const{return n;}public:int get()const{return impl();}};int f(){R r{7};return r.get();}',
        'method-folded-valid-call': 'struct R{int n;static constexpr int get(){return 7;}};static_assert(R::get()==7);',
        'method-const-folded-live-call': 'struct R{int n;constexpr int get()const{return n;}};constexpr R r{7};static_assert(r.get()==7);',
        'method-discarded-partial-static': 'struct R{int n;static int get(){return 7;}};int f(){R r;return r.get();}',
    })
    core_v2.update({
        'constructor-default': 'struct R{int n;R():n(7){}};int f(){R r;return r.n;}',
        'constructor-converting': 'struct R{int n;R(int v):n(v){}};int f(){R r=7;return r.n;}',
        'constructor-explicit': 'struct R{int n;explicit R(int v):n(v){}};int f(){R r=R(7);return r.n;}',
        'constructor-out-of-line': 'struct R{int n;R(int);};R::R(int v):n(v){} int f(){R r(7);return r.n;}',
        'constructor-multiarg': 'struct R{int n;R(int a,int b):n(a+b){}};int f(){R r(3,4);return r.n;}',
        'constructor-const': 'struct R{int n;R(int v):n(v){}int get()const{return n;}};int f(){const R r(7);return r.get();}',
        'constructor-partial': 'struct R{int n,spare;R(int v):n(v){}int get()const{return n;}};int f(){R r(7);return r.get();}',
        'constructor-implicit-member': 'struct I{int n;I():n(7){}};struct R{I value;R(){}};int f(){R r;return r.value.n;}',
        'constructor-array-default': 'struct R{int n;R():n(7){}};int f(){R r[2];return r[1].n;}',
        'constructor-array-filler': 'struct R{int n;R():n(7){}R(int v):n(v){}};int f(){R r[2]={R(3)};return r[1].n;}',
        'constructor-matrix-default': 'struct R{int n;R():n(7){}};int f(){R r[2][2];return r[1][1].n;}',
        'constructor-field-array': 'struct I{int n;I():n(7){}};struct R{I value[2];R(){}};int f(){R r;return r.value[1].n;}',
        'constructor-early-return': 'struct R{int n;R(bool stop):n(7){if(stop)return;n=9;}};int f(){R r(true);return r.n;}',
        'constructor-folded-global': 'struct R{int n;constexpr R(int v):n(v){}};constexpr R r(7);static_assert(r.n==7);',
        'constructor-trivial-copy': 'struct R{int n;R(int v):n(v){}};int f(){R a(7);R b=a;b=a;return b.n;}',
        'constructor-record-result': 'struct R{int n;R(int v):n(v){}};R make(){return R(7);}int f(){return make().n;}',
    })
    core_v2.update({
        'call-record-recursive': 'struct R{int n;R(int v):n(v){}};R f(int n){if(!n)return R(1);return f(n-1);}int main(){return f(3).n-1;}',
        'call-record-array-field': 'struct R{int n[2];};R f(){return {{3,5}};}int main(){return f().n[1]-5;}',
        'call-record-qualified-result': 'struct R{int n;R(int v):n(v){}};const R f(){return R(7);}int main(){R r=f();return r.n-7;}',
        'call-record-aggregate-argument': 'struct R{int n;};int f(R r){return r.n;}int main(){return f({7})-7;}',
        'call-record-pointer-alias': 'struct R{int n;};int f(R r,R&source){r.n+=2;return source.n;}int main(){R r{7};return f(r,r)-7;}',
        'call-record-ordered-result': 'struct R{int a,b;};R f(R*p){return {7,p->a+2};}int main(){R r=f(&r);return r.b-9;}',
        'call-record-converting-return': 'struct R{int n;R(int v):n(v){}};R f(){return 7;}int main(){return f().n-7;}',
        'call-record-conditional-result': 'struct R{int n;R(int v):n(v){}};R f(int n){return R(n);}int main(){int n=3;R r=n==3?f(7):f(9);return r.n-7;}',
        'call-record-comma-result': 'struct R{int n;R(int v):n(v){}};R f(int n){return R(n);}int main(){int n=3;R r=(++n,f(n));return r.n-4;}',
    })
    core_v2.update({
        'destruction-local': 'struct R{int n;~R(){}};void f(){R r{1};}',
        'destruction-out-of-line': 'struct R{int n;~R();};R::~R(){n=0;}void f(){R r{1};}',
        'destruction-implicit-container': 'struct R{int n;~R(){}};struct Box{R r[2];};void f(){Box b{{{1},{2}}};}',
        'destruction-const': 'struct R{int n;~R(){n=0;}};void f(){const R r{1};}',
        'destruction-uninitialized': 'struct R{int n;~R(){n=0;}};void f(){R r;}',
        'destruction-array-filler': 'struct R{int n;R():n(7){}~R(){}};void f(){R r[2]={};}',
        'destruction-reference-call': 'struct R{int n;~R(){}};R&alias(R&r){return r;}int f(){R r{1};return alias(r).n;}',
        'destruction-member-initializer': 'struct R{int n;~R(){}};struct Box{int n;Box():n(R{1}.n){}};',
        'destruction-conditional': 'struct R{int n;~R(){}};int f(bool b){return (b?R{1}:R{2}).n;}',
        'destruction-parameter': 'struct R{int n;~R(){}};int f(R r){return r.n;}int main(){return f(R{1})-1;}',
        'destruction-result': 'struct R{int n;~R(){}};R f(){return {1};}int main(){R r=f();return r.n-1;}',
        'destruction-named-copy': 'struct R{int n;~R(){}};R f(R r){R copy=r;copy=r;return copy;}',
    })
    core_v2.update({
        'user-copy-constructor': 'struct R{int n;R(int v):n(v){}R(const R&r):n(r.n+1){}};int main(){R a(1);R b=a;return b.n-2;}',
        'user-copy-assignment': 'struct R{int n;R&operator=(const R&r){n=r.n+1;return *this;}};int main(){R a{1},b{2};a=b;return a.n-3;}',
        'user-copy-explicit-assignment': 'struct R{int n;R&operator=(const R&r){n=r.n+1;return *this;}};int main(){R a{1},b{2};a.operator=(b);return a.n-3;}',
        'user-copy-qualified-assignment': 'struct R{int n;R&operator=(const R&r)&{n=r.n+1;return *this;}};int main(){R a{1},b{2};a=b;return a.n-3;}',
        'user-copy-explicit-constructor': 'struct R{int n;R(int v):n(v){}explicit R(const R&r):n(r.n+1){}};int main(){R a(1);R b(a);return b.n-2;}',
        'user-copy-array': 'struct R{int n;R(int v):n(v){}R(const R&r):n(r.n+1){}};int main(){R a(1);R b[2]={a,a};return b[1].n-2;}',
        'user-copy-return': 'struct R{int n;R(int v):n(v){}R(const R&r):n(r.n+1){}};R f(const R&r){return r;}int main(){R a(1);return f(a).n-2;}',
        'user-copy-named-return': 'struct R{int n;R(int v):n(v){}R(const R&r):n(r.n+1){}};R f(){R r(1);return r;}int main(){return f().n-2;}',
        'user-copy-parameter': 'struct R{int n;R(int v):n(v){}R(const R&r):n(r.n+1){}};int f(R r){return r.n;}int main(){R a(1);return f(a)-2;}',
        'user-copy-containing-aggregate': 'struct R{int n;R(int v):n(v){}R(const R&r):n(r.n+1){}~R(){}};struct Box{R r;};int main(){R a(1);Box b{a};return b.r.n-2;}',
        'user-copy-constexpr': 'struct R{int n;constexpr R(int v):n(v){}constexpr R(const R&r):n(r.n+1){}};constexpr R a(1);constexpr R b=a;static_assert(b.n==2);',
    })
    core_v2.update({
        'lifecycle-implicit-default': 'struct I{int n;I():n(7){}};struct R{I i;};int f(){R r;return r.i.n;}',
        'lifecycle-implicit-array-default': 'struct I{int n;I():n(7){}};struct R{I i[2];};int f(){R r;return r.i[1].n;}',
        'lifecycle-defaulted-nontrivial': 'struct I{int n;I():n(7){}};struct R{I i;R()=default;};int f(){R r;return r.i.n;}',
        'lifecycle-explicit-trivial-default': 'struct R{int n;explicit R()=default;};int f(){R r;r.n=7;return r.n;}',
        'lifecycle-explicit-trivial-value': 'struct R{int n;explicit R()=default;};int f(){R r=R();return r.n;}',
        'lifecycle-defaulted-unused': 'struct R{int n;explicit R()=default;~R()=default;};',
        'lifecycle-defaulted-out-of-line': 'struct R{int n;R();};R::R()=default;int f(){R r;r.n=7;return r.n;}',
        'lifecycle-implicit-value-zero': 'struct I{int n;I():n(7){}};struct R{int n;I i;};int f(){R r=R();return r.n;}',
        'lifecycle-unevaluated-lazy': 'struct I{int n;I(){n=7;}};struct R{I i;explicit R()=default;};int f(){return sizeof(R{});}',
        'lifecycle-defaulted-member-destructor': 'struct I{int n;~I(){}};struct R{I i;~R()=default;};void f(){R r{{1}};}',
        'lifecycle-out-of-line-destructor': 'struct I{int n;~I(){}};struct R{I i;~R();};R::~R()=default;void f(){R r{{1}};}',
        'lifecycle-defaulted-trivial-destructor': 'struct R{int n;~R()=default;};int f(){R r{7};return r.n;}',
    })
    core_v2.update({
        'generated-copy-implicit-members': 'struct I{int n;I(const I&s):n(s.n+1){}};struct R{I i;~R()=default;};R f(const R&s){return s;}',
        'generated-copy-array-copy': 'struct I{int n;I(const I&s):n(s.n+1){}};struct R{I i[2];~R()=default;};R f(const R&s){return s;}',
        'generated-copy-nested-array-copy': 'struct I{int n;I(const I&s):n(s.n+1){}};struct R{I i[2][3];~R()=default;};R f(const R&s){return s;}',
        'generated-copy-defaulted-copy': 'struct I{int n;I(const I&s):n(s.n+1){}};struct R{I i;R(const R&)=default;};R f(const R&s){return s;}',
        'generated-copy-explicit-copy': 'struct I{int n;I(const I&s):n(s.n+1){}};struct R{I i;explicit R(const R&)=default;};void f(const R&s){R r(s);}',
        'generated-copy-out-of-line-copy': 'struct I{int n;I(const I&s):n(s.n+1){}};struct R{I i;R(const R&);};R::R(const R&)=default;R f(const R&s){return s;}',
        'generated-copy-trivial-bodyless-copy': 'struct R{int n;R(const R&)=default;};R f(const R&s){return s;}',
        'generated-copy-unused-defaulted-copy': 'struct R{int n;R(const R&)=default;};',
        'generated-copy-unevaluated-lazy-copy': 'struct I{int n;I(const I&s){n=s.n+1;}};struct R{I i;R(const R&)=default;};int f(const R&s){return sizeof(R(s));}',
        'generated-copy-mutable-source': 'struct I{int n;I(I&s):n(++s.n){}};struct R{I i[2];~R()=default;};R f(R&s){return s;}',
    })
    core_v2.update({
        'generated-assignment-implicit-nontrivial': 'struct I{int n;I&operator=(const I&s){n=s.n+1;return *this;}};struct R{I i;};void f(R&a,const R&b){a=b;}',
        'generated-assignment-defaulted-nontrivial': 'struct I{int n;I&operator=(const I&s){n=s.n+1;return *this;}};struct R{I i;R&operator=(const R&)=default;};void f(R&a,const R&b){a=b;}',
        'generated-assignment-out-of-line': 'struct R{int n[2];R&operator=(const R&);};R&R::operator=(const R&)=default;void f(R&a,const R&b){a=b;}',
        'generated-assignment-qualified': 'struct R{int n;R&operator=(const R&) & =default;};R&f(R&a,const R&b){return a=b;}',
        'generated-assignment-trivial-operator': 'struct R{int n;R&operator=(const R&)=default;};R&f(R&a,const R&b){return a=b;}',
        'generated-assignment-trivial-member': 'struct R{int n;R&operator=(const R&)=default;};R&f(R&a,const R&b){return a.operator=(b);}',
        'generated-assignment-trivial-arrow': 'struct R{int n;R&operator=(const R&)=default;};R&f(R*a,const R&b){return a->operator=(b);}',
        'generated-assignment-implicit-member': 'struct R{int n;};R&f(R&a,const R&b){return a.operator=(b);}',
        'generated-assignment-unused': 'struct R{int n;R&operator=(const R&)=default;};',
        'generated-assignment-unevaluated-lazy': 'struct I{int n;I&operator=(const I&s){n=s.n+1;return *this;}};struct R{I i;R&operator=(const R&)=default;};int f(R&a,const R&b){return sizeof(a=b);}',
        'generated-assignment-nested-array': 'struct I{int n;I&operator=(const I&s){n=s.n+1;return *this;}};struct R{I i[2][3];};void f(R&a,const R&b){a=b;}',
        'generated-assignment-scalar-array': 'struct I{int n;I&operator=(const I&s){n=s.n+1;return *this;}};struct R{int a[2][3];I i;};void f(R&a,const R&b){a=b;}',
        'generated-assignment-mutable-source': 'struct I{int n;I&operator=(I&s){n=++s.n;return *this;}};struct R{I i[2];};void f(R&a,R&b){a=b;}',
        'generated-assignment-nontrivial-lifetime-trivial-assignment': 'struct I{int n;I(const I&s):n(s.n){}~I(){}};struct J{int n;J&operator=(const J&s){n=s.n;return *this;}};struct R{I i[2];J j;};void f(R&a,const R&b){a=b;}',
    })
    core_v2.update({
        'generated-copy-implicit-containing-copy': 'struct R{int n;R(int v):n(v){}R(const R&r):n(r.n+1){}};struct Box{R r;};void f(){Box a{R(1)};Box b=a;}',
        'generated-copy-implicit-containing-copy-dead': 'struct R{int n;R(int v):n(v){}R(const R&r):n(r.n+1){}};struct Box{R r;};void f(){Box a{R(1)};if(false){Box b=a;}}',
    })
    core_v2.update({
        'default-member-scalar-self': 'struct R{int n=3;int next=n+4;R*self=this;};int main(){R r;return r.next==7&&r.self==&r?0:1;}',
        'default-member-const-object': 'struct R{int n=3;R*self=this;};int main(){const R r{};return r.self!=&r;}',
        'default-member-array-default': 'struct R{int n=3;int values[2][2]={{n},{n+1}};};int main(){R r[2]={{4}};return r[0].values[1][0]==5&&r[1].values[1][0]==4?0:1;}',
        'default-member-member-call': 'struct R{int n=3;int get(){return n;}int next=get();};int main(){R r{};return r.next-3;}',
        'default-member-unused': 'struct R{int n=1;R*self=this;};',
        'default-member-old-assignment': 'struct R{int n=1;R&operator=(const R&)=default;};',
        'default-member-old-copy': 'struct R{int n=1;R(const R&)=default;};',
        'default-member-old-copy-query': 'struct R{int n=1;R(const R&)=default;};int f(const R&r){return sizeof(R(r));}',
        'default-member-old-default': 'struct R{int n=1;R()=default;};',
        'default-member-old-default-query': 'struct R{int n=1;explicit R()=default;};int f(){return sizeof(R{});}',
        'default-member-old-user-constructor': 'struct R{int n=1;R(){}};',
    })
    core_v2.update({
        'live-rvalue-old-reference': 'int f(int&&r){return r;}',
        'live-rvalue-old-method': 'struct R{int n;int get()&&{return n;}};',
        'live-rvalue-scalar-chain': 'int&&f(int&&r){return static_cast<int&&>(r);}int main(){int n=1;int&&r=f(static_cast<int&&>(n));r=2;return n-2;}',
        'live-rvalue-const-chain': 'const int&&f(const int&&r){return static_cast<const int&&>(r);}int main(){const int n=1;const int&&r=f(static_cast<const int&&>(n));return &r!=&n;}',
        'live-rvalue-array-reference': 'using Row=int[2];Row&&f(Row&&r){return static_cast<Row&&>(r);}int main(){Row a{};Row&&r=f(static_cast<Row&&>(a));r[0]=1;return a[0]-1;}',
        'live-rvalue-collapsed-reference': 'using R=int&&;using L=R&;int main(){int n=1;L l=n;R r=static_cast<R>(n);l=2;return r-2;}',
        'live-rvalue-const-rvalue-method': 'struct R{int n;int get()const&&{return n;}};int f(const R&r){return static_cast<const R&&>(r).get();}',
        'live-rvalue-live-assignment': 'struct R{int n;};R&f(R&a,const R&b){return static_cast<R&&>(a)=b;}',
        'live-rvalue-default-this': 'struct R{int n=3;int get()&&{return n;}int next=static_cast<R&&>(*this).get();};int main(){R r{};return r.next-3;}',
    })
    core_v2.update({
        'user-move-move-constructor': 'struct R{int n;R(R&&r):n(r.n){}};',
        'user-move-move-assignment': 'struct R{int n;R&operator=(R&&r){n=r.n;return *this;}};',
        'user-move-move-default-member': 'struct R{int n=1;R(R&&s):n(s.n){}};',
        'user-move-move-with-destructor': 'struct R{int n;R(R&&r):n(r.n){}~R(){}};',
        'user-move-move-prvalue': 'struct R{int n;R(int v):n(v){}R(R&&x):n(x.n){}};R f(){return R(1);}',
        'user-move-const-move': 'struct R{int n;R(const R&&r):n(r.n){}};',
        'user-move-const-move-assignment': 'struct R{int n;R&operator=(const R&&r){n=r.n;return *this;}};',
        'user-move-qualified-assignment': 'struct R{int n;R&operator=(R&&r)&&{n=r.n;return *this;}};R&f(R&a,R&b){return static_cast<R&&>(a)=static_cast<R&&>(b);}',
        'user-move-explicit-move': 'struct R{int n;R(int v):n(v){}explicit R(R&&r):n(r.n){}};int main(){R a(1);R b(static_cast<R&&>(a));return b.n-1;}',
        'user-move-copy-fallback': 'struct R{int n;R(const R&r):n(r.n+1){}};R f(R&r){return static_cast<R&&>(r);}',
        'user-move-named-rvalue-copy': 'struct R{int n;R(const R&r):n(r.n+1){}R(R&&r):n(r.n+2){}};R f(R&&r){return R(r);}',
        'user-move-previously-rejected-2': 'struct R{int n;R(R&&s):n(s.n){}};',
        'user-move-previously-rejected-3': 'struct R{int n;R&operator=(R&&s){n=s.n;return *this;}};',
        'user-move-previously-rejected-7': 'struct R{int n;R(int v):n(v){} R(R&&v):n(v.n){}};',
    })
    core_v2.update({
        'generated-move-defaulted-constructor': 'struct R{int n;R(R&&)=default;};',
        'generated-move-defaulted-assignment': 'struct R{int n;R&operator=(R&&)=default;};',
        'generated-move-implicit-nontrivial-constructor': 'struct I{int n;I(I&&s):n(s.n+1){s.n=-1;}};struct R{I items[2];};R f(R&&s){return static_cast<R&&>(s);}',
        'generated-move-implicit-nontrivial-assignment': 'struct I{int n;I&operator=(I&&s){n=s.n+1;s.n=-1;return *this;}};struct R{I items[2];};R&f(R&a,R&b){return a=static_cast<R&&>(b);}',
        'generated-move-copy-fallback-constructor': 'struct I{int n;I(const I&s):n(s.n+1){}};struct R{I items[2];R(R&&)=default;};R f(R&&s){return static_cast<R&&>(s);}',
        'generated-move-copy-fallback-assignment': 'struct I{int n;I&operator=(const I&s){n=s.n+1;return *this;}};struct R{I items[2];R&operator=(R&&)=default;};R&f(R&a,R&b){return a=static_cast<R&&>(b);}',
        'generated-move-trivial-operator': 'struct R{int n[2];R&operator=(R&&)=default;};R&f(R&a,R&b){return a=static_cast<R&&>(b);}',
        'generated-move-trivial-member': 'struct R{int n[2];R&operator=(R&&)=default;};R&f(R&a,R&b){return a.operator=(static_cast<R&&>(b));}',
        'generated-move-trivial-arrow': 'struct R{int n[2];R&operator=(R&&)=default;};R&f(R*a,R*b){return a->operator=(static_cast<R&&>(*b));}',
        'generated-move-rvalue-qualified': 'struct R{int n;R&operator=(R&&)&&=default;};R&f(R&a,R&b){return static_cast<R&&>(a)=static_cast<R&&>(b);}',
        'generated-move-unused-nontrivial': 'struct I{int n;I(I&&s):n(s.n+1){}};struct R{I i;R(R&&)=default;};',
        'generated-move-unevaluated': 'struct I{int n;I(I&&s):n(s.n+1){}};struct R{I i;R(R&&)=default;};int f(R&&s){return sizeof(R(static_cast<R&&>(s)));}',
        'generated-move-inline-temporary-assignment': 'struct R{int n;};int f(){R a{1};a=R{2};return (R{3}=R{4}).n+a.n;}',
        'generated-move-inline-temporary-construction': 'struct R{int n;};int f(){R a(static_cast<R&&>(R{1}));return a.n;}',
    })
    core_v2.update({
        'noexcept-562-noexcept-constructor': 'struct R{int n;R(const R&r)noexcept:n(r.n){}};',
        'noexcept-562-noexcept-assignment': 'struct R{int n;R&operator=(const R&r)noexcept{n=r.n;return *this;}};',
        'noexcept-761-copy-noexcept': 'struct R{int n;R(const R&)noexcept=default;};',
        'noexcept-761-copy-noexcept-false': 'struct R{int n;R(const R&)noexcept(false)=default;};',
        'noexcept-761-copy-throw': 'struct R{int n;R(const R&)throw()=default;};',
        'noexcept-761-out-of-line-noexcept': 'struct R{int n;R(const R&)noexcept;};R::R(const R&)noexcept=default;',
        'noexcept-886-noexcept': 'struct R{int n;R&operator=(const R&)noexcept=default;};',
        'noexcept-886-noexcept-false': 'struct R{int n;R&operator=(const R&)noexcept(false)=default;};',
        'noexcept-886-out-of-line-noexcept': 'struct R{int n;R&operator=(const R&)noexcept;};R&R::operator=(const R&)noexcept=default;',
        'noexcept-1147-method-noexcept': 'struct R{int n;int get()&&noexcept{return n;}};',
        'noexcept-1286-constructor-noexcept': 'struct R{int n;R(R&&r)noexcept:n(r.n){}};',
        'noexcept-1286-assignment-noexcept': 'struct R{int n;R&operator=(R&&r)noexcept{n=r.n;return *this;}};',
        'noexcept-1476-constructor-noexcept': 'struct R{int n;R(R&&)noexcept=default;};',
        'noexcept-1476-constructor-noexcept-false': 'struct R{int n;R(R&&)noexcept(false)=default;};',
        'noexcept-1476-constructor-throw': 'struct R{int n;R(R&&)throw()=default;};',
        'noexcept-1476-assignment-noexcept': 'struct R{int n;R&operator=(R&&)noexcept=default;};',
        'noexcept-1476-assignment-noexcept-false': 'struct R{int n;R&operator=(R&&)noexcept(false)=default;};',
        'noexcept-1476-out-of-line-noexcept': 'struct R{int n;R(R&&)noexcept;};R::R(R&&)noexcept=default;',
        'noexcept-1476-out-of-line-assignment-noexcept': 'struct R{int n;R&operator=(R&&)noexcept;};R&R::operator=(R&&)noexcept=default;',
        'noexcept-1592-constructor-noexcept': 'struct R{int n;R()noexcept=default;};',
        'noexcept-1592-constructor-noexcept-false': 'struct R{int n;R()noexcept(false)=default;};',
        'noexcept-1592-destructor-noexcept': 'struct R{int n;~R()noexcept=default;};',
        'noexcept-1592-destructor-throw': 'struct R{int n;~R()throw()=default;};',
        'noexcept-1592-out-of-line-noexcept': 'struct R{int n;R()noexcept;};R::R()noexcept=default;',
        'noexcept-1592-out-of-line-destructor-noexcept': 'struct R{int n;~R()noexcept;};R::~R()noexcept=default;',
        'noexcept-1710-explicit-noexcept': 'struct R{int n;~R()noexcept{}};',
        'noexcept-1710-explicit-noexcept-false': 'struct R{int n;~R()noexcept(false){}};',
        'noexcept-1710-explicit-empty-throw': 'struct R{int n;~R()throw(){}};',
        'noexcept-1958-method-noexcept-method': 'struct R{int n;int get()const noexcept{return n;}};',
        'noexcept-1985-constructor-noexcept-constructor': 'struct R{int n;R() noexcept:n(1){}};',
    })
    core_v2.update({
        'operator-user_copy_rejected-const-assignment': 'struct R{int n;R&operator=(const R&r)const{return const_cast<R&>(*this);}};',
        'operator-user_copy_rejected-rvalue-assignment': 'struct R{int n;R&operator=(const R&r)&&{n=r.n;return *this;}};',
        'operator-user_copy_rejected-by-value-assignment': 'struct R{int n;R&operator=(R r){n=r.n;return *this;}};',
        'operator-user_copy_rejected-void-assignment': 'struct R{int n;void operator=(const R&r){n=r.n;}};',
        'operator-user_copy_rejected-other-assignment-result': 'struct R{int n;int&operator=(const R&r){n=r.n;return n;}};',
        'operator-user_copy_rejected-arbitrary-operator': 'struct R{int n;R operator+(const R&r){return {n+r.n};}};',
        'operator-user_move_rejected-const-assignment-receiver': 'struct R{int n;R&operator=(R&&)const{return const_cast<R&>(*this);}};',
        'operator-user_move_rejected-assignment-void-result': 'struct R{int n;void operator=(R&&r){n=r.n;}};',
        'operator-user_move_rejected-assignment-const-result': 'struct R{int n;const R&operator=(R&&r){n=r.n;return *this;}};',
        'operator-user_move_rejected-assignment-other-result': 'struct R{int n;int&operator=(R&&r){n=r.n;return n;}};',
        'operator-user_move_rejected-assignment-value-source': 'struct R{int n;R&operator=(R r){n=r.n;return *this;}};',
        'operator-user_move_rejected-arbitrary-operator': 'struct R{int n;R operator+(R&&r){return {n+r.n};}};',
        'operator-method-call': 'struct R{int n;int operator()()const{return n;}};',
    })
    core_v2.update({
        'conversion-conversion': 'struct R{int n;operator int()const{return n;}};',
        'conversion-method-conversion': 'struct R{int n;operator int()const{return n;}};',
        'conversion-constructor-conversion-function': 'struct R{int n;R():n(1){} operator int()const{return n;}};int f(){R r;return r;}',
    })
    core_v2.update({
        'temporary-call-940-temporary-source': 'struct R{int n;R&operator=(const R&)=default;};void f(R&r){r=R{1};}',
        'temporary-call-941-temporary-receiver': 'struct R{int n;R&operator=(const R&)=default;};void f(const R&s){R{1}=s;}',
        'temporary-call-1088-temporary-reference': 'int take(const int&n){return n;}struct R{int n=take(1);};',
        'temporary-call-1089-temporary-receiver': 'struct A{int n;int get(){return n;}};struct R{int n=A{1}.get();};',
        'temporary-call-1206-temporary-method': 'struct R{int n;int get()&&{return n;}};int f(){return R{1}.get();}',
        'temporary-call-1207-temporary-cast-method': 'struct R{int n;int get()&&{return n;}};int f(){return static_cast<R&&>(R{1}).get();}',
        'temporary-call-1208-temporary-callee-argument': 'struct R{int n;};R&&id(R&&r){return static_cast<R&&>(r);}void f(){R&&r=id(R{1});}',
        'temporary-call-1341-temporary-constructor-source': 'struct R{int n;R(int v):n(v){}R(R&&r):n(r.n){}};void f(){R r(static_cast<R&&>(R(1)));}',
        'temporary-call-1342-temporary-assignment-source': 'struct R{int n;R&operator=(R&&r){n=r.n;return *this;}};void f(R&r){r=R{1};}',
        'temporary-call-1343-temporary-assignment-receiver': 'struct R{int n;R&operator=(R&&r){n=r.n;return *this;}};void f(R&r){R{1}=static_cast<R&&>(r);}',
        'temporary-call-1524-temporary-defaulted-constructor': 'struct R{int n;R(R&&)=default;};void f(){R r(static_cast<R&&>(R{1}));}',
        'temporary-call-1525-temporary-defaulted-assignment-source': 'struct R{int n;R&operator=(R&&)=default;};void f(R&r){r=R{1};}',
        'temporary-call-1526-temporary-defaulted-assignment-receiver': 'struct R{int n;R&operator=(R&&)=default;};void f(R&r){R{1}=static_cast<R&&>(r);}',
        'temporary-call-1527-temporary-implicit-member-source': 'struct R{int n;};void f(R&r){r.operator=(R{1});}',
        'temporary-call-1528-temporary-implicit-member-receiver': 'struct R{int n;};void f(R&r){R{1}.operator=(static_cast<R&&>(r));}',
        'temporary-call-1530-temporary-nontrivial-implicit-constructor': 'struct I{int n;I(int v):n(v){}I(I&&r):n(r.n){}};struct R{I i;};void f(){R r(static_cast<R&&>(R{I(1)}));}',
        'temporary-call-1531-temporary-nontrivial-implicit-assignment': 'struct I{int n;I&operator=(I&&r){n=r.n;return *this;}};struct R{I i;};void f(R&r){r=R{{1}};}',
        'temporary-call-1677-temporary-receiver': 'struct R{int n;int get()noexcept{return n;}};bool f(){return noexcept(R{1}.get());}',
        'temporary-call-1678-temporary-reference': 'int take(const int&n)noexcept{return n;}bool f(){return noexcept(take(1));}',
        'temporary-call-1853-temporary-receiver': 'struct R{int n;int operator()()const{return n;}};int f(){return R{1}();}',
        'temporary-call-1854-temporary-reference': 'struct R{int n;};int operator+(const R&a,const R&b){return a.n+b.n;}int f(R&r){return r+R{1};}',
        'temporary-call-1855-unevaluated-temporary': 'struct R{int n;int operator()()const noexcept{return n;}};bool f(){return noexcept(R{1}());}',
        'temporary-call-2047-fresh-receiver': 'struct R{int n;operator int()const{return n;}};int f(){return R{1};}',
        'temporary-call-2053-query-fresh-receiver': 'struct R{operator int()const noexcept{return 1;}};bool f(){return noexcept(static_cast<int>(R{}));}',
        'temporary-call-2270-temporary-receiver': 'struct R{int n;~R(){}int get(){return n;}};int f(){return R{1}.get();}',
        'temporary-call-2449-temporary-reference-argument': 'int f(const int&r){return r;} int main(){return f(1);}',
        'temporary-call-2505-method-temporary-dot': 'struct R{int n;int get()const{return n;}};int f(){return R{1}.get();}',
        'temporary-call-2506-method-temporary-arrow': 'struct E{int n;int get()const{return n;}};struct H{E a[1];};int f(){return H{{{1}}}.a->get();}',
        'temporary-call-2507-method-temporary-arrow-offset': 'struct E{int n;int get()const{return n;}};struct H{E a[1];};int f(){return (H{{{1}}}.a+0)->get();}',
        'temporary-call-2508-method-dead-temporary-receiver': 'struct R{int n;int get()const{return n;}};int f(){if(false)return R{1}.get();return 0;}',
        'temporary-call-2509-method-folded-temporary-receiver': 'struct R{int n;constexpr int get()const{return n;}};static_assert(R{1}.get()==1);',
        'temporary-call-2523-method-method-temporary-reference-argument': 'struct R{int n;int get(const int&v){return n+v;}};int f(){R r{1};return r.get(2);}',
        'temporary-call-2524-method-static-temporary-reference-argument': 'struct R{int n;static int get(const int&v){return v;}};int f(){return R::get(2);}',
        'temporary-call-2526-method-temporary-reverse-arrow-offset': 'struct E{int n;int get()const{return n;}};struct H{E a[1];};int f(){return (0+H{{{1}}}.a)->get();}',
        'temporary-call-2546-constructor-temporary-reference-argument': 'struct R{int n;R(const int &v):n(v){}};int f(){R r(1);return r.n;}',
        'temporary-call-2547-constructor-dead-temporary-reference-argument': 'struct R{int n;R(const int &v):n(v){}};int f(){if(false){R r(1);return r.n;}return 0;}',
        'temporary-call-2548-constructor-temporary-method-receiver': 'struct R{int n;R(int v):n(v){} int get()const{return n;}};int f(){return R(1).get();}',
        'temporary-call-2549-constructor-temporary-subobject-receiver': 'struct I{int n;int get()const{return n;}};struct R{I i;R():i{1}{}};int f(){return R().i.get();}',
        'temporary-call-2550-constructor-temporary-array-receiver': 'struct I{int n;int get()const{return n;}};struct R{I i[1];R():i{{1}}{}};int f(){return (R().i+0)->get();}',
        'temporary-call-620-temporary-assignment-source': 'struct R{int n;R(int v):n(v){}R&operator=(const R&r){n=r.n;return *this;}};void f(){R r(1);r=R(2);}',
        'temporary-call-1198-reference-argument': 'int f(int&&n){return n;}int main(){return f(1);}',
        'temporary-call-user-copy-receiver': 'struct R{int n;R(int v):n(v){}R&operator=(const R&r){n=r.n;return *this;}};void f(){R r(1);R(2)=r;}',
        'temporary-call-cpp-scalar-reference-argument': 'int f(const int &x){return x;} int main(){return f(42);}',
    })
    core_v2.update({
        'automatic-reference-promoted-1': 'struct I{int n;I(const I&s):n(s.n){}};struct R{I i;R(const R&)=default;};void f(const R&s){const R&r=R(s);}',
        'automatic-reference-promoted-2': 'void f(){int&&r=1;}',
        'automatic-reference-promoted-3': 'void f(){int&&r=static_cast<int&&>(1);}',
        'automatic-reference-promoted-4': 'void f(){const int&&r=1;}',
        'automatic-reference-promoted-5': 'struct R{int n;};void f(){R&&r=R{1};}',
        'automatic-reference-promoted-6': 'struct R{int n;};void f(){R&&r=static_cast<R&&>(R{1});}',
        'automatic-reference-promoted-7': 'struct R{int n;};void f(){int&&r=R{1}.n;}',
        'automatic-reference-promoted-8': 'struct R{int a[2];};void f(){int&&r=R{{1,2}}.a[0];}',
        'automatic-reference-promoted-9': 'struct R{int n;};void f(bool b,R&live){R&&r=b?static_cast<R&&>(live):R{1};}',
        'automatic-reference-promoted-10': 'struct R{int n;};void f(){int n=0;R&&r=(++n,R{1});}',
        'automatic-reference-promoted-11': 'struct R{int n;operator int()const{return n;}};int f(){R r{1};const int&n=r;return n;}',
        'automatic-reference-promoted-12': 'int f(){const int&r=1;return r;}',
        'automatic-reference-promoted-13': 'int f(){int&&r=1;return r;}',
        'automatic-reference-promoted-14': 'struct R{int n;};int f(){const R&r=R{1};return r.n;}',
        'automatic-reference-promoted-15': 'struct R{int n;};int f(){R&&r=R{1};return r.n;}',
        'automatic-reference-promoted-16': 'struct R{int n;};int f(){const int&r=R{1}.n;return r;}',
        'automatic-reference-promoted-17': 'struct R{int a[2];};int f(){const int&r=R{{1,2}}.a[0];return r;}',
        'automatic-reference-promoted-18': 'struct R{int n;explicit R()=default;};int f(){const R&r=R{};return r.n;}',
        'automatic-reference-promoted-19': 'struct R{int n;~R(){}};int f(){const R&r=R{1};return r.n;}',
        'automatic-reference-promoted-20': 'int f(){const int&r=1; return r;}',
        'automatic-reference-promoted-21': 'int f(){if(false){const int&r=1;} return 0;}',
        'automatic-reference-promoted-22': 'int f(){int x=1; const unsigned int&r=x; return r;}',
        'automatic-reference-promoted-23': 'struct R{int x;}; int f(){const int&r=R{1}.x; return r;}',
        'automatic-reference-promoted-24': 'struct R{int a[2];}; int f(){int n=0; const int&r=(++n,R{{1,2}}.a)[0]; return r;}',
        'automatic-reference-promoted-25': 'struct R{int a[2];}; int f(){const int&r=R{{1,2}}.a[0]; return r;}',
        'automatic-reference-promoted-26': 'struct R{int a[2];}; int f(){const int &r=R{{1,2}}.a[0]; return r;}',
        'automatic-reference-promoted-27': 'struct R{int a[2];}; int f(){int n=0; const int &r=(++n,R{{1,2}}.a)[0]; return r;}',
        'automatic-reference-promoted-28': 'int f(){const int &value=42; return value;}',
        'automatic-reference-promoted-29': 'int f(){if(false){const int &value=42;} return 0;}',
        'automatic-reference-promoted-30': 'int f(){int x=1; const unsigned int &r=x; return r;}',
        'automatic-reference-promoted-31': 'struct R{int x;}; int f(){const int &r=R{1}.x; return r;}',
        'automatic-reference-brace-scalar': 'int f(){const int&r{1};return r;}',
        'automatic-reference-equal-brace-record': 'struct R{int n;};int f(){const R&r={R{2}};return r.n;}',
        'automatic-reference-brace-subobject': 'struct R{int n;};int f(){int&&r{R{3}.n};return ++r;}',
        'automatic-reference-brace-live': 'int f(int&n){int&r{n};return ++r;}',
        'automatic-reference-brace-parameter': 'int take(const int&n){return n;}int f(){return take({4});}',
        'automatic-reference-brace-constructor-parameter': 'struct R{int n;R(const int&r):n(r){}};int f(){R r({5});return r.n;}',
        'automatic-reference-brace-record-conversion': 'struct V{int n;};struct R{int n;operator V()const{return V{n};}};int f(){R r{6};const V&v={r};return v.n;}',
        'automatic-reference-constexpr-reference': 'struct R{int n;};constexpr int f(){const R&r{R{7}};return r.n;}constexpr int n=f();static_assert(n==7);',
    })
    core_v2.update({
        'array-temporary-promoted-array-argument': 'using A=int[2];void take(const int(&)[2]){}void f(){take(A{1,2});}',
        'array-temporary-promoted-dead-array-argument': 'using A=int[2];void take(const int(&)[2]){}void f(){if(false)take(A{1,2});}',
        'array-temporary-promoted-query-array-argument': 'using A=int[2];void take(const int(&)[2])noexcept{}bool f(){return noexcept(take(A{1,2}));}',
        'array-temporary-promoted-query-array-expression': 'using A=int[2];bool f(){return noexcept(A{1,2}[0]);}',
        'array-temporary-promoted-array-owner': 'void f(){const int(&r)[2]={1,2};}',
        'array-temporary-promoted-dead-array-owner': 'void f(){if(false){const int(&r)[2]={1,2};}}',
        'array-temporary-promoted-constexpr-array-owner': 'constexpr int f(){const int(&r)[2]={1,2};return r[0];}constexpr int n=f();',
        'array-temporary-promoted-query-braced-array': 'void take(const int(&)[2])noexcept{}bool f(){return noexcept(take({1,2}));}',
        'array-temporary-promoted-braced-array-argument': 'void take(const int(&)[2]){}void f(){take({1,2});}',
        'array-temporary-scalar-discard': 'using A=int[2];void f(){A{1,2};}',
        'array-temporary-record-discard': 'struct R{int n;~R(){}};using A=R[2];void f(){A{{1},{2}};}',
        'array-temporary-scalar-decay': 'using A=int[2];int read(const int*p){return p[0]+p[1];}int f(){return read(A{1,2});}',
        'array-temporary-scalar-index': 'using A=int[2];int f(){return A{1,2}[1];}',
        'array-temporary-record-element-receiver': 'struct R{int n;int get()const{return n;}};using A=R[2];int f(){return A{{1},{2}}[1].get();}',
        'array-temporary-rvalue-array-reference': 'using A=int[2];int f(){A&&r={1,2};return ++r[1];}',
        'array-temporary-direct-element-extension': 'using A=int[2];int f(){const int&r=A{1,2}[1];return r;}',
        'array-temporary-row-extension': 'using A=int[2][2];int f(){const int(&r)[2]=A{{1,2},{3,4}}[1];return r[0];}',
        'array-temporary-array-comma': 'using A=int[2];int f(){int n=0;const A&r=(++n,A{1,2});return n+r[0];}',
        'array-temporary-braced-live-array': 'using A=int[2];int f(A&a){A&r{a};return ++r[0];}',
        'array-temporary-array-rvalue-parameter': 'using A=int[2];int read(A&&a){return ++a[0];}int f(){return read(A{1,2});}',
    })
    for name, source in core_v2.items():
        check("v2-" + name, source, profile="cpp-core-v2")
    # The generated C++17 record calling convention owns parameter/result
    # objects explicitly. These checks pin NeverC's choice, not a universal
    # identity guarantee for trivial classes under [class.temporary].
    call_storage_source = """struct R {
  int value; R *self;
  explicit R(int n):value(n),self(this){}
  R combine(R other) const { return R(value+other.value); }
  static R build(int n) { return R(n); }
};
struct Box { R value; explicit Box(R x):value(x.value){} };
R make(int n) { return R(n); }
R forward(int n) { return make(n); }
R named(int n) { R local(n); return local; }
R identity(R value) { return value; }
int read(R value) { return value.value; }
int constant(const R value) { return value.value; }
int two(R first,R second) { first.value=1;return second.value; }
R& alias(R& value) { return value; }
int main() {
  R x=make(3);
  R y=forward(5);
  R z=x.combine(R(7));
  Box box(R(11));
  int first=read(R(13));
  int second=two(x,x);
  return first+second;
}
"""
    call_storage = check("v2-record-call-storage", call_storage_source, profile="cpp-core-v2")
    storage_records = {r["loc"]["line"]: r["id"] for r in call_storage["records"]}
    rid, bid = storage_records[1], storage_records[7]
    storage_signatures = {
        3: ("void", ["ptr:" + rid, "int"]),
        4: ("void", ["ptr:" + rid, "cptr:" + rid, "ptr:" + rid]),
        5: ("void", ["ptr:" + rid, "int"]),
        7: ("void", ["ptr:" + bid, "ptr:" + rid]),
        8: ("void", ["ptr:" + rid, "int"]),
        9: ("void", ["ptr:" + rid, "int"]),
        10: ("void", ["ptr:" + rid, "int"]),
        11: ("void", ["ptr:" + rid, "ptr:" + rid]),
        12: ("int", ["ptr:" + rid]),
        13: ("int", ["ptr:" + rid]),
        14: ("int", ["ptr:" + rid, "ptr:" + rid]),
        15: ("ptr:" + rid, ["ptr:" + rid]),
    }
    storage_functions = {f["loc"]["line"]: f for f in call_storage["functions"]}
    storage_by_name = {f["name"]: f for f in call_storage["functions"]}
    for line, (result, params) in storage_signatures.items():
        function = storage_functions[line]
        assert function["result"] == result, function
        assert [p["type"] for p in function["params"]] == params, function
    for function in call_storage["functions"]:
        assert function["result"] not in (rid, bid), function
        assert all(p["type"] not in (rid, bid) for p in function["params"]), function
        for node in function["body"]:
            if node.get("op") == "call":
                callee = storage_by_name[node["callee"]]
                assert [a["type"] for a in node["args"]] == [p["type"] for p in callee["params"]], node
                if callee["result"] == "void":
                    assert "target" not in node, node
            if node.get("op") == "return" and function["result"] == "void":
                assert "value" not in node, node

    def storage_pointer_object(function, pointer):
        if pointer["kind"] == "cast":
            return storage_pointer_object(function, pointer["args"][0])
        if pointer["kind"] == "address":
            return storage_place(function, pointer["args"][0])
        assert pointer["kind"] == "var", pointer
        values = [n["value"] for n in function["body"] if n.get("op") == "assign"
                  and n["target"].get("kind") == "var" and n["target"]["name"] == pointer["name"]]
        if values:
            assert len(values) == 1, values
            return storage_pointer_object(function, values[0])
        assert any(p["name"] == pointer["name"] for p in function["params"]), pointer
        return ("parameter", pointer["name"])

    def storage_place(function, place):
        if place["kind"] == "var":
            return ("object", place["name"])
        assert place["kind"] == "dereference", place
        return storage_pointer_object(function, place["args"][0])

    forwarded_function = storage_functions[9]
    forwarded_calls = [n for n in forwarded_function["body"] if n.get("op") == "call"]
    assert len(forwarded_calls) == 1 and forwarded_calls[0]["callee"] == storage_functions[8]["name"]
    assert storage_pointer_object(forwarded_function, forwarded_calls[0]["args"][0]) == (
        "parameter", forwarded_function["params"][0]["name"])
    assert not any(v["type"] == rid for v in forwarded_function["locals"]), forwarded_function
    for line in (4, 5, 8, 9):
        assert not any(n.get("op") == "assign" and n["value"]["type"] == rid
                       for n in storage_functions[line]["body"]), storage_functions[line]
    # Named-local and named-parameter returns retain Clang's selected source
    # copy/move. The hidden result object must not alias either source object.
    for line in (10, 11):
        function = storage_functions[line]
        copies = [n for n in function["body"] if n.get("op") == "assign" and n["value"]["type"] == rid]
        assert len(copies) == 1, function
        assert storage_place(function, copies[0]["target"]) == ("parameter", function["params"][0]["name"])
        assert storage_place(function, copies[0]["target"]) != storage_place(function, copies[0]["value"])
    main_storage = next(f for f in call_storage["functions"] if f["name"] == "main")
    record_locals = [v for v in main_storage["locals"] if v["type"] == rid]
    assert len(record_locals) == 8, record_locals
    assert sum(v["type"] == bid for v in main_storage["locals"]) == 1, main_storage
    copied_arguments = [n for n in main_storage["body"] if n.get("op") == "assign" and n["value"]["type"] == rid]
    assert len(copied_arguments) == 2, copied_arguments
    source_object = next(v for v in record_locals if v["loc"]["line"] == 17)
    assert all(storage_place(main_storage, n["value"]) == ("object", source_object["name"])
               for n in copied_arguments), copied_arguments
    twin_call = next(n for n in main_storage["body"] if n.get("op") == "call"
                     and n["callee"] == storage_functions[14]["name"])
    twin_objects = [storage_pointer_object(main_storage, a) for a in twin_call["args"]]
    assert len(set(twin_objects)) == 2 and ("object", source_object["name"]) not in twin_objects, twin_call
    assert set(twin_objects) == {storage_place(main_storage, n["target"]) for n in copied_arguments}
    for declaration_line, callee_line in ((17, 8), (18, 9), (19, 4)):
        local = next(v for v in record_locals if v["loc"]["line"] == declaration_line)
        invocation = next(n for n in main_storage["body"] if n.get("op") == "call"
                          and n["callee"] == storage_functions[callee_line]["name"])
        assert storage_pointer_object(main_storage, invocation["args"][0]) == ("object", local["name"])
    with tempfile.TemporaryDirectory(prefix="neverc-record-call-storage-relocated-") as temp:
        relocated = check("record-call-storage-relocated", call_storage_source,
                          root=Path(temp) / "project", profile="cpp-core-v2")
        assert relocated == call_storage, "call storage identities depend on the absolute root"
    old_record_source = "struct R{int n;};R identity(R value){return value;}int main(){R r={1};return identity(r).n-1;}"
    old_records = check("v1-record-value-signatures", old_record_source)
    old_record_id = old_records["records"][0]["id"]
    old_identity = next(f for f in old_records["functions"] if f["name"] != "main")
    assert old_identity["result"] == old_record_id, old_identity
    assert [p["type"] for p in old_identity["params"]] == [old_record_id], old_identity
    check("v2-record-parameter-const-write", "struct R{int n;};int f(const R r){r.n=1;return r.n;}",
          "TR0202", profile="cpp-core-v2")
    user_copy_source = """struct R {
  int value; R *self;
  R(int n):value(n),self(this){}
  R(const R&other):value(other.value+1),self(this){}
  R&operator=(const R&other){value=other.value+2;return *this;}
};
R make(int n){return R(n);}
R forward(int n){return make(n);}
int take(R value){return value.value;}
R&left(R&r,int&trace){trace=trace*10+1;return r;}
R&right(R&r,int&trace){trace=trace*10+2;return r;}
R fromSource(const R&r){return r;}
int main(){
  R source(1);
  R copy=source;
  copy=source;
  copy.operator=(source);
  int trace=0;
  left(copy,trace)=right(source,trace);
  left(copy,trace).operator=(right(source,trace));
  int value=take(source);
  R result=forward(7);
  R returned=fromSource(source);
  return value+result.value+returned.value;
}
"""
    user_copy = check("v2-user-copy-protocol", user_copy_source, profile="cpp-core-v2")
    rid = user_copy["records"][0]["id"]
    copy_functions = {f["loc"]["line"]: f for f in user_copy["functions"]}
    copy_by_name = {f["name"]: f for f in user_copy["functions"]}
    for line, result, params in ((4, "void", ["ptr:" + rid, "cptr:" + rid]),
                                 (5, "ptr:" + rid, ["ptr:" + rid, "cptr:" + rid]),
                                 (12, "void", ["ptr:" + rid, "cptr:" + rid])):
        function = copy_functions[line]
        assert function["result"] == result and [p["type"] for p in function["params"]] == params, function
    for function in user_copy["functions"]:
        for instruction in function["body"]:
            if instruction["op"] == "call":
                callee = copy_by_name[instruction["callee"]]
                assert [a["type"] for a in instruction["args"]] == [p["type"] for p in callee["params"]], instruction
    main_copy = copy_functions[13]
    calls = [n for n in main_copy["body"] if n["op"] == "call"]
    copy_name, assignment_name = copy_functions[4]["name"], copy_functions[5]["name"]
    assert sum(c["callee"] == copy_name for c in calls) == 2, calls
    assert sum(c["callee"] == assignment_name for c in calls) == 4, calls
    # Lvalue construction and by-value argument preparation call user copying,
    # while direct prvalue result forwarding introduces no copy call.
    assert all(n["target"]["type"] != rid for n in main_copy["body"] if n["op"] == "assign"), main_copy
    for line in (7, 8):
        assert all(n.get("callee") != copy_name for n in copy_functions[line]["body"]), copy_functions[line]
    returned_copy = [n for n in copy_functions[12]["body"] if n.get("callee") == copy_name]
    assert len(returned_copy) == 1, returned_copy
    assert storage_pointer_object(copy_functions[12], returned_copy[0]["args"][0]) == (
        "parameter", copy_functions[12]["params"][0]["name"])
    assert storage_pointer_object(copy_functions[12], returned_copy[0]["args"][1]) == (
        "parameter", copy_functions[12]["params"][1]["name"])
    objects = {v["loc"]["line"]: v for v in main_copy["locals"] if v["type"] == rid}
    for line in (15, 16, 17):
        invocation = next(c for c in calls if c["loc"]["line"] == line)
        assert storage_pointer_object(main_copy, invocation["args"][0]) == ("object", objects[15]["name"])
        assert storage_pointer_object(main_copy, invocation["args"][1]) == ("object", objects[14]["name"])
    argument_copy = next(c for c in calls if c["callee"] == copy_name and c["loc"]["line"] == 21)
    parameter_call = next(c for c in calls if c["callee"] == copy_functions[9]["name"])
    assert storage_pointer_object(main_copy, argument_copy["args"][0]) == storage_pointer_object(
        main_copy, parameter_call["args"][0])
    assert storage_pointer_object(main_copy, argument_copy["args"][0]) != ("object", objects[14]["name"])
    assert [c["callee"] for c in calls if c["loc"]["line"] == 19] == [
        copy_functions[11]["name"], copy_functions[10]["name"], assignment_name]
    assert [c["callee"] for c in calls if c["loc"]["line"] == 20] == [
        copy_functions[10]["name"], copy_functions[11]["name"], assignment_name]
    assignment_return = next(n["value"] for n in copy_functions[5]["body"] if n["op"] == "return")
    assert assignment_return["type"] == "ptr:" + rid and assignment_return["kind"] == "var", assignment_return
    with tempfile.TemporaryDirectory(prefix="neverc-user-copy-relocated-") as temp:
        relocated = check("user-copy-relocated", user_copy_source,
                          root=Path(temp) / "project", profile="cpp-core-v2")
        assert relocated == user_copy, "user copy identities depend on the absolute root"

    user_copy_rejected = {
        'deleted-constructor': 'struct R{int n;R(const R&)=delete;};',
        'deleted-assignment': 'struct R{int n;R&operator=(const R&)=delete;};',
        'volatile-constructor': 'struct R{int n;R(const volatile R&r):n(r.n){}};',
        'volatile-assignment': 'struct R{int n;R&operator=(const volatile R&r){n=r.n;return *this;}};',
        'default-argument': 'struct R{int n;R(const R&r,int extra=0):n(r.n+extra){}};',
    }
    for name, source in user_copy_rejected.items():
        check("v2-user-copy-reject-" + name, source, "TR0201", profile="cpp-core-v2")
    check("v2-copy-definition", "struct R{int n;R(const R&);};", "TR0203", profile="cpp-core-v2")
    check("v2-assignment-definition", "struct R{int n;R&operator=(const R&);};", "TR0203", profile="cpp-core-v2")
    check("v1-user-copy-still-rejected", "struct R{int n;R(const R&r):n(r.n){}};", "TR0201")
    check("v2-user-copy-const-write", "struct R{int n;R&operator=(const R&r){n=r.n;return *this;}};void f(){const R r{1};R s{2};r=s;}",
          "TR0202", profile="cpp-core-v2")
    conversion_cleanup_source = """struct R {
  int *value; R *self;
  explicit R(int *p):value(p),self(this){++*value;}
  ~R(){--*value;}
};
R make(int *p){return static_cast<R>(p);}
void consume(R value){}
void f(int *p){
  R direct=static_cast<R>(p);
  R cstyle=(R)p;
  static_cast<R>(p);
  (R)p;
  consume(static_cast<R>(p));
  R result=make(p);
}
"""
    converted = check("v2-constructor-conversion-cleanup", conversion_cleanup_source,
                      profile="cpp-core-v2")
    converted_record = converted["records"][0]["id"]
    converted_functions = {f["name"]: f for f in converted["functions"]}
    converted_by_line = {f["loc"]["line"]: f for f in converted["functions"]}
    converted_ctor = converted_by_line[3]
    converted_dtor = converted_functions[converted_record + "_destroy"]
    assert converted_ctor["result"] == converted_dtor["result"] == "void"
    assert [p["type"] for p in converted_ctor["params"]] == ["ptr:" + converted_record, "ptr:int"]
    assert [p["type"] for p in converted_dtor["params"]] == ["ptr:" + converted_record]
    converted_body = converted_by_line[8]
    converted_calls = [n for n in converted_body["body"] if n["op"] == "call"]
    constructions = [n for n in converted_calls if n["callee"] == converted_ctor["name"]]
    cleanups = [n for n in converted_calls if n["callee"] == converted_dtor["name"]]
    assert len(constructions) == 5 and len(cleanups) == 5, converted_calls
    converted_locals = [v for v in converted_body["locals"] if v["type"] == converted_record]
    assert len(converted_locals) == 6, converted_locals
    for line in (9, 10):
        local = next(v for v in converted_locals if v["loc"]["line"] == line)
        construction = next(n for n in constructions if n["loc"]["line"] == line)
        assert storage_pointer_object(converted_body, construction["args"][0]) == ("object", local["name"])
    parameter_call = next(n for n in converted_calls if n["callee"] == converted_by_line[7]["name"])
    parameter_object = storage_pointer_object(converted_body, parameter_call["args"][0])
    cleanup_objects = [storage_pointer_object(converted_body, n["args"][0]) for n in cleanups]
    assert len(set(cleanup_objects)) == 5 and parameter_object not in cleanup_objects, cleanups
    assert set(cleanup_objects) | {parameter_object} == {("object", v["name"]) for v in converted_locals}
    made_calls = [n for n in converted_by_line[6]["body"] if n["op"] == "call"]
    assert len(made_calls) == 1 and made_calls[0]["callee"] == converted_ctor["name"], made_calls
    assert storage_pointer_object(converted_by_line[6], made_calls[0]["args"][0]) == (
        "parameter", converted_by_line[6]["params"][0]["name"])
    for function in converted["functions"]:
        for node in function["body"]:
            if node["op"] == "call":
                callee = converted_functions[node["callee"]]
                assert [a["type"] for a in node["args"]] == [p["type"] for p in callee["params"]], node
            if node["op"] == "assign":
                assert node["value"]["type"] != converted_record, "conversion wrapper inserted a record copy"
    with tempfile.TemporaryDirectory(prefix="neverc-conversion-cleanup-relocated-") as temp:
        relocated = check("conversion-cleanup-relocated", conversion_cleanup_source,
                          root=Path(temp) / "project", profile="cpp-core-v2")
        assert relocated == converted, "conversion cleanup identities depend on the absolute root"
    generated_copy_source = """struct Leaf { int n;Leaf*self;
 Leaf(int v):n(v),self(this){}
 Leaf(const Leaf&s):n(s.n+1),self(this){}
 ~Leaf(){}
};
struct Inner { Leaf items[2];~Inner()=default; };
struct Box { int scalar[2];Inner inner;Leaf grid[2][2];Box(const Box&)=default;~Box()=default; };
struct Outside { Leaf leaf;Outside(const Outside&);~Outside()=default; };
Outside::Outside(const Outside&)=default;
struct Trivial { int n;Trivial*self;Trivial(const Trivial&)=default; };
struct ArrayTrivial { Trivial items[2];Leaf leaf;~ArrayTrivial()=default; };
struct MutableLeaf { int n;MutableLeaf(MutableLeaf&s):n(++s.n){}~MutableLeaf(){} };
struct MutableBox { MutableLeaf items[2];~MutableBox()=default; };
struct Lazy { Leaf leaf;Lazy(const Lazy&)=default; };
Box copied(const Box&s){return s;}
void twice(const Box&s){Box a=s;Box b=s;}
Outside outside(const Outside&s){return s;}
Trivial trivial(const Trivial&s){return s;}
ArrayTrivial array(const ArrayTrivial&s){return s;}
MutableBox mutableCopy(MutableBox&s){return s;}
int query(const Lazy&s){return sizeof(Lazy(s));}
"""
    generated_copy = check("v2-generated-copy-protocol", generated_copy_source, profile="cpp-core-v2")
    gc_records = {r["loc"]["line"]: r for r in generated_copy["records"]}
    gc_functions = {f["name"]: f for f in generated_copy["functions"]}
    gc_by_line = {f["loc"]["line"]: f for f in generated_copy["functions"]}
    gc_constructors = {}
    for line in (1, 6, 7, 8, 10, 11, 12, 13, 14):
        rid = gc_records[line]["id"]
        source_kind = "ptr:" if line in (12, 13) else "cptr:"
        locations = (4,) if line == 1 else ((8, 9) if line == 8 else (line,))
        # A free function returning this record can have the same hidden
        # result/source signature. Select the constructor's source identity.
        selected = [f for f in generated_copy["functions"] if f["result"] == "void"
                    and f["loc"]["line"] in locations
                    and [p["type"] for p in f["params"]] == ["ptr:" + rid, source_kind + rid]]
        assert len(selected) == (0 if line in (10, 14) else 1), (line, selected)
        if selected:
            gc_constructors[line] = selected[0]

    def gc_calls(function):
        return [n for n in function["body"] if n["op"] == "call"]

    def gc_identity(function, expr):
        kind = expr["kind"]
        if kind in ("cast", "address", "dereference", "array_decay"):
            return gc_identity(function, expr["args"][0])
        if kind == "literal":
            # Integer carriers encode their values as decimal strings.
            return int(expr["value"])
        if kind == "member":
            return ("member", gc_identity(function, expr["args"][0]), expr["name"])
        if kind == "index":
            return ("index", gc_identity(function, expr["args"][0]),
                    gc_identity(function, expr["args"][1]))
        assert kind == "var", expr
        if any(p["name"] == expr["name"] for p in function["params"]):
            return ("parameter", expr["name"])
        values = [n["value"] for n in function["body"] if n["op"] == "assign"
                  and n["target"].get("kind") == "var" and n["target"]["name"] == expr["name"]]
        assert len(values) == 1, (expr, values)
        return gc_identity(function, values[0])

    for line, callees in {6: [1, 1], 7: [6, 1, 1, 1, 1], 8: [1],
                          11: [1], 13: [12, 12]}.items():
        function = gc_constructors[line]
        assert [n["callee"] for n in gc_calls(function)] == [gc_constructors[c]["name"] for c in callees]
        assert not any(n["op"] == "assign" and n["target"]["type"] == gc_records[line]["id"]
                       for n in function["body"]), "generated nontrivial copy became a whole-record store"
    for line in (6, 13):
        function = gc_constructors[line]
        field = gc_records[line]["fields"][0]["name"]
        for index_value, call in enumerate(gc_calls(function)):
            for arg, parameter in zip(call["args"], function["params"]):
                assert gc_identity(function, arg) == (
                    "index", ("member", ("parameter", parameter["name"]), field), index_value)
    box_copy = gc_constructors[7]
    box_fields = gc_records[7]["fields"]
    for call, (row, col) in zip(gc_calls(box_copy)[1:], ((0, 0), (0, 1), (1, 0), (1, 1))):
        for arg, parameter in zip(call["args"], box_copy["params"]):
            assert gc_identity(box_copy, arg) == (
                "index", ("index", ("member", ("parameter", parameter["name"]),
                                    box_fields[2]["name"]), row), col)
    scalar_stores = [n for n in box_copy["body"] if n["op"] == "assign"
                     and n["target"]["kind"] == "index" and n["target"]["type"] == "int"]
    assert len(scalar_stores) == 2, scalar_stores
    for index_value, node in enumerate(scalar_stores):
        for expr, parameter in zip((node["target"], node["value"]), box_copy["params"]):
            assert gc_identity(box_copy, expr) == (
                "index", ("member", ("parameter", parameter["name"]), box_fields[0]["name"]), index_value)
    # Each semantic common array source gets exactly one pointer capture: the
    # scalar array, outer grid, and each of its two inner rows.
    source_arrays = [n for n in box_copy["body"] if n["op"] == "assign"
                     and n["value"]["kind"] == "address"
                     and n["value"]["args"][0]["type"].startswith("arr:")]
    assert len(source_arrays) == 4, source_arrays
    trivial_id = gc_records[10]["id"]
    array_copy = gc_constructors[11]
    trivial_stores = [n for n in array_copy["body"] if n["op"] == "assign"
                      and n["target"]["type"] == trivial_id]
    assert len(trivial_stores) == 2, trivial_stores
    for index_value, node in enumerate(trivial_stores):
        for expr, parameter in zip((node["target"], node["value"]), array_copy["params"]):
            assert gc_identity(array_copy, expr) == (
                "index", ("member", ("parameter", parameter["name"]),
                          gc_records[11]["fields"][0]["name"]), index_value)
    for line, constructor_line in ((15, 7), (17, 8), (19, 11), (20, 13)):
        function = gc_by_line[line]
        calls = gc_calls(function)
        assert len(calls) == 1 and calls[0]["callee"] == gc_constructors[constructor_line]["name"]
        for arg, parameter in zip(calls[0]["args"], function["params"]):
            assert gc_identity(function, arg) == ("parameter", parameter["name"])
    assert sum(c["callee"] == gc_constructors[7]["name"] for c in gc_calls(gc_by_line[16])) == 2
    assert not gc_calls(gc_by_line[18]) and not gc_calls(gc_by_line[21])
    for function in generated_copy["functions"]:
        for node in gc_calls(function):
            callee = gc_functions[node["callee"]]
            assert [a["type"] for a in node["args"]] == [p["type"] for p in callee["params"]], node
    with tempfile.TemporaryDirectory(prefix="neverc-generated-copy-relocated-") as temp:
        relocated = check("generated-copy-relocated", generated_copy_source,
                          root=Path(temp) / "project", profile="cpp-core-v2")
        assert relocated == generated_copy, "generated copying depends on the absolute root"
    generated_copy_rejected = {
        'deleted-copy': 'struct R{int n;R(const R&)=delete;};',
        'defaulted-deleted-copy': 'struct I{int n;I(const I&)=delete;};struct R{I i;R(const R&)=default;};',
        'reference-field': 'struct R{int &n;R(const R&)=default;};',
        'const-field': 'struct R{const int n;R(const R&)=default;};',
        'nonpublic-field': 'class R{int n;public:R(const R&)=default;};',
        'base-copy': 'struct B{int n;};struct R:B{int m;R(const R&)=default;};',
        'lambda-array-copy': 'int f(){int values[2]={1,2};auto capture=[values](){return values[0];};return capture();}',
        'decomposed-array-copy': 'int f(){int values[2]={1,2};auto [a,b]=values;return a+b;}',
        'copy-expansion': 'struct I{int n;I(const I&s):n(s.n){}};struct R{I items[65536];~R()=default;};R f(const R&s){return s;}',
    }
    check("v2-generated-copy-invalid-volatile", "struct R{int n;R(const volatile R&)=default;};",
          "TR0202", profile="cpp-core-v2")
    for name, source in generated_copy_rejected.items():
        check("v2-generated-copy-reject-" + name, source, "TR0201", profile="cpp-core-v2")
    check("v2-generated-copy-missing", "struct I{int n;I(const I&);};struct R{I i;R(const R&)=default;};R f(const R&s){return s;}",
          "TR0203", profile="cpp-core-v2")
    check("v1-defaulted-copy", "struct R{int n;R(const R&)=default;};", "TR0201")
    generated_assignment_source = """struct Leaf { int n;Leaf*alias;
 Leaf&operator=(const Leaf&s){n=s.n+1;return *alias;}
};
struct Inner { Leaf items[2]; };
struct Plain { int n;Plain*self;Plain(const Plain&s):n(s.n),self(this){}~Plain(){} };
struct Box { int scalar[2][2];Inner inner;Leaf grid[2][2];Plain plain[2];Box&operator=(const Box&)=default; };
struct Outside { int scalar[2];Leaf leaf;Outside&operator=(const Outside&); };
Outside&Outside::operator=(const Outside&)=default;
struct Trivial { int n[2];Trivial*self;Trivial&operator=(const Trivial&)=default; };
struct MutableLeaf { int n;MutableLeaf&operator=(MutableLeaf&s){n=++s.n;return *this;} };
struct MutableBox { MutableLeaf items[2]; };
struct Lazy { Leaf leaf;Lazy&operator=(const Lazy&)=default; };
Box&assign(Box&a,const Box&b){return a=b;}
void twice(Box&a,const Box&b){a=b;a.operator=(b);}
Outside&outside(Outside&a,const Outside&b){return a=b;}
Trivial&trivial(Trivial&a,const Trivial&b){return a=b;}
Trivial&member(Trivial&a,const Trivial&b){return a.operator=(b);}
Trivial&arrow(Trivial*a,const Trivial&b){return a->operator=(b);}
MutableBox&mutableCopy(MutableBox&a,MutableBox&b){return a=b;}
int query(Lazy&a,const Lazy&b){return sizeof(a=b);}
Box&left(Box&a){return a;}
const Box&right(const Box&b){return b;}
void ordered(Box&a,const Box&b){left(a)=right(b);}
void memberOrdered(Box&a,const Box&b){left(a).operator=(right(b));}
"""
    generated_assignment = check("v2-generated-assignment-protocol", generated_assignment_source, profile="cpp-core-v2")
    ga_records = {r["loc"]["line"]: r for r in generated_assignment["records"]}
    ga_functions = {f["name"]: f for f in generated_assignment["functions"]}
    ga_by_line = {f["loc"]["line"]: f for f in generated_assignment["functions"]}
    ga_assignments = {}
    for line in (1, 4, 5, 6, 7, 9, 10, 11, 12):
        rid = ga_records[line]["id"]
        source_kind = "ptr:" if line in (10, 11) else "cptr:"
        locations = (2,) if line == 1 else ((7, 8) if line == 7 else (line,))
        selected = [f for f in generated_assignment["functions"] if f["result"] == "ptr:" + rid
                    and f["loc"]["line"] in locations
                    and [p["type"] for p in f["params"]] == ["ptr:" + rid, source_kind + rid]]
        assert len(selected) == (0 if line in (5, 9, 12) else 1), (line, selected)
        if selected:
            ga_assignments[line] = selected[0]
    for line, callees in {4: [1], 6: [4, 1], 7: [1], 11: [10]}.items():
        function = ga_assignments[line]
        calls = gc_calls(function)
        assert [n["callee"] for n in calls] == [ga_assignments[c]["name"] for c in callees], calls
        # Nontrivial array assignments stay counted loops: one body call per
        # semantic loop, executed for every element, without whole-record stores.
        if line in (4, 6, 11):
            assert any(n["op"] == "branch" for n in function["body"]), function
        assert not any(n["op"] == "assign" and n["target"]["type"] == ga_records[line]["id"]
                       for n in function["body"]), function
        returned = next(n["value"] for n in function["body"] if n["op"] == "return")
        assert gc_identity(function, returned) == ("parameter", function["params"][0]["name"])
    box_assignment = ga_assignments[6]
    fields = ga_records[6]["fields"]
    # Optimized generated copies of scalar and trivially assigned class arrays
    # become typed stores, even if the class has a nontrivial ctor/destructor.
    scalar_stores = [n for n in box_assignment["body"] if n["op"] == "assign"
                     and n["target"]["kind"] == "index" and n["target"]["type"] == "int"]
    assert len(scalar_stores) == 4, scalar_stores
    for node, (row, col) in zip(scalar_stores, ((0, 0), (0, 1), (1, 0), (1, 1))):
        for expr, param in zip((node["target"], node["value"]), box_assignment["params"]):
            assert gc_identity(box_assignment, expr) == (
                "index", ("index", ("member", ("parameter", param["name"]), fields[0]["name"]), row), col)
    plain_stores = [n for n in box_assignment["body"] if n["op"] == "assign"
                    and n["target"]["type"] == ga_records[5]["id"]]
    assert len(plain_stores) == 2, plain_stores
    for index_value, node in enumerate(plain_stores):
        for expr, param in zip((node["target"], node["value"]), box_assignment["params"]):
            assert gc_identity(box_assignment, expr) == (
                "index", ("member", ("parameter", param["name"]), fields[3]["name"]), index_value)
    array_captures = [n for n in box_assignment["body"] if n["op"] == "assign"
                      and n["value"]["kind"] == "address"
                      and n["value"]["args"][0]["type"].startswith("arr:")]
    assert len(array_captures) == 4, array_captures
    for line in (16, 17, 18):
        function = ga_by_line[line]
        assert not gc_calls(function), function
        stores = [n for n in function["body"] if n["op"] == "assign"
                  and n["target"]["type"] == ga_records[9]["id"]]
        assert len(stores) == 1, stores
        assert gc_identity(function, stores[0]["target"]) == ("parameter", function["params"][0]["name"])
        assert gc_identity(function, stores[0]["value"]) == ("parameter", function["params"][1]["name"])
        returned = next(n["value"] for n in function["body"] if n["op"] == "return")
        assert gc_identity(function, returned) == ("parameter", function["params"][0]["name"])
    for line, callee in ((13, 6), (15, 7), (19, 11)):
        calls = gc_calls(ga_by_line[line])
        assert len(calls) == 1 and calls[0]["callee"] == ga_assignments[callee]["name"], calls
    assert [n["callee"] for n in gc_calls(ga_by_line[14])] == [ga_assignments[6]["name"]] * 2
    assert not gc_calls(ga_by_line[20])
    assert [n["callee"] for n in gc_calls(ga_by_line[23])] == [
        ga_by_line[22]["name"], ga_by_line[21]["name"], ga_assignments[6]["name"]]
    assert [n["callee"] for n in gc_calls(ga_by_line[24])] == [
        ga_by_line[21]["name"], ga_by_line[22]["name"], ga_assignments[6]["name"]]
    for function in generated_assignment["functions"]:
        assert not any(n["op"] == "mapped_call" for n in function["body"]), function
        for node in gc_calls(function):
            assert node["callee"] in ga_functions, node
            callee = ga_functions[node["callee"]]
            assert [a["type"] for a in node["args"]] == [p["type"] for p in callee["params"]], node
    assert "__builtin_memcpy" not in json.dumps(generated_assignment)
    with tempfile.TemporaryDirectory(prefix="neverc-generated-assignment-relocated-") as temp:
        relocated = check("generated-assignment-relocated", generated_assignment_source,
                          root=Path(temp) / "project", profile="cpp-core-v2")
        assert relocated == generated_assignment, "generated assignment depends on the absolute root"
    generated_assignment_rejected = {
        'deleted': 'struct R{int n;R&operator=(const R&)=delete;};',
        'defaulted-deleted': 'struct I{int n;I&operator=(const I&)=delete;};struct R{I i;R&operator=(const R&)=default;};',
        'rvalue-receiver': 'struct R{int n;R&operator=(const R&)&&=default;};',
        'const-field': 'struct R{const int n;R&operator=(const R&)=default;};',
        'reference-field': 'struct R{int&n;R&operator=(const R&)=default;};',
        'private-field': 'class R{int n;public:R&operator=(const R&)=default;};',
        'base-field': 'struct B{int n;};struct R:B{int m;R&operator=(const R&)=default;};',
        'raw-builtin': 'void f(int*a,int*b){__builtin_memcpy(a,b,4);}',
        'dead-builtin': 'void f(int*a,int*b){if(false)__builtin_memcpy(a,b,4);}',
        'user-member-builtin': 'struct R{int n[2];R&operator=(const R&s){__builtin_memcpy(n,s.n,sizeof(n));return *this;}};',
        'array-expansion': 'struct I{int n;I&operator=(const I&s){n=s.n;return *this;}};struct R{int n[65536];I i;R&operator=(const R&)=default;};void f(R&a,const R&b){a=b;}',
    }
    generated_assignment_invalid = {
        'volatile-source': 'struct R{int n;R&operator=(const volatile R&)=default;};',
        'volatile-receiver': 'struct R{int n;R&operator=(const R&)volatile=default;};',
        'const-receiver': 'struct R{int n;R&operator=(const R&)const=default;};',
        'value-parameter': 'struct R{int n;R&operator=(R)=default;};',
        'value-result': 'struct R{int n;R operator=(const R&)=default;};',
        'const-result': 'struct R{int n;const R&operator=(const R&)=default;};',
    }
    for name, source in generated_assignment_invalid.items():
        check("v2-generated-assignment-invalid-" + name, source, "TR0202", profile="cpp-core-v2")
    for name, source in generated_assignment_rejected.items():
        check("v2-generated-assignment-reject-" + name, source, "TR0201", profile="cpp-core-v2")
    check("v2-generated-assignment-missing", "struct I{int n;I&operator=(const I&);};struct R{I i;R&operator=(const R&)=default;};void f(R&a,const R&b){a=b;}",
          "TR0203", profile="cpp-core-v2")
    check("v1-defaulted-assignment", "struct R{int n;R&operator=(const R&)=default;};", "TR0201")
    default_member_source = """struct Outer;
struct Inner { Outer*outer;Inner*self=this; };
struct Outer { int n=3;Inner inner={this};Outer*self=this; };
struct Caller;
struct Target { Caller*caller;Target*self=this; };
struct Caller { int n;Target make(){return {this};} };
Outer make(){return {};}
struct Defaults { int n=7;Defaults*self=this;Defaults()=default; };
Defaults construct(){return Defaults();}
struct Lazy { int n=9;Lazy*self=this;Lazy()=default; };
int query(){return sizeof(Lazy{});}
"""
    defaults = check("v2-default-member-destinations", default_member_source, profile="cpp-core-v2")
    dm_records = {r["loc"]["line"]: r for r in defaults["records"]}
    dm_functions = {f["name"]: f for f in defaults["functions"]}
    dm_by_line = {f["loc"]["line"]: f for f in defaults["functions"]}

    def dm_stores(function):
        return [n for n in function["body"] if n["op"] == "assign" and n["target"]["kind"] == "member"]

    def dm_member(base, field):
        return ("member", base, field["name"])

    # Nested explicit outer-this and selected inner-this must be different
    # pointers; the subsequent outer default must restore the outer receiver.
    function = dm_by_line[7]
    outer = ("parameter", function["params"][0]["name"])
    inner = dm_member(outer, dm_records[3]["fields"][1])
    stores = dm_stores(function)
    assert len(stores) == 4 and not gc_calls(function), function
    expected = [(dm_member(outer, dm_records[3]["fields"][0]), 3),
                (dm_member(inner, dm_records[2]["fields"][0]), outer),
                (dm_member(inner, dm_records[2]["fields"][1]), inner),
                (dm_member(outer, dm_records[3]["fields"][2]), outer)]
    assert [(gc_identity(function, n["target"]), gc_identity(function, n["value"])) for n in stores] == expected
    assert not any(v["type"] in (dm_records[2]["id"], dm_records[3]["id"])
                   for v in function["locals"]), function
    function = dm_by_line[6]
    assert [p["type"] for p in function["params"]] == ["ptr:" + dm_records[5]["id"], "ptr:" + dm_records[6]["id"]]
    target, caller = [("parameter", p["name"]) for p in function["params"]]
    stores = dm_stores(function)
    assert [(gc_identity(function, n["target"]), gc_identity(function, n["value"])) for n in stores] == [
        (dm_member(target, dm_records[5]["fields"][0]), caller),
        (dm_member(target, dm_records[5]["fields"][1]), target)]
    constructor = next(f for f in defaults["functions"] if f["result"] == "void"
                       and f["loc"]["line"] == 8
                       and [p["type"] for p in f["params"]] == ["ptr:" + dm_records[8]["id"]])
    receiver = ("parameter", constructor["params"][0]["name"])
    assert [(gc_identity(constructor, n["target"]), gc_identity(constructor, n["value"]))
            for n in dm_stores(constructor)] == [
        (dm_member(receiver, dm_records[8]["fields"][0]), 7),
        (dm_member(receiver, dm_records[8]["fields"][1]), receiver)]
    function = dm_by_line[9]
    calls = gc_calls(function)
    assert len(calls) == 1 and calls[0]["callee"] == constructor["name"], calls
    assert gc_identity(function, calls[0]["args"][0]) == ("parameter", function["params"][0]["name"])
    assert not gc_calls(dm_by_line[11]), dm_by_line[11]
    assert not any(f["params"] and f["params"][0]["type"] == "ptr:" + dm_records[10]["id"]
                   for f in defaults["functions"]), "unevaluated default construction invented a body"
    for function in defaults["functions"]:
        for node in gc_calls(function):
            callee = dm_functions[node["callee"]]
            assert [a["type"] for a in node["args"]] == [p["type"] for p in callee["params"]]
    with tempfile.TemporaryDirectory(prefix="neverc-default-member-relocated-") as temp:
        relocated = check("default-member-relocated", default_member_source,
                          root=Path(temp) / "project", profile="cpp-core-v2")
        assert relocated == defaults, "default initializer identities depend on the absolute root"

    default_cleanup_source = """struct Log { int n; };
int mark(Log&l,int n){l.n=l.n*10+n;return n;}
struct Temp { Log*log;Temp(Log&l):log(&l){}~Temp(){mark(*log,3);} };
struct Aggregate { Log*log;int first=(Temp(*log),mark(*log,1));int second=mark(*log,2); };
struct Constructor { Log*log;int first=(Temp(*log),mark(*log,1));int second=mark(*log,2);Constructor(Log&l):log(&l){} };
struct Copy { Log*log;int n=mark(*log,4);Copy(Log&l):log(&l){}Copy(const Copy&s):log(s.log){} };
void aggregate(Log&l){Aggregate a{&l};}
void skipped(Log&l){Aggregate a{&l,7,8};}
Aggregate copied(const Aggregate&s){return s;}
void assigned(Aggregate&a,const Aggregate&s){a=s;}
"""
    cleanup_defaults = check("v2-default-member-cleanup", default_cleanup_source, profile="cpp-core-v2")
    dc_records = {r["loc"]["line"]: r for r in cleanup_defaults["records"]}
    dc_functions = {f["name"]: f for f in cleanup_defaults["functions"]}
    dc_by_line = {f["loc"]["line"]: f for f in cleanup_defaults["functions"]}
    mark_name = dc_by_line[2]["name"]
    destructor_name = dc_records[3]["id"] + "_destroy"
    temp_ctor = next(f["name"] for f in cleanup_defaults["functions"] if f["result"] == "void"
                     and f["loc"]["line"] == 3
                     and [p["type"] for p in f["params"]] == ["ptr:" + dc_records[3]["id"], "ptr:" + dc_records[1]["id"]])
    constructor = next(f for f in cleanup_defaults["functions"] if f["result"] == "void"
                       and f["loc"]["line"] == 5
                       and [p["type"] for p in f["params"]] == ["ptr:" + dc_records[5]["id"], "ptr:" + dc_records[1]["id"]])
    for function, expected in ((dc_by_line[7], [temp_ctor, mark_name, mark_name, destructor_name]),
                               (constructor, [temp_ctor, mark_name, destructor_name, mark_name])):
        calls = gc_calls(function)
        assert [n["callee"] for n in calls] == expected, calls
        construction = next(n for n in calls if n["callee"] == temp_ctor)
        cleanup = next(n for n in calls if n["callee"] == destructor_name)
        assert storage_pointer_object(function, construction["args"][0]) == storage_pointer_object(function, cleanup["args"][0])
        assert sum(v["type"] == dc_records[3]["id"] for v in function["locals"]) == 1
    for line in (8, 9, 10):
        assert not gc_calls(dc_by_line[line]), "overrides and generated copies/assignments reran defaults"
    user_copy = next(f for f in cleanup_defaults["functions"] if f["result"] == "void"
                     and f["loc"]["line"] == 6
                     and [p["type"] for p in f["params"]] == ["ptr:" + dc_records[6]["id"], "cptr:" + dc_records[6]["id"]])
    assert [n["callee"] for n in gc_calls(user_copy)] == [mark_name], user_copy
    for function in cleanup_defaults["functions"]:
        for node in gc_calls(function):
            callee = dc_functions[node["callee"]]
            assert [a["type"] for a in node["args"]] == [p["type"] for p in callee["params"]]

    default_member_rejected = {
        'const-field': 'struct R{const int n=1;};',
        'reference-field': 'struct R{int value;int&ref=value;};',
        'mutable-field': 'struct R{mutable int n=1;};',
        'private-field': 'class R{int n=1;};',
        'bitfield': 'struct R{int bits:2;int n=1;};',
        'static-member': 'struct R{static int x;int n=1;};int R::x=0;',
        'base': 'struct B{int n=1;};struct R:B{int next=2;};',
        'attribute': 'struct R{[[maybe_unused]] int n=1;};',
        'floating-default': 'struct R{int n=static_cast<int>(1.5);};',
        'lambda-unused': 'struct R{int n=[](){return 1;}();};',
        'lambda-overridden': 'struct R{int n=[](){return 1;}();};void f(){R r{7};}',
        'throw-unused': 'struct R{int n=(throw 1,2);};',
        'throw-overridden': 'struct R{int n=(throw 1,2);R():n(7){}};',
        'new-default': 'struct R{int*p=new int(1);};',
        'reinterpret-default': 'struct R{int*p=reinterpret_cast<int*>(1);};',
        'template-default': 'template<class T>struct R{T n=1;};',
        'excessive-array': 'struct R{int n[65537]={1};};',
        'address-of-member': 'struct R{int n=1;int R::*p=&R::n;};',
    }
    for name, source in default_member_rejected.items():
        check("v2-default-member-reject-" + name, source, "TR0201", profile="cpp-core-v2")
    check("v2-default-member-missing", "int missing();struct R{int n=missing();};void f(){R r{7};}",
          "TR0203", profile="cpp-core-v2")
    check("v1-default-member", "struct R{int n=1;};", "TR0201")
    rvalue_source = """struct R { int n;R*self;int set(int value)&&{n=value;return n;}
 int tag()&{return 1;}
 int tag()&&{return 2;}
 R&&again()&&{return static_cast<R&&>(*this);}
};
int pick(int&){return 1;}
int pick(int&&){return 2;}
R&&identity(R&&r){return static_cast<R&&>(r);}
int&&field(R&&r){return static_cast<R&&>(r).n;}
using Row=int[2];
Row&&row(Row&&r){return static_cast<Row&&>(r);}
int*&&pointer(int*&&p){return static_cast<int*&&>(p);}
R&&choose(bool b,R&a,R&c){return b?static_cast<R&&>(a):static_cast<R&&>(c);}
int select(int&n){return pick(n)+pick(static_cast<int&&>(n));}
int method(R&r){return r.tag()+static_cast<R&&>(r).tag();}
R&&forward(R&&r){return identity(static_cast<R&&>(r));}
R&&receiver(R&r,int&trace){trace=trace*10+1;return static_cast<R&&>(r);}
int argument(int&trace){trace=trace*10+2;return trace;}
int consume(R&&r,int n){return r.n+n;}
int ordered(R&r,int&trace){return receiver(r,trace).set(argument(trace));}
"""
    rvalues = check("v2-live-rvalue-identities", rvalue_source, profile="cpp-core-v2")
    rv_record = rvalues["records"][0]
    rv_id = rv_record["id"]
    rv_functions = {f["name"]: f for f in rvalues["functions"]}
    rv_lines = {f["loc"]["line"]: f for f in rvalues["functions"]}
    signatures = {
        2: ("int", ["ptr:" + rv_id]), 3: ("int", ["ptr:" + rv_id]),
        4: ("ptr:" + rv_id, ["ptr:" + rv_id]),
        6: ("int", ["ptr:int"]), 7: ("int", ["ptr:int"]),
        8: ("ptr:" + rv_id, ["ptr:" + rv_id]),
        9: ("ptr:int", ["ptr:" + rv_id]),
        11: ("ptr:arr:2:int", ["ptr:arr:2:int"]),
        12: ("ptr:ptr:int", ["ptr:ptr:int"]),
        13: ("ptr:" + rv_id, ["bool", "ptr:" + rv_id, "ptr:" + rv_id]),
        16: ("ptr:" + rv_id, ["ptr:" + rv_id]),
    }
    for line, (result, params) in signatures.items():
        function = rv_lines[line]
        assert function["result"] == result and [p["type"] for p in function["params"]] == params, function
    assert rv_lines[2]["name"] != rv_lines[3]["name"] and rv_lines[6]["name"] != rv_lines[7]["name"]
    for line in (4, 8, 9, 11, 12):
        function = rv_lines[line]
        returned = next(n["value"] for n in function["body"] if n["op"] == "return")
        expected = ("parameter", function["params"][0]["name"])
        if line == 9:
            expected = ("member", expected, rv_record["fields"][0]["name"])
        assert gc_identity(function, returned) == expected, (line, function)
        assert not gc_calls(function), function
    for line, selected in ((14, (6, 7)), (15, (2, 3))):
        function = rv_lines[line]
        calls = gc_calls(function)
        assert [n["callee"] for n in calls] == [rv_lines[i]["name"] for i in selected], calls
        assert all(gc_identity(function, n["args"][0]) == ("parameter", function["params"][0]["name"])
                   for n in calls), calls
    function = rv_lines[13]
    assert sum(n["op"] == "branch" for n in function["body"]) == 1, function
    pointer_stores = [n for n in function["body"] if n["op"] == "assign"
                      and n["target"]["type"] == "ptr:" + rv_id]
    # Both arms store addresses into one join slot. The return may copy that
    # pointer, but neither arm may copy the record itself.
    arms = [n for n in pointer_stores if sum(other["target"].get("name") == n["target"].get("name")
                                           for other in pointer_stores) == 2]
    assert len(arms) == 2 and arms[0]["target"]["name"] == arms[1]["target"]["name"], arms
    assert [gc_identity(function, n["value"]) for n in arms] == [
        ("parameter", function["params"][i]["name"]) for i in (1, 2)]
    assert [n["callee"] for n in gc_calls(rv_lines[20])] == [
        rv_lines[17]["name"], rv_lines[18]["name"], rv_lines[1]["name"]]
    function = rv_lines[16]
    calls = gc_calls(function)
    assert len(calls) == 1 and calls[0]["callee"] == rv_lines[8]["name"], calls
    assert gc_identity(function, calls[0]["args"][0]) == ("parameter", function["params"][0]["name"])
    returned = next(n["value"] for n in function["body"] if n["op"] == "return")
    # Resolve return pointer snapshots only to the call-result storage, whose
    # identity is the actual returned reference rather than a new record owner.
    while returned["kind"] in ("cast", "address", "dereference"):
        returned = returned["args"][0]
    return_values = {n["target"]["name"]: n["value"] for n in function["body"]
                     if n["op"] == "assign" and n["target"]["kind"] == "var"}
    while returned["kind"] == "var" and returned["name"] in return_values:
        returned = return_values[returned["name"]]
        while returned["kind"] in ("cast", "address", "dereference"):
            returned = returned["args"][0]
    assert returned["kind"] == "var" and returned["name"] == calls[0]["target"]["name"]
    for function in rvalues["functions"]:
        assert not any(v["type"] == rv_id or v["type"].startswith("arr:") for v in function["locals"]), function
        for node in gc_calls(function):
            callee = rv_functions[node["callee"]]
            assert [a["type"] for a in node["args"]] == [p["type"] for p in callee["params"]], node
            assert not node["callee"].endswith("_destroy"), "a reference became a cleanup owner"
    with tempfile.TemporaryDirectory(prefix="neverc-live-rvalue-relocated-") as temp:
        relocated = check("live-rvalue-relocated", rvalue_source,
                          root=Path(temp) / "project", profile="cpp-core-v2")
        assert relocated == rvalues, "rvalue reference identities depend on the absolute root"

    rvalue_rejected = {
        'reference-return': 'int&&f(){return 1;}',
        'volatile-reference': 'int f(volatile int&&n){return n;}',
        'reference-field': 'struct R{int&&n;};',
        'global-reference': 'int n;int&&r=static_cast<int&&>(n);',
        'function-reference': 'int f(){return 1;}using Fn=int();Fn&&g(){return static_cast<Fn&&>(f);}',
    }
    for name, source in rvalue_rejected.items():
        check("v2-rvalue_rejected-" + name, source, "TR0201", profile="cpp-core-v2")
    rvalue_invalid = {
        'direct-lvalue-binding': 'void f(){int n=1;int&&r=n;}',
        'lvalue-method-on-xvalue': 'struct R{int n;int get()&{return n;}};int f(R&r){return static_cast<R&&>(r).get();}',
        'rvalue-method-on-lvalue': 'struct R{int n;int get()&&{return n;}};int f(R&r){return r.get();}',
        'const-mutation': 'void f(const int&&n){n=1;}',
    }
    for name, source in rvalue_invalid.items():
        check("v2-rvalue_invalid-" + name, source, "TR0202", profile="cpp-core-v2")
    check("v1-rvalue-reference", "int f(int&&r){return r;}", "TR0201")
    user_move_source = """struct R { int n;R*self=this;R*alias=this;
 R(int v):n(v){}
 R(const R&r):n(r.n+1){}
 R(R&&r):n(r.n+10){r.n=-1;}
 R(const R&&r):n(r.n+20){}
 R&operator=(const R&r){n=r.n+1;return *alias;}
 R&operator=(R&&r){n=r.n+10;r.n=-1;return *alias;}
 R&operator=(const R&&r){n=r.n+20;return *alias;}
 ~R(){}
};
R make(){return R(1);}
R forward(){return make();}
R moved(R&&r){return static_cast<R&&>(r);}
R constMoved(const R&&r){return static_cast<const R&&>(r);}
R named(R&&r){return R(r);}
R&assign(R&a,R&b){return a=static_cast<R&&>(b);}
R&constAssign(R&a,const R&b){return a=static_cast<const R&&>(b);}
R&&source(R&r,int&t){t=t*10+2;return static_cast<R&&>(r);}
R&receiver(R&r,int&t){t=t*10+1;return r;}
void ordered(R&a,R&b,int&t){receiver(a,t)=source(b,t);}
void memberOrdered(R&a,R&b,int&t){receiver(a,t).operator=(source(b,t));}
int take(R r){return r.n;}
void consume(R&source){take(static_cast<R&&>(source));}
void local(R&source){R destination(static_cast<R&&>(source));}
R localResult(){R source(4);return source;}
R parameterResult(R source){return source;}
"""
    user_moves = check("v2-user-move-protocol", user_move_source, profile="cpp-core-v2")
    um_record = user_moves["records"][0]
    um_id = um_record["id"]
    um_functions = {f["name"]: f for f in user_moves["functions"]}
    um_lines = {f["loc"]["line"]: f for f in user_moves["functions"]}
    for line, result, params in ((3, "void", ["ptr:" + um_id, "cptr:" + um_id]),
                                 (4, "void", ["ptr:" + um_id, "ptr:" + um_id]),
                                 (5, "void", ["ptr:" + um_id, "cptr:" + um_id]),
                                 (6, "ptr:" + um_id, ["ptr:" + um_id, "cptr:" + um_id]),
                                 (7, "ptr:" + um_id, ["ptr:" + um_id, "ptr:" + um_id]),
                                 (8, "ptr:" + um_id, ["ptr:" + um_id, "cptr:" + um_id]),
                                 (13, "void", ["ptr:" + um_id, "ptr:" + um_id]),
                                 (14, "void", ["ptr:" + um_id, "cptr:" + um_id])):
        function = um_lines[line]
        assert function["result"] == result and [p["type"] for p in function["params"]] == params, function
    assert len({um_lines[line]["name"] for line in (3, 4, 5, 6, 7, 8)}) == 6
    for line, selected in ((13, 4), (14, 5), (15, 3), (16, 7), (17, 8)):
        function = um_lines[line]
        calls = gc_calls(function)
        assert len(calls) == 1 and calls[0]["callee"] == um_lines[selected]["name"], calls
        assert [gc_identity(function, arg) for arg in calls[0]["args"]] == [
            ("parameter", p["name"]) for p in function["params"]], calls
        assert not any(v["type"] == um_id for v in function["locals"]), function
    for line in (3, 4, 5):
        function = um_lines[line]
        receiver = ("parameter", function["params"][0]["name"])
        self_stores = [n for n in function["body"] if n["op"] == "assign"
                       and n["target"].get("name") in (um_record["fields"][1]["name"], um_record["fields"][2]["name"])]
        assert len(self_stores) == 2 and all(gc_identity(function, n["value"]) == receiver for n in self_stores)
    for line in (6, 7, 8):
        function = um_lines[line]
        returned = next(n["value"] for n in function["body"] if n["op"] == "return")
        assert gc_identity(function, returned) == (
            "member", ("parameter", function["params"][0]["name"]), um_record["fields"][2]["name"]), function
    assert [n["callee"] for n in gc_calls(um_lines[20])] == [
        um_lines[18]["name"], um_lines[19]["name"], um_lines[7]["name"]]
    assert [n["callee"] for n in gc_calls(um_lines[21])] == [
        um_lines[19]["name"], um_lines[18]["name"], um_lines[7]["name"]]
    for line, selected in ((11, 2), (12, 11)):
        function = um_lines[line]
        calls = gc_calls(function)
        assert len(calls) == 1 and calls[0]["callee"] == um_lines[selected]["name"]
        assert gc_identity(function, calls[0]["args"][0]) == ("parameter", function["params"][0]["name"])
        assert not any(v["type"] == um_id for v in function["locals"]), function
    destructor = um_id + "_destroy"
    function = um_lines[23]
    calls = gc_calls(function)
    assert [n["callee"] for n in calls] == [um_lines[4]["name"], um_lines[22]["name"]], calls
    assert storage_pointer_object(function, calls[0]["args"][0]) == storage_pointer_object(function, calls[1]["args"][0])
    assert storage_pointer_object(function, calls[0]["args"][1]) == ("parameter", function["params"][0]["name"])
    function = um_lines[24]
    calls = gc_calls(function)
    assert [n["callee"] for n in calls] == [um_lines[4]["name"], destructor], calls
    assert storage_pointer_object(function, calls[0]["args"][0]) == storage_pointer_object(function, calls[1]["args"][0])
    assert storage_pointer_object(function, calls[0]["args"][1]) == ("parameter", function["params"][0]["name"])
    assert [n["callee"] for n in gc_calls(um_lines[22])] == [destructor], um_lines[22]
    function = um_lines[25]
    calls = gc_calls(function)
    assert [n["callee"] for n in calls] == [um_lines[2]["name"], um_lines[4]["name"], destructor], calls
    assert storage_pointer_object(function, calls[1]["args"][0]) == ("parameter", function["params"][0]["name"])
    source_object = storage_pointer_object(function, calls[0]["args"][0])
    assert source_object[0] == "object" and all(
        storage_pointer_object(function, pointer) == source_object
        for pointer in (calls[1]["args"][1], calls[2]["args"][0]))
    function = um_lines[26]
    calls = gc_calls(function)
    assert [n["callee"] for n in calls] == [um_lines[4]["name"], destructor], calls
    assert [storage_pointer_object(function, a) for a in calls[0]["args"]] == [
        ("parameter", p["name"]) for p in function["params"]]
    assert storage_pointer_object(function, calls[1]["args"][0]) == ("parameter", function["params"][1]["name"])
    for function in user_moves["functions"]:
        for node in gc_calls(function):
            callee = um_functions[node["callee"]]
            assert [a["type"] for a in node["args"]] == [p["type"] for p in callee["params"]], node
        assert not any(n["op"] == "assign" and n["target"]["type"] == um_id for n in function["body"]), function
    with tempfile.TemporaryDirectory(prefix="neverc-user-move-relocated-") as temp:
        relocated = check("user-move-relocated", user_move_source,
                          root=Path(temp) / "project", profile="cpp-core-v2")
        assert relocated == user_moves, "user move identities depend on the absolute root"

    user_move_rejected = {
        'deleted-constructor': 'struct R{int n;R(R&&)=delete;};',
        'volatile-constructor': 'struct R{int n;R(volatile R&&r):n(r.n){}};',
        'const-volatile-constructor': 'struct R{int n;R(const volatile R&&r):n(r.n){}};',
        'constructor-default-argument': 'struct R{int n;R(R&&r,int extra=0):n(r.n+extra){}};',
        'deleted-assignment': 'struct R{int n;R&operator=(R&&)=delete;};',
        'volatile-assignment-source': 'struct R{int n;R&operator=(volatile R&&r){n=r.n;return *this;}};',
        'volatile-assignment-receiver': 'struct R{int n;R&operator=(R&&)volatile{return const_cast<R&>(*this);}};',
        'attribute': 'struct R{int n;[[deprecated]] R(R&&r):n(r.n){}};',
        'base': 'struct B{int n;};struct R:B{int value;R(R&&r):value(r.value){}};',
    }
    for name, source in user_move_rejected.items():
        check("v2-" + 'user_move_rejected' + "-" + name, source, "TR0201", profile="cpp-core-v2")
    user_move_invalid = {
        'deleted-implicit-copy': 'struct R{int n;R(R&&r):n(r.n){}};R f(R&r){return R(r);}',
        'lvalue-to-move-only': 'struct R{int n;R&operator=(R&&r){n=r.n;return *this;}};void f(R&a,R&b){a=b;}',
        'invalid-rvalue-receiver': 'struct R{int n;R&operator=(R&&r)&&{n=r.n;return *this;}};void f(R&a,R&b){a=static_cast<R&&>(b);}',
        'write-const-source': 'struct R{int n;R(const R&&r):n(r.n){r.n=1;}};',
    }
    for name, source in user_move_invalid.items():
        check("v2-" + 'user_move_invalid' + "-" + name, source, "TR0202", profile="cpp-core-v2")
    check('v2-user-move-missing-constructor', 'struct R{int n;R(R&&);};', "TR0203", profile="cpp-core-v2")
    check('v2-user-move-missing-assignment', 'struct R{int n;R&operator=(R&&);};', "TR0203", profile="cpp-core-v2")
    check("v1-user-move", "struct R{int n;R(R&&r):n(r.n){}};", "TR0201")
    generated_move_source = """struct Leaf { int n;Leaf*self=this;Leaf*alias=this;
 Leaf(int v):n(v){}
 Leaf(const Leaf&s):n(s.n+100){}
 Leaf(Leaf&&s):n(s.n+10){s.n=-1;}
 Leaf&operator=(const Leaf&s){n=s.n+30;return *alias;}
 Leaf&operator=(Leaf&&s){n=s.n+20;s.n=-1;return *alias;}
 ~Leaf(){}
};
struct CopyOnly{int n;CopyOnly(int v):n(v){}CopyOnly(const CopyOnly&s):n(s.n+100){}CopyOnly&operator=(const CopyOnly&s){n=s.n+30;return *this;}~CopyOnly(){}};
struct Plain{int n;Plain*self=this;Plain(int v):n(v){}Plain(const Plain&s):n(s.n){}~Plain(){}};
struct MovePlain{int n;MovePlain*self=this;MovePlain(int v):n(v){}MovePlain&operator=(MovePlain&&)=default;~MovePlain(){}};
struct Inner{Leaf items[2];};
struct Box{int scalar[2][2];Inner inner;Leaf grid[2][2];CopyOnly fallback[2];Box*self=this;Box(Box&&)=default;Box&operator=(Box&&)=default;};
struct ArrayAssignment{Plain copied[2];MovePlain moved[2];Leaf leaf;};
struct Outside{Leaf leaf;Outside(Outside&&);Outside&operator=(Outside&&);};
Outside::Outside(Outside&&)=default;
Outside&Outside::operator=(Outside&&)=default;
struct Trivial{int values[2];Trivial*self=this;Trivial(Trivial&&)=default;Trivial&operator=(Trivial&&)=default;};
struct Lazy{Leaf leaf;Lazy(Lazy&&)=default;Lazy&operator=(Lazy&&)=default;};
Box build(Box&&source){return static_cast<Box&&>(source);}
void twice(Box&a,Box&b){a=static_cast<Box&&>(b);a.operator=(static_cast<Box&&>(b));}
ArrayAssignment&assignArrays(ArrayAssignment&a,ArrayAssignment&b){return a=static_cast<ArrayAssignment&&>(b);}
Outside outside(Outside&&source){return static_cast<Outside&&>(source);}
Outside&assignOutside(Outside&a,Outside&b){return a=static_cast<Outside&&>(b);}
Trivial trivialMove(Trivial&&source){return static_cast<Trivial&&>(source);}
Trivial&trivialAssign(Trivial&a,Trivial&b){return a=static_cast<Trivial&&>(b);}
Trivial&trivialMember(Trivial&a,Trivial&b){return a.operator=(static_cast<Trivial&&>(b));}
int query(Lazy&source){return sizeof(Lazy(static_cast<Lazy&&>(source)));}
int queryAssignment(Lazy&a,Lazy&b){return sizeof(a=static_cast<Lazy&&>(b));}
Box&&right(Box&r){return static_cast<Box&&>(r);}
Box&left(Box&r){return r;}
void ordered(Box&a,Box&b){left(a)=right(b);}
void memberOrdered(Box&a,Box&b){left(a).operator=(right(b));}
struct Simple{int n;};
Simple makeSimple(){return {1};}
Simple&compatAssign(Simple&r){return r=Simple{2};}
Simple compatConstruct(){return Simple(static_cast<Simple&&>(Simple{3}));}
int compatReceiver(){return (Simple{4}=Simple{5}).n;}
int take(Box value){return value.grid[0][0].n;}
void consume(Box&source){take(static_cast<Box&&>(source));}
"""
    generated_moves = check("v2-generated-move-protocol", generated_move_source, profile="cpp-core-v2")
    gm_records = {r["loc"]["line"]: r for r in generated_moves["records"]}
    gm_functions = {f["name"]: f for f in generated_moves["functions"]}
    gm_lines = {f["loc"]["line"]: f for f in generated_moves["functions"]}

    def gm_helper(record_line, assignment=False, source_const=False):
        rid = gm_records[record_line]["id"]
        result = "ptr:" + rid if assignment else "void"
        params = ["ptr:" + rid, ("cptr:" if source_const else "ptr:") + rid]
        locations = (1, 3, 4, 5, 6) if record_line == 1 else ((15, 16, 17) if record_line == 15 else (record_line,))
        functions = [f for f in generated_moves["functions"] if f["result"] == result
                     and f["loc"]["line"] in locations
                     and [p["type"] for p in f["params"]] == params]
        assert len(functions) == 1, (record_line, assignment, functions)
        return functions[0]

    gm_leaf_move = gm_helper(1)
    gm_leaf_assignment = gm_helper(1, assignment=True)
    gm_copy = gm_helper(9, source_const=True)
    gm_copy_assignment = gm_helper(9, assignment=True, source_const=True)
    gm_inner = gm_helper(12)
    gm_box = gm_helper(13)
    gm_box_assignment = gm_helper(13, assignment=True)
    assert [c["callee"] for c in gc_calls(gm_inner)] == [gm_leaf_move["name"]] * 2
    assert [c["callee"] for c in gc_calls(gm_box)] == [
        gm_inner["name"], *([gm_leaf_move["name"]] * 4), *([gm_copy["name"]] * 2)]
    for i, call in enumerate(gc_calls(gm_inner)):
        for arg, parameter in zip(call["args"], gm_inner["params"]):
            assert gc_identity(gm_inner, arg) == (
                "index", ("member", ("parameter", parameter["name"]), gm_records[12]["fields"][0]["name"]), i)
    fields = gm_records[13]["fields"]
    for call, (row, col) in zip(gc_calls(gm_box)[1:5], ((0, 0), (0, 1), (1, 0), (1, 1))):
        for arg, parameter in zip(call["args"], gm_box["params"]):
            assert gc_identity(gm_box, arg) == (
                "index", ("index", ("member", ("parameter", parameter["name"]), fields[2]["name"]), row), col)
    for i, call in enumerate(gc_calls(gm_box)[5:]):
        for arg, parameter in zip(call["args"], gm_box["params"]):
            assert gc_identity(gm_box, arg) == (
                "index", ("member", ("parameter", parameter["name"]), fields[3]["name"]), i)
    source_arrays = [n for n in gm_box["body"] if n["op"] == "assign"
                     and n["value"]["kind"] == "address"
                     and n["value"]["args"][0]["type"].startswith("arr:")]
    assert len(source_arrays) == 7, source_arrays
    self_store = next(n for n in gm_box["body"] if n["op"] == "assign"
                      and n["target"].get("name") == fields[4]["name"])
    assert gc_identity(gm_box, self_store["value"]) == (
        "member", ("parameter", gm_box["params"][1]["name"]), fields[4]["name"])
    assert [c["callee"] for c in gc_calls(gm_box_assignment)] == [
        gm_helper(12, assignment=True)["name"], gm_leaf_assignment["name"], gm_copy_assignment["name"]]
    scalar_stores = [n for n in gm_box_assignment["body"] if n["op"] == "assign"
                     and n["target"]["kind"] == "index" and n["target"]["type"] == "int"]
    assert len(scalar_stores) == 4, scalar_stores
    for node, (row, col) in zip(scalar_stores, ((0, 0), (0, 1), (1, 0), (1, 1))):
        for expr, parameter in zip((node["target"], node["value"]), gm_box_assignment["params"]):
            assert gc_identity(gm_box_assignment, expr) == (
                "index", ("index", ("member", ("parameter", parameter["name"]), fields[0]["name"]), row), col)
    for record_line in (12, 13, 14, 15):
        function = gm_helper(record_line, assignment=True)
        returned = next(n["value"] for n in function["body"] if n["op"] == "return")
        assert gc_identity(function, returned) == ("parameter", function["params"][0]["name"])
        assert not any(n["op"] == "assign" and n["target"]["type"] == gm_records[record_line]["id"]
                       for n in function["body"]), function
        assert not any(v["type"] == gm_records[record_line]["id"] for v in function["locals"])
    array_assignment = gm_helper(14, assignment=True)
    assert [c["callee"] for c in gc_calls(array_assignment)] == [gm_leaf_assignment["name"]]
    for record_line, field_index in ((10, 0), (11, 1)):
        stores = [n for n in array_assignment["body"] if n["op"] == "assign"
                  and n["target"]["type"] == gm_records[record_line]["id"]]
        assert len(stores) == 2, stores
        for index_value, node in enumerate(stores):
            for expr, parameter in zip((node["target"], node["value"]), array_assignment["params"]):
                assert gc_identity(array_assignment, expr) == (
                    "index", ("member", ("parameter", parameter["name"]),
                              gm_records[14]["fields"][field_index]["name"]), index_value)
    for line, selected in ((20, gm_box), (22, array_assignment), (23, gm_helper(15)),
                           (24, gm_helper(15, assignment=True))):
        function = gm_lines[line]
        calls = gc_calls(function)
        assert len(calls) == 1 and calls[0]["callee"] == selected["name"], calls
        assert [gc_identity(function, arg) for arg in calls[0]["args"]] == [
            ("parameter", p["name"]) for p in function["params"]]
    assert [c["callee"] for c in gc_calls(gm_lines[21])] == [gm_box_assignment["name"]] * 2
    for line in (25, 26, 27):
        function = gm_lines[line]
        assert not gc_calls(function), function
        stores = [n for n in function["body"] if n["op"] == "assign"
                  and n["target"]["type"] == gm_records[18]["id"]]
        assert len(stores) == 1 and [gc_identity(function, expr) for expr in (
            stores[0]["target"], stores[0]["value"])] == [
                ("parameter", p["name"]) for p in function["params"]], stores
    for line in (28, 29, 36, 37, 38):
        assert not gc_calls(gm_lines[line]), gm_lines[line]
    for record_line in (18, 19, 34):
        rid = gm_records[record_line]["id"]
        assert not any(f["loc"]["line"] == record_line and f["result"] in ("void", "ptr:" + rid)
                       and [p["type"] for p in f["params"]] == ["ptr:" + rid] * 2
                       for f in generated_moves["functions"]), record_line
    assert [c["callee"] for c in gc_calls(gm_lines[32])] == [gm_lines[30]["name"], gm_lines[31]["name"], gm_box_assignment["name"]]
    assert [c["callee"] for c in gc_calls(gm_lines[33])] == [gm_lines[31]["name"], gm_lines[30]["name"], gm_box_assignment["name"]]
    function = gm_lines[40]
    calls = gc_calls(function)
    assert [c["callee"] for c in calls] == [gm_box["name"], gm_lines[39]["name"]], calls
    assert storage_pointer_object(function, calls[0]["args"][0]) == storage_pointer_object(function, calls[1]["args"][0])
    assert storage_pointer_object(function, calls[0]["args"][1]) == ("parameter", function["params"][0]["name"])
    assert [c["callee"] for c in gc_calls(gm_lines[39])] == [gm_records[13]["id"] + "_destroy"]
    for function in generated_moves["functions"]:
        for call in gc_calls(function):
            callee = gm_functions[call["callee"]]
            assert [a["type"] for a in call["args"]] == [p["type"] for p in callee["params"]], call
    with tempfile.TemporaryDirectory(prefix="neverc-generated-move-relocated-") as temp:
        relocated = check("generated-move-relocated", generated_move_source,
                          root=Path(temp) / "project", profile="cpp-core-v2")
        assert relocated == generated_moves, "generated move identities depend on the absolute root"

    generated_move_rejected = {
        'deleted-constructor': 'struct R{int n;R(R&&)=delete;};',
        'deleted-assignment': 'struct R{int n;R&operator=(R&&)=delete;};',
        'defaulted-deleted-constructor': 'struct I{int n;I(I&&)=delete;};struct R{I i;R(R&&)=default;};',
        'defaulted-deleted-assignment': 'struct I{int n;I&operator=(I&&)=delete;};struct R{I i;R&operator=(R&&)=default;};',
        'attribute': 'struct R{int n;[[deprecated]] R(R&&)=default;};',
        'reference-field': 'struct R{int&n;R(R&&)=default;};',
        'const-field': 'struct R{const int n;R(R&&)=default;};',
        'private-field': 'class R{int n;public:R(R&&)=default;};',
        'base': 'struct B{int n;};struct R:B{int value;R(R&&)=default;};',
        'source-builtin': 'struct R{int n[2];};void f(R&a,R&b){__builtin_memcpy(&a,&b,sizeof(R));}',
        'lambda-array': 'int f(){int a[2]={1,2};auto capture=[a](){return a[0];};return capture();}',
        'expansion': 'struct I{int n;I(I&&s):n(s.n){}};struct R{I items[65536];R(R&&)=default;};R f(R&&s){return static_cast<R&&>(s);}',
    }
    for name, source in generated_move_rejected.items():
        check("v2-" + 'generated_move_rejected' + "-" + name, source, "TR0201", profile="cpp-core-v2")
    generated_move_invalid = {
        'const-constructor-source': 'struct R{int n;R(const R&&)=default;};',
        'volatile-constructor-source': 'struct R{int n;R(volatile R&&)=default;};',
        'const-assignment-source': 'struct R{int n;R&operator=(const R&&)=default;};',
        'volatile-assignment-source': 'struct R{int n;R&operator=(volatile R&&)=default;};',
        'const-assignment-receiver': 'struct R{int n;R&operator=(R&&)const=default;};',
        'volatile-assignment-receiver': 'struct R{int n;R&operator=(R&&)volatile=default;};',
        'const-assignment-result': 'struct R{int n;const R&operator=(R&&)=default;};',
        'value-assignment-result': 'struct R{int n;R operator=(R&&)=default;};',
        'lvalue-binding': 'struct R{int n;R(R&&)=default;};void f(R&r){R s(r);}',
        'invalid-ref-receiver': 'struct R{int n;R&operator=(R&&)&&=default;};void f(R&a,R&b){a=static_cast<R&&>(b);}',
    }
    for name, source in generated_move_invalid.items():
        check("v2-" + 'generated_move_invalid' + "-" + name, source, "TR0202", profile="cpp-core-v2")
    check('v2-generated-move-missing-constructor', 'struct I{int n;I(I&&);};struct R{I i;R(R&&)=default;};R f(R&&r){return static_cast<R&&>(r);}', "TR0203", profile="cpp-core-v2")
    check('v2-generated-move-missing-assignment', 'struct I{int n;I&operator=(I&&);};struct R{I i;R&operator=(R&&)=default;};void f(R&a,R&b){a=static_cast<R&&>(b);}', "TR0203", profile="cpp-core-v2")
    check("v1-generated-move", "struct R{int n;R(R&&)=default;};", "TR0201")
    noexcept_source = """constexpr bool condition(){return true;}
int safe(int&n)noexcept(condition()){return ++n;}
int unsafe(int&n)noexcept(false){return ++n;}
int plain(int&n){return ++n;}
int legacy(int&n)throw(){return ++n;}
int propagated(int&n)noexcept(noexcept(safe(n))){return safe(n);}
struct Leaf {
 int n;
 Leaf(int v)noexcept:n(v){}
 Leaf(const Leaf&s)noexcept(false):n(s.n){}
 Leaf(Leaf&&s)noexcept:n(s.n){s.n=-1;}
 Leaf&operator=(const Leaf&s)noexcept(false){n=s.n;return *this;}
 Leaf&operator=(Leaf&&s)noexcept{n=s.n;s.n=-2;return *this;}
 int read()const & noexcept{return n;}
 int read()&& noexcept(false){return n;}
 ~Leaf()noexcept{}
};
struct Risky{int*n;Risky(int&v)noexcept:n(&v){++v;}~Risky()noexcept(false){++*n;}};
struct Auto{Leaf leaf;};
struct Explicit{int n=1;Explicit()noexcept(false)=default;};
struct Lazy{Leaf leaf;Lazy(Lazy&&)noexcept=default;};
constexpr bool constant=noexcept(1+2);
static_assert(constant);
bool increment(int&n){return noexcept(++n);}
bool safeQuery(int&n){return noexcept(safe(n));}
bool unsafeQuery(int&n){return noexcept(unsafe(n));}
bool plainQuery(int&n){return noexcept(plain(n));}
bool legacyQuery(int&n){return noexcept(legacy(n));}
bool propagatedQuery(int&n){return noexcept(propagated(n));}
bool nested(int&n){return noexcept(noexcept(unsafe(++n)));}
bool construction(){return noexcept(Leaf(1));}
bool destruction(int&n){return noexcept(Risky(n));}
bool copyQuery(Leaf&r){return noexcept(Leaf(r));}
bool moveQuery(Leaf&r){return noexcept(Leaf(static_cast<Leaf&&>(r)));}
bool copyAssignQuery(Leaf&a,Leaf&b){return noexcept(a=b);}
bool moveAssignQuery(Leaf&a,Leaf&b){return noexcept(a=static_cast<Leaf&&>(b));}
bool methodQuery(Leaf&r){return noexcept(r.read());}
bool rvalueMethodQuery(Leaf&r){return noexcept(static_cast<Leaf&&>(r).read());}
bool autoCopy(Auto&r){return noexcept(Auto(r));}
bool autoMove(Auto&r){return noexcept(Auto(static_cast<Auto&&>(r)));}
bool explicitQuery(){return noexcept(Explicit());}
bool lazyQuery(Lazy&r){return noexcept(Lazy(static_cast<Lazy&&>(r)));}
Leaf selectedCopy(Leaf&r){return Leaf(r);}
Leaf selectedMove(Leaf&r){return Leaf(static_cast<Leaf&&>(r));}
Leaf&selectedAssignment(Leaf&a,Leaf&b){return a=static_cast<Leaf&&>(b);}
"""
    noexcept_module = check("v2-noexcept-protocol", noexcept_source, profile="cpp-core-v2")
    nq_functions = {f["name"]: f for f in noexcept_module["functions"]}

    def nq_line(prefix):
        lines = [i for i, text in enumerate(noexcept_source.splitlines(), 1) if text.startswith(prefix)]
        assert len(lines) == 1, (prefix, lines)
        return lines[0]

    def nq_function(prefix):
        line = nq_line(prefix)
        functions = [f for f in noexcept_module["functions"] if f["loc"]["line"] == line]
        assert len(functions) == 1, (prefix, functions)
        return functions[0]

    def nq_constant(function, expr):
        if expr["kind"] == "literal":
            assert expr["type"] == "bool" and isinstance(expr["value"], bool), expr
            return expr["value"]
        assert expr["kind"] == "var", expr
        assignments = [n["value"] for n in function["body"] if n["op"] == "assign"
                       and n["target"].get("name") == expr["name"]]
        assert len(assignments) == 1, (expr, assignments)
        return nq_constant(function, assignments[0])

    for name, expected in {
        "increment": True, "safeQuery": True, "unsafeQuery": False, "plainQuery": False,
        "legacyQuery": True, "propagatedQuery": True, "nested": True, "construction": True,
        "destruction": False, "copyQuery": False, "moveQuery": True, "copyAssignQuery": False,
        "moveAssignQuery": True, "methodQuery": True, "rvalueMethodQuery": False,
        "autoCopy": False, "autoMove": True, "explicitQuery": False, "lazyQuery": True,
    }.items():
        function = nq_function("bool " + name + "(")
        assert function["result"] == "bool" and not gc_calls(function), function
        assert all(v["type"] == "bool" for v in function["locals"]), function
        assert all(n["op"] in ("label", "return") or (n["op"] == "assign"
                   and n["target"]["kind"] == "var" and n["target"]["type"] == "bool")
                   for n in function["body"]), function
        returns = [n["value"] for n in function["body"] if n["op"] == "return"]
        assert len(returns) == 1 and nq_constant(function, returns[0]) is expected, (name, returns)
    propagated_function = nq_function("int propagated(")
    assert [call["callee"] for call in gc_calls(propagated_function)] == [nq_function("int safe(")["name"]]
    assert not gc_calls(nq_function("int safe(")), "declaration noexcept(condition()) executed its condition"
    for caller, callee in (("Leaf selectedCopy(", " Leaf(const Leaf&"),
                           ("Leaf selectedMove(", " Leaf(Leaf&&"),
                           ("Leaf&selectedAssignment(", " Leaf&operator=(Leaf&&")):
        function = nq_function(caller)
        calls = gc_calls(function)
        assert len(calls) == 1 and calls[0]["callee"] == nq_function(callee)["name"], function
        assert [gc_identity(function, arg) for arg in calls[0]["args"]] == [
            ("parameter", p["name"]) for p in function["params"]]
    for function in noexcept_module["functions"]:
        for call in gc_calls(function):
            assert [a["type"] for a in call["args"]] == [
                p["type"] for p in nq_functions[call["callee"]]["params"]], call
    assert any(g["type"] == "bool" and g["value"]["value"] is True
               for g in noexcept_module["globals"])
    with tempfile.TemporaryDirectory(prefix="neverc-noexcept-relocated-") as temp:
        relocated = check("noexcept-relocated", noexcept_source, root=Path(temp) / "project",
                          profile="cpp-core-v2")
        assert relocated == noexcept_module, "noexcept identities depend on the absolute root"

    noexcept_rejected = {
        'query-size-double': 'bool f(){return noexcept(sizeof(double));}',
        'unused-query': 'void f(){noexcept(sizeof(double));}',
        'spec-size-double': 'int f()noexcept(sizeof(double)>0){return 1;}',
        'short-circuit-spec': 'int f()noexcept(true || noexcept(sizeof(double))){return 1;}',
        'nested-query': 'bool f(){return noexcept(noexcept(sizeof(double)));}',
        'defaulted-spec': 'struct R{int n;R()noexcept(sizeof(double)>0)=default;};',
        'out-of-line-spec': 'struct R{int n;R()noexcept(sizeof(double)>0);};R::R()noexcept(true)=default;',
        'redeclaration-spec': 'int f()noexcept(sizeof(double)>0);int f()noexcept(true){return 1;}',
        'default-field-query': 'struct R{bool b=noexcept(sizeof(double));};',
        'throw-query': 'bool f(){return noexcept(throw 1);}',
        'throw-body': 'int f()noexcept{throw 1;}',
        'catch-body': 'int f()noexcept(false){try{return 1;}catch(...){return 2;}}',
        'vendor-nothrow': '__attribute__((nothrow)) int f(){return 1;}',
        'dependent-spec': 'template<class T>int f(T&t)noexcept(noexcept(t.get())){return 1;}',
        'explicit-destruction-query': 'struct R{int n;~R()noexcept{}};bool f(R&r){return noexcept(r.~R());}',
    }
    for name, source in noexcept_rejected.items():
        check("v2-" + 'noexcept_rejected' + "-" + name, source, "TR0201", profile="cpp-core-v2")
    noexcept_invalid = {
        'nonconstant-spec': 'void f(int n)noexcept(n){}',
        'incompatible-redeclaration': 'int f()noexcept;int f()noexcept(false){return 1;}',
        'typed-dynamic-spec': 'int f()throw(int){return 1;}',
        'invalid-query-operand': 'bool f(){return noexcept(unknown());}',
    }
    for name, source in noexcept_invalid.items():
        check("v2-" + 'noexcept_invalid' + "-" + name, source, "TR0202", profile="cpp-core-v2")
    noexcept_missing = {
        'query-call': 'int missing()noexcept;bool f(){return noexcept(missing());}',
        'query-constructor': 'struct R{int n;R()noexcept;};bool f(){return noexcept(R());}',
    }
    for name, source in noexcept_missing.items():
        check("v2-" + 'noexcept_missing' + "-" + name, source, "TR0203", profile="cpp-core-v2")
    check("v1-noexcept-spec", "int f()noexcept{return 1;}", "TR0201")
    check("v1-noexcept-query", "bool f(){return noexcept(1+2);}", "TR0201")
    operator_source = """struct R {
 int n;
 R&operator+=(const R&r){n=r.n;return *this;}
 int operator<<(int v)const{return n<<v;}
 int&operator[](int){return n;}
 bool operator&&(const R&r)const{return n&&r.n;}
 bool operator||(const R&r)const{return n||r.n;}
 R&operator,(R&r){return r;}
 R&operator++(){++n;return *this;}
 R operator++(int){return {n++};}
 int&operator*(){return n;}
 int*operator&(){return &n;}
 int operator()()const noexcept{return n;}
 R operator()(int v)const{return {n+v};}
 ~R(){}
};
R operator+(R value,int n){return {value.n+n};}
bool operator==(const R&a,const R&b){return a.n==b.n;}
R&left(R&r){return r;}
R&right(R&r){return r;}
int index(){return 1;}
void ordered(R&a,R&b){left(a)+=right(b);}
void memberOrdered(R&a,R&b){left(a).operator+=(right(b));}
int shift(R&a){return left(a)<<index();}
int&subscript(R&a){return left(a)[index()];}
bool logicalAnd(R&a,R&b){return left(a)&&right(b);}
bool logicalOr(R&a,R&b){return left(a)||right(b);}
R&comma(R&a,R&b){return (left(a),right(b));}
void postfix(R&r){r++;}
R&prefix(R&r){return ++r;}
int&dereference(R&r){return *r;}
int*address(R&r){return &r;}
int functor(R&r){return r();}
R result(R&r){return r(7);}
R freeResult(R&r){return r+1;}
bool equality(R&a,R&b){return a==b;}
bool query(R&r){return noexcept(r());}
struct P {
 int n;
 P(const P&s):n(s.n){}
 ~P(){}
};
void operator+=(P lhs,P rhs){if(lhs.n)return;rhs.n=0;}
P operator-=(P lhs,P rhs){return lhs;}
void freeOrdered(P&a,P&b){a+=b;}
void freeExplicit(P&a,P&b){operator+=(a,b);}
P freeValue(P&a,P&b){return a-=b;}
P explicitValue(P&a,P&b){return operator-=(a,b);}
"""
    operators = check("v2-overloaded-operators-protocol", operator_source, profile="cpp-core-v2")
    op_functions = {f["name"]: f for f in operators["functions"]}

    def op_line(prefix):
        lines = [i for i, text in enumerate(operator_source.splitlines(), 1) if text.startswith(prefix)]
        assert len(lines) == 1, (prefix, lines)
        return lines[0]

    def op_function(prefix):
        functions = [f for f in operators["functions"] if f["loc"]["line"] == op_line(prefix)]
        assert len(functions) == 1, (prefix, functions)
        return functions[0]

    def op_names(prefix):
        return [c["callee"] for c in gc_calls(op_function(prefix))]

    op_records = {r["loc"]["line"]: r for r in operators["records"]}
    rid = op_records[op_line("struct R {")]["id"]
    pid = op_records[op_line("struct P {")]["id"]
    left_name = op_function("R&left(")["name"]
    right_name = op_function("R&right(")["name"]
    index_name = op_function("int index(")["name"]
    assignment_name = op_function(" R&operator+=(")["name"]
    assert op_names("void ordered(") == [right_name, left_name, assignment_name]
    assert op_names("void memberOrdered(") == [left_name, right_name, assignment_name]
    for caller, callee in (("int shift(", " int operator<<("),
                           ("int&subscript(", " int&operator[](")):
        assert op_names(caller) == [left_name, index_name, op_function(callee)["name"]]
    for caller, callee in (("bool logicalAnd(", " bool operator&&("),
                           ("bool logicalOr(", " bool operator||("),
                           ("R&comma(", " R&operator,(")):
        function = op_function(caller)
        assert op_names(caller) == [left_name, right_name, op_function(callee)["name"]]
        assert not any(n["op"] == "branch" for n in function["body"]), function
    for caller, callee in (("R&prefix(", " R&operator++("),
                           ("int&dereference(", " int&operator*("),
                           ("int*address(", " int*operator&("),
                           ("int functor(", " int operator()("),
                           ("bool equality(", "bool operator==(")):
        function = op_function(caller)
        assert op_names(caller) == [op_function(callee)["name"]]
        assert not any(v["type"] in (rid, pid) for v in function["locals"]), function
    postfix = op_function("void postfix(")
    postfix_operator = op_function(" R operator++(")
    calls = gc_calls(postfix)
    assert [c["callee"] for c in calls] == [postfix_operator["name"], rid + "_destroy"]
    assert [p["type"] for p in postfix_operator["params"]] == ["ptr:" + rid, "ptr:" + rid, "int"]
    assert gc_identity(postfix, calls[0]["args"][2]) == 0
    assert storage_pointer_object(postfix, calls[0]["args"][0]) == storage_pointer_object(postfix, calls[1]["args"][0])
    for caller, callee in (("R result(", " R operator()("), ("R freeResult(", "R operator+(")):
        function = op_function(caller)
        calls = gc_calls(function)
        assert len(calls) == 1 and calls[0]["callee"] == op_function(callee)["name"], calls
        assert gc_identity(function, calls[0]["args"][0]) == ("parameter", function["params"][0]["name"])
    assert [p["type"] for p in op_function("bool operator==(")["params"]] == ["cptr:" + rid] * 2
    assert [p["type"] for p in op_function("R operator+(")["params"]] == ["ptr:" + rid, "ptr:" + rid, "int"]
    assert op_names("R operator+(") == [rid + "_destroy"]
    query = op_function("bool query(")
    assert not gc_calls(query) and not any(v["type"] == rid for v in query["locals"])
    returned = next(n["value"] for n in query["body"] if n["op"] == "return")
    assert nq_constant(query, returned) is True
    p_copy = op_function(" P(const P&")
    for caller, callee, result_offset in (("void freeOrdered(", "void operator+=(", 0),
                                          ("void freeExplicit(", "void operator+=(", 0),
                                          ("P freeValue(", "P operator-=(", 1),
                                          ("P explicitValue(", "P operator-=(", 1)):
        function = op_function(caller)
        calls = gc_calls(function)
        assert [c["callee"] for c in calls] == [p_copy["name"], p_copy["name"], op_function(callee)["name"]]
        assert [gc_identity(function, c["args"][1]) for c in calls[:2]] == [
            ("parameter", function["params"][result_offset + i]["name"]) for i in (1, 0)]
        assert [storage_pointer_object(function, a) for a in calls[-1]["args"][result_offset:]] == [
            storage_pointer_object(function, calls[i]["args"][0]) for i in (1, 0)]
        if result_offset:
            assert gc_identity(function, calls[-1]["args"][0]) == ("parameter", function["params"][0]["name"])
        assert sum(v["type"] == pid for v in function["locals"]) == 2, function
    for callee, offset, cleanup_count in (("void operator+=(", 0, 4), ("P operator-=(", 1, 2)):
        function = op_function(callee)
        cleanup = [c for c in gc_calls(function) if c["callee"] == pid + "_destroy"]
        assert len(cleanup) == cleanup_count, cleanup
        assert [gc_identity(function, c["args"][0]) for c in cleanup] == [
            ("parameter", function["params"][offset + i]["name"])
            for i in ([0, 1] * (cleanup_count // 2))]
    returned_value = op_function("P operator-=(")
    assert op_names("P operator-=(") == [p_copy["name"], pid + "_destroy", pid + "_destroy"]
    assert gc_identity(returned_value, gc_calls(returned_value)[0]["args"][0]) == (
        "parameter", returned_value["params"][0]["name"])
    for function in operators["functions"]:
        for call in gc_calls(function):
            assert [a["type"] for a in call["args"]] == [
                p["type"] for p in op_functions[call["callee"]]["params"]], call
    with tempfile.TemporaryDirectory(prefix="neverc-operators-relocated-") as temp:
        relocated = check("operators-relocated", operator_source, root=Path(temp) / "project",
                          profile="cpp-core-v2")
        assert relocated == operators, "operator identities depend on the absolute root"

    operator_rejected = {
        'deleted': 'struct R{int n;int operator+(int)const=delete;};',
        'volatile-receiver': 'struct R{int n;int operator()()volatile{return n;}};',
        'volatile-argument': 'struct R{int n;int operator+(volatile R&r)const{return r.n;}};',
        'template': 'struct R{int n;template<class T>int operator()(T v){return n;}};',
        'default-argument': 'struct R{int n;int operator()(int v=1){return n+v;}};',
        'friend': 'struct R{int n;friend int operator+(const R&r,int v){return r.n+v;}};',
        'member-pointer': 'struct R{int n;int operator+(int v)const{return n+v;}};void f(){auto p=&R::operator+;}',
        'free-pointer': 'struct R{int n;};int operator+(R r,int v){return r.n+v;}void f(){auto p=&operator+;}',
        'new-member': 'using Size=decltype(sizeof(0));struct R{int n;static void*operator new(Size){return nullptr;}};',
        'delete-member': 'struct R{int n;static void operator delete(void*){}};',
        'new-free': 'using Size=decltype(sizeof(0));void*operator new(Size){return nullptr;}',
        'attribute': 'struct R{int n;[[deprecated]] int operator()()const{return n;}};',
        'virtual': 'struct R{int n;virtual int operator()()const{return n;}};',
    }
    for name, source in operator_rejected.items():
        check("v2-" + 'operator_rejected' + "-" + name, source, "TR0201", profile="cpp-core-v2")
    operator_invalid = {
        'binary-arity': 'struct R{int n;int operator+(int,int){return n;}};',
        'prefix-postfix-type': 'struct R{int n;R&operator++(long){return *this;}};',
        'free-assignment': 'struct R{int n;};R&operator=(R&a,const R&b){return a;}',
        'static-member': 'struct R{int n;static int operator+(int){return 1;}};',
        'free-call': 'struct R{int n;};int operator()(R r){return r.n;}',
    }
    for name, source in operator_invalid.items():
        check("v2-" + 'operator_invalid' + "-" + name, source, "TR0202", profile="cpp-core-v2")
    operator_missing = {
        'member': 'struct R{int n;int operator()()const;};int f(R&r){return r();}',
        'free': 'struct R{int n;};int operator+(R r,int v);int f(R&r){return r+1;}',
    }
    for name, source in operator_missing.items():
        check("v2-" + 'operator_missing' + "-" + name, source, "TR0203", profile="cpp-core-v2")
    check("v1-member-operator", "struct R{int n;int operator()()const{return n;}};", "TR0201")
    check("v1-free-operator", "struct R{int n;};int operator+(R r,int n){return r.n+n;}int f(){R r{1};return r+2;}", "TR0201")
    conversion_source = """struct S {
 int n;
 operator int() & {return n;}
 operator int() const & {return n+1;}
 operator int() && {return n+2;}
 explicit operator bool() const noexcept {return n!=0;}
};
struct Ref {
 int n;
 operator int&(){return n;}
};
struct R {
 int n;
 R(int value):n(value){}
 R(const R&r):n(r.n){}
 R(R&&r):n(r.n){r.n=-1;}
 ~R(){}
};
struct Factory {
 int n;
 operator R()const noexcept{return R(n);}
};
struct RefFactory {
 R*p;
 operator R&(){return *p;}
};
struct MoveFactory {
 R*p;
 operator R&&(){return static_cast<R&&>(*p);}
};
int implicit(S&r){return r;}
int constant(const S&r){return r;}
int rvalue(S&r){return static_cast<S&&>(r);}
long long promotion(S&r){return r;}
int explicitName(S&r){return r.operator int();}
bool logicalAnd(S&a,S&b){return a&&b;}
bool logicalOr(S&a,S&b){return a||b;}
int&alias(Ref&r){return r;}
R result(Factory&r){return r;}
R explicitResult(Factory&r){return r.operator R();}
int local(Factory&r){R value=r;return value.n;}
void discard(Factory&r){static_cast<R>(r);}
R copied(RefFactory&r){return r;}
R moved(MoveFactory&r){return r;}
R&recordAlias(RefFactory&r){return r;}
bool query(const S&r){return noexcept(static_cast<bool>(r));}
bool recordQuery(Factory&r){return noexcept(static_cast<R>(r));}
"""
    conversions = check("v2-conversion-functions-protocol", conversion_source, profile="cpp-core-v2")
    uc_functions = {f["name"]: f for f in conversions["functions"]}

    def uc_line(prefix):
        lines = [i for i, line in enumerate(conversion_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(lines) == 1, (prefix, lines)
        return lines[0]

    def uc_function(prefix):
        matches = [f for f in conversions["functions"] if f["loc"]["line"] == uc_line(prefix)]
        assert len(matches) == 1, (prefix, matches)
        return matches[0]

    uc_records = {r["loc"]["line"]: r["id"] for r in conversions["records"]}
    sid = uc_records[uc_line("struct S {")]
    rid = uc_records[uc_line("struct R {")]
    fid = uc_records[uc_line("struct Factory {")]
    selected_int = [uc_function(p) for p in (" operator int() & {", " operator int() const & {", " operator int() && {")]
    assert len({f["name"] for f in selected_int}) == 3
    assert [f["result"] for f in selected_int] == ["int"] * 3
    assert [[p["type"] for p in f["params"]] for f in selected_int] == [["ptr:"+sid], ["cptr:"+sid], ["ptr:"+sid]]
    for caller, selected in (("int implicit(", 0), ("int constant(", 1), ("int rvalue(", 2),
                             ("long long promotion(", 0), ("int explicitName(", 0)):
        function = uc_function(caller)
        calls = gc_calls(function)
        assert len(calls) == 1 and calls[0]["callee"] == selected_int[selected]["name"], function
        assert storage_pointer_object(function, calls[0]["args"][0]) == ("parameter", function["params"][0]["name"])
        assert not any(v["type"] in uc_records.values() for v in function["locals"]), function
    assert uc_function("long long promotion(")["result"] == "i64"
    bool_name = uc_function(" explicit operator bool(")["name"]
    for prefix in ("bool logicalAnd(", "bool logicalOr("):
        function = uc_function(prefix)
        calls = gc_calls(function)
        assert [c["callee"] for c in calls] == [bool_name, bool_name], function
        assert [storage_pointer_object(function, c["args"][0]) for c in calls] == [
            ("parameter", p["name"]) for p in function["params"]]
        positions = [i for i, node in enumerate(function["body"]) if node["op"] == "call"]
        between = function["body"][positions[0]+1:positions[1]]
        branches = [n for n in between if n["op"] == "branch"]
        labels = [n["label"] for n in between if n["op"] == "label"]
        assert len(branches) == 1 and labels, function
        assert labels[-1] == branches[0]["true" if prefix == "bool logicalAnd(" else "false"], function

    def uc_reference_origin(function, expr):
        if expr["kind"] in ("cast", "address", "dereference"):
            return uc_reference_origin(function, expr["args"][0])
        assert expr["kind"] == "var", expr
        calls = [c for c in gc_calls(function) if c.get("target", {}).get("name") == expr["name"]]
        if calls:
            assert len(calls) == 1
            return calls[0]["callee"]
        values = [n["value"] for n in function["body"] if n["op"] == "assign"
                  and n["target"].get("kind") == "var" and n["target"]["name"] == expr["name"]]
        assert len(values) == 1, (expr, values)
        return uc_reference_origin(function, values[0])

    for prefix, conversion, result_type in (("int&alias(", " operator int&(", "ptr:int"),
                                             ("R&recordAlias(", " operator R&(", "ptr:"+rid)):
        function = uc_function(prefix)
        calls = gc_calls(function)
        name = uc_function(conversion)["name"]
        assert len(calls) == 1 and calls[0]["callee"] == name and function["result"] == result_type, function
        assert not any(v["type"] in uc_records.values() for v in function["locals"]), function
        returns = [n["value"] for n in function["body"] if n["op"] == "return"]
        assert len(returns) == 1 and uc_reference_origin(function, returns[0]) == name
    record_conversion = uc_function(" operator R()const")
    assert record_conversion["result"] == "void"
    assert [p["type"] for p in record_conversion["params"]] == ["ptr:"+rid, "cptr:"+fid]
    constructor = uc_function(" R(int value)")
    calls = gc_calls(record_conversion)
    assert len(calls) == 1 and calls[0]["callee"] == constructor["name"]
    assert storage_pointer_object(record_conversion, calls[0]["args"][0]) == ("parameter", record_conversion["params"][0]["name"])
    for prefix in ("R result(", "R explicitResult("):
        function = uc_function(prefix)
        calls = gc_calls(function)
        assert len(calls) == 1 and calls[0]["callee"] == record_conversion["name"], function
        assert [storage_pointer_object(function, arg) for arg in calls[0]["args"]] == [
            ("parameter", p["name"]) for p in function["params"]]
        assert not any(v["type"] == rid for v in function["locals"]), function
    for prefix in ("int local(", "void discard("):
        function = uc_function(prefix)
        calls = gc_calls(function)
        assert [c["callee"] for c in calls] == [record_conversion["name"], rid+"_destroy"], function
        objects = [v for v in function["locals"] if v["type"] == rid]
        assert len(objects) == 1, function
        assert [storage_pointer_object(function, c["args"][0]) for c in calls] == [("object", objects[0]["name"])] * 2
    for prefix, conversion, constructor_prefix in (("R copied(", " operator R&(", " R(const R&r)"),
                                                    ("R moved(", " operator R&&(", " R(R&&r)")):
        function = uc_function(prefix)
        calls = gc_calls(function)
        conversion_name = uc_function(conversion)["name"]
        assert [c["callee"] for c in calls] == [conversion_name, uc_function(constructor_prefix)["name"]], function
        assert storage_pointer_object(function, calls[1]["args"][0]) == ("parameter", function["params"][0]["name"])
        assert uc_reference_origin(function, calls[1]["args"][1]) == conversion_name
        assert not any(v["type"] == rid for v in function["locals"]), function
    for prefix in ("bool query(", "bool recordQuery("):
        function = uc_function(prefix)
        assert not gc_calls(function) and not any(v["type"] in uc_records.values() for v in function["locals"]), function
        returns = [n["value"] for n in function["body"] if n["op"] == "return"]
        assert len(returns) == 1 and nq_constant(function, returns[0]) is True
    for function in conversions["functions"]:
        for call in gc_calls(function):
            callee = uc_functions[call["callee"]]
            assert [a["type"] for a in call["args"]] == [p["type"] for p in callee["params"]], call
        assert not any(n["op"] == "assign" and n["value"]["type"] == rid for n in function["body"]), function
    with tempfile.TemporaryDirectory(prefix="neverc-user-conversions-relocated-") as temp:
        relocated = check("conversion-functions-relocated", conversion_source,
                          root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == conversions, "conversion identities depend on the absolute root"

    conversion_rejected = {
        'float': 'struct R{operator double()const{return 1.0;}};',
        'string': 'struct R{operator const char*()const{return "x";}};',
        'volatile': 'struct R{operator int()volatile{return 1;}};',
        'restrict': 'struct R{operator int()__restrict{return 1;}};',
        'template': 'struct R{template<class T>operator T()const{return T{};}};',
        'member-address': 'struct R{operator int()const{return 1;}};auto f(){return &R::operator int;}',
        'function-pointer': 'using F=int(*)();int g(){return 1;}struct R{operator F()const{return g;}};',
        'empty-record-conversion': 'struct T{int n;};struct R{operator T()const{return {1};}};int f(){R r;const T&t=r;return t.n;}',
        'unused-throw': 'struct R{operator int()const{throw 1;}};',
        'query-throw': 'struct R{operator int()const noexcept(false){throw 1;}};bool f(R&r){return noexcept(static_cast<int>(r));}',
        'folded-float': 'struct R{constexpr operator int()const{return static_cast<int>(1.0);}};constexpr R r{};static_assert(int(r)==1,"value");',
        'default-argument': 'int g(int n=1){return n;}struct R{operator int()const{return g();}};',
        'virtual': 'struct R{virtual operator int()const{return 1;}};',
    }
    for name, source in conversion_rejected.items():
        check("v2-" + 'conversion_rejected' + "-" + name, source, "TR0201", profile="cpp-core-v2")
    conversion_invalid = {
        'explicit-copy': 'struct R{explicit operator int()const{return 1;}};int f(){R r;int n=r;return n;}',
        'parameters': 'struct R{operator int(int n){return n;}};',
        'written-result': 'struct R{int operator int(){return 1;}};',
        'ambiguous': 'struct R{operator long(){return 1;}operator unsigned long(){return 1;}};int f(){R r;return r;}',
        'deleted-use': 'struct R{operator int()const=delete;};int f(){R r;return r;}',
        'ref-qualifier': 'struct R{operator int()&&{return 1;}};int f(){R r;return r;}',
    }
    for name, source in conversion_invalid.items():
        check("v2-" + 'conversion_invalid' + "-" + name, source, "TR0202", profile="cpp-core-v2")
    conversion_missing = {
        'implicit': 'struct R{operator int()const;};int f(R&r){return r;}',
        'explicit': 'struct R{explicit operator bool()const;};bool f(R&r){return static_cast<bool>(r);}',
    }
    for name, source in conversion_missing.items():
        check("v2-" + 'conversion_missing' + "-" + name, source, "TR0203", profile="cpp-core-v2")
    check("v1-conversion-function", "struct R{int n;operator int()const{return n;}};", "TR0201")
    check("v1-explicit-conversion", "struct R{explicit operator bool()const{return true;}};bool f(R&r){return static_cast<bool>(r);}", "TR0201")
    temporary_call_source = """struct R {
 int n;
 R(int value):n(value){}
 ~R(){}
 int get(const int&v)const{return n+v;}
 int&ref()&&{return n;}
 R make()&&{return R(n+1);}
 explicit operator bool()const noexcept{return n!=0;}
 R&operator+=(const R&r){n+=r.n;return *this;}
};
int number(const int&n){return n;}
int take(const R&r){return r.n;}
int scalar(){return number(7);}
int receiver(){return R(1).get(2);}
int argument(){return take(R(3));}
int alias(){return number(R(4).ref());}
R result(){return R(5).make();}
void assignment(){R(1)+=R(2);}
bool logical(){return R(0)&&R(1);}
void loop(int count){for(int i=0;i<count;++i)R(i).get(1);}
struct H{R array[2];};
int subobject(){return (H{{R(6),R(7)}}.array+1)->get(2);}
bool query(){return noexcept(R(1).get(2));}
"""
    temporary_calls = check("v2-full-expression-temporary-calls-protocol", temporary_call_source, profile="cpp-core-v2")
    tc_functions = {f["name"]: f for f in temporary_calls["functions"]}

    def tc_line(prefix):
        matches = [i for i, line in enumerate(temporary_call_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(matches) == 1, (prefix, matches)
        return matches[0]

    def tc_function(prefix):
        matches = [f for f in temporary_calls["functions"] if f["loc"]["line"] == tc_line(prefix)]
        assert len(matches) == 1, (prefix, matches)
        return matches[0]

    tc_records = {r["loc"]["line"]: r["id"] for r in temporary_calls["records"]}
    rid = tc_records[tc_line("struct R {")]
    hid = tc_records[tc_line("struct H{")]
    constructor = tc_function(" R(int value)")["name"]
    get_name = tc_function(" int get(")["name"]
    number_name = tc_function("int number(")["name"]
    ref_name = tc_function(" int&ref(")["name"]
    make_name = tc_function(" R make(")["name"]
    scalar = tc_function("int scalar(")
    calls = gc_calls(scalar)
    assert len(calls) == 1 and calls[0]["callee"] == number_name
    scalar_object = storage_pointer_object(scalar, calls[0]["args"][0])
    assert scalar_object[0] == "object" and any(v["name"] == scalar_object[1] and v["type"] == "int" for v in scalar["locals"])
    values = [n["value"] for n in scalar["body"] if n["op"] == "assign"
              and n["target"].get("name") == scalar_object[1]]
    assert len(values) == 1 and gc_identity(scalar, values[0]) == 7
    for prefix, callee in (("int receiver(", get_name), ("int argument(", tc_function("int take(")["name"])):
        function = tc_function(prefix)
        calls = gc_calls(function)
        assert [c["callee"] for c in calls] == [constructor, callee, rid+"_destroy"], function
        objects = [v for v in function["locals"] if v["type"] == rid]
        assert len(objects) == 1
        assert [storage_pointer_object(function, c["args"][0]) for c in calls] == [("object", objects[0]["name"])] * 3
    function = tc_function("int alias(")
    calls = gc_calls(function)
    assert [c["callee"] for c in calls] == [constructor, ref_name, number_name, rid+"_destroy"], function
    assert uc_reference_origin(function, calls[2]["args"][0]) == ref_name
    assert storage_pointer_object(function, calls[0]["args"][0]) == storage_pointer_object(function, calls[3]["args"][0])
    result = tc_function("R result(")
    calls = gc_calls(result)
    assert [c["callee"] for c in calls] == [constructor, make_name, rid+"_destroy"], result
    assert storage_pointer_object(result, calls[1]["args"][0]) == ("parameter", result["params"][0]["name"])
    receiver_object = storage_pointer_object(result, calls[0]["args"][0])
    assert receiver_object[0] == "object"
    assert storage_pointer_object(result, calls[1]["args"][1]) == receiver_object
    assert storage_pointer_object(result, calls[2]["args"][0]) == receiver_object
    function = tc_function("void assignment(")
    calls = gc_calls(function)
    assert [c["callee"] for c in calls] == [constructor, constructor, tc_function(" R&operator+=(")["name"], rid+"_destroy", rid+"_destroy"], function
    assert [gc_identity(function, c["args"][1]) for c in calls[:2]] == [2, 1]
    rhs, lhs = [storage_pointer_object(function, c["args"][0]) for c in calls[:2]]
    assert rhs != lhs
    assert [storage_pointer_object(function, arg) for arg in calls[2]["args"]] == [lhs, rhs]
    assert [storage_pointer_object(function, c["args"][0]) for c in calls[3:]] == [lhs, rhs]
    function = tc_function("bool logical(")
    calls = gc_calls(function)
    bool_name = tc_function(" explicit operator bool(")["name"]
    assert [c["callee"] for c in calls] == [constructor, bool_name, constructor, bool_name, rid+"_destroy", rid+"_destroy"], function
    assert [storage_pointer_object(function, c["args"][0]) for c in calls[-2:]] == [
        storage_pointer_object(function, calls[i]["args"][0]) for i in (2, 0)]
    positions = [i for i, n in enumerate(function["body"]) if n["op"] == "call"]
    assert any(n["op"] == "branch" for n in function["body"][positions[1]+1:positions[2]]), function
    # Each conditional owner starts false, becomes live after construction and
    # is cleared in its guarded cleanup. A skipped RHS keeps its flag false.
    for index in positions[-2:]:
        labels = [n["label"] for n in function["body"][:index] if n["op"] == "label"]
        guards = [n for n in function["body"][:index] if n["op"] == "branch" and n["true"] == labels[-1]]
        assert len(guards) == 1 and guards[0]["condition"]["kind"] == "var", function
        flag = guards[0]["condition"]["name"]
        values = [n["value"] for n in function["body"] if n["op"] == "assign" and n["target"].get("name") == flag]
        assert [v["value"] for v in values] == [False, True, False], values
    loop = tc_function("void loop(")
    assert [c["callee"] for c in gc_calls(loop)] == [constructor, get_name, rid+"_destroy"], loop
    assert len([v for v in loop["locals"] if v["type"] == rid]) == 1
    subobject = tc_function("int subobject(")
    calls = gc_calls(subobject)
    assert [c["callee"] for c in calls] == [constructor, constructor, get_name, hid+"_destroy"], subobject
    assert len([v for v in subobject["locals"] if v["type"] == hid]) == 1
    assert not any(v["type"] == rid for v in subobject["locals"]), subobject
    assert [c["callee"] for c in gc_calls(tc_functions[hid+"_destroy"])] == [rid+"_destroy", rid+"_destroy"]
    query = tc_function("bool query(")
    assert not gc_calls(query) and not any(v["type"] in tc_records.values() for v in query["locals"]), query
    values = [n["value"] for n in query["body"] if n["op"] == "return"]
    assert len(values) == 1 and nq_constant(query, values[0]) is False
    for function in temporary_calls["functions"]:
        for call in gc_calls(function):
            assert [a["type"] for a in call["args"]] == [p["type"] for p in tc_functions[call["callee"]]["params"]], call
    with tempfile.TemporaryDirectory(prefix="neverc-temporary-calls-relocated-") as temp:
        relocated = check("temporary-calls-relocated", temporary_call_source,
                          root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == temporary_calls, "temporary-call identities depend on the absolute root"

    temporary_call_rejected = {
        'empty-conversion-record': 'struct R{operator int()const{return 1;}};int f(){R r;const int&n=r;return n;}',
        'fresh-return': 'const int&f(){return 1;}',
        'fresh-record-return': 'struct R{int n;};const R&f(){return R{1};}',
        'static-extension': 'int f(){static const int&r=1;return r;}',
        'global-extension': 'const int&r=1;',
        'unused-throw': 'struct R{int get()const{throw 1;}};int f(){return R{}.get();}',
        'query-float': 'struct R{double get()const noexcept{return 1.0;}};bool f(){return noexcept(R{}.get());}',
    }
    for name, source in temporary_call_rejected.items():
        check("v2-" + 'temporary_call_rejected' + "-" + name, source, "TR0201", profile="cpp-core-v2")
    temporary_call_invalid = {
        'mutable-reference': 'void take(int&){}void f(){take(1);}',
        'mutable-record-reference': 'struct R{int n;};void take(R&){}void f(){take(R{1});}',
        'lvalue-qualified': 'struct R{int get()&{return 1;}};int f(){return R{}.get();}',
        'deleted-move': 'struct R{R(){}R(R&&)=delete;};void f(){R r(static_cast<R&&>(R{}));}',
    }
    for name, source in temporary_call_invalid.items():
        check("v2-" + 'temporary_call_invalid' + "-" + name, source, "TR0202", profile="cpp-core-v2")
    temporary_call_missing = {
        'temporary-receiver': 'struct R{int get()const;};int f(){return R{}.get();}',
        'temporary-argument': 'int take(const int&);int f(){return take(1);}',
    }
    for name, source in temporary_call_missing.items():
        check("v2-" + 'temporary_call_missing' + "-" + name, source, "TR0203", profile="cpp-core-v2")
    check("v1-temporary-scalar-call", "int take(const int&n){return n;}int f(){return take(1);}", "TR0201")
    check("v1-temporary-method-call", "struct R{int get()const{return 1;}};int f(){return R{}.get();}", "TR0201")
    automatic_reference_source = """struct R {
 int n;R*self;
 R(int value):n(value),self(this){}
 R(const R&r):n(r.n),self(this){}
 R(R&&r):n(r.n),self(this){r.n=-1;}
 ~R(){n=0;}
 explicit operator bool()const{return n!=0;}
};
int read(const R&r){return r.n;}
int number(const int&n){return n;}
void mark(){}
int scalar(){const int&r{7};mark();return r;}
int record(){const R&r={R(1)};mark();const R&same{r};return same.n;}
int nested(){const R&r{R(read(R(2)))};mark();return r.n;}
R result(){const R&r=R(3);mark();return r;}
R moved(){R&&r={R(4)};mark();return static_cast<R&&>(r);}
int branch(bool b){const int&r=b?R(5).n:R(6).n;mark();return r;}
int choice(bool b){const R&r=b?R(7):R(8);mark();return r.n;}
void loop(int n){while(const R&r=R(n--)){mark();}}
struct Group{R array[2];};
int subobject(){const int&r={Group{{R(9),R(10)}}.array[1].n};mark();return r;}
int braces(){return number({11})+read({R(12)});}
struct Source{int n;
 operator int()const{return n;}
};
int converted(){Source s{13};const int&r{s};mark();return r;}
"""
    extended = check("v2-automatic-reference-lifetime-protocol", automatic_reference_source, profile="cpp-core-v2")
    ar_functions = {f["name"]: f for f in extended["functions"]}

    def ar_line(prefix):
        matches = [i for i, line in enumerate(automatic_reference_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(matches) == 1, (prefix, matches)
        return matches[0]

    def ar_function(prefix):
        matches = [f for f in extended["functions"] if f["loc"]["line"] == ar_line(prefix)]
        assert len(matches) == 1, (prefix, matches)
        return matches[0]

    ar_records = {r["loc"]["line"]: r["id"] for r in extended["records"]}
    rid = ar_records[ar_line("struct R {")]
    gid = ar_records[ar_line("struct Group{")]
    constructor = ar_function(" R(int value)")["name"]
    copy = ar_function(" R(const R&r)")["name"]
    move = ar_function(" R(R&&r)")["name"]
    mark = ar_function("void mark(")["name"]
    read = ar_function("int read(")["name"]
    boolean = ar_function(" explicit operator bool(")["name"]
    scalar = ar_function("int scalar(")
    assert [c["callee"] for c in gc_calls(scalar)] == [mark], scalar
    refs = [n["target"] for n in scalar["body"] if n["op"] == "assign" and n["target"]["type"] == "cptr:int"]
    assert refs, scalar
    objects = {storage_pointer_object(scalar, ref) for ref in refs}
    assert len(objects) == 1, objects
    obj = next(iter(objects))
    assert obj[0] == "object"
    values = [n["value"] for n in scalar["body"] if n["op"] == "assign" and n["target"].get("name") == obj[1]]
    assert len(values) == 1 and gc_identity(scalar, values[0]) == 7, values
    record = ar_function("int record(")
    calls = gc_calls(record)
    assert [c["callee"] for c in calls] == [constructor, mark, rid+"_destroy"], record
    owned = storage_pointer_object(record, calls[0]["args"][0])
    assert owned == storage_pointer_object(record, calls[-1]["args"][0])
    assert len([v for v in record["locals"] if v["type"] == rid]) == 1, record
    references = [n["target"] for n in record["body"] if n["op"] == "assign" and n["target"]["type"] == "cptr:"+rid]
    assert len(references) == 2 and all(storage_pointer_object(record, r) == owned for r in references), record
    nested = ar_function("int nested(")
    calls = gc_calls(nested)
    assert [c["callee"] for c in calls] == [constructor, read, constructor, rid+"_destroy", mark, rid+"_destroy"], nested
    inner = storage_pointer_object(nested, calls[0]["args"][0])
    outer = storage_pointer_object(nested, calls[2]["args"][0])
    assert inner != outer
    assert [storage_pointer_object(nested, calls[i]["args"][0]) for i in (1, 3, 5)] == [inner, inner, outer]
    for prefix, selected in (("R result(", copy), ("R moved(", move)):
        function = ar_function(prefix)
        calls = gc_calls(function)
        assert [c["callee"] for c in calls] == [constructor, mark, selected, rid+"_destroy"], function
        source = storage_pointer_object(function, calls[0]["args"][0])
        destination = storage_pointer_object(function, calls[2]["args"][0])
        assert destination == ("parameter", function["params"][0]["name"]) and source != destination
        assert storage_pointer_object(function, calls[2]["args"][1]) == source
        assert storage_pointer_object(function, calls[3]["args"][0]) == source
        assert len([v for v in function["locals"] if v["type"] == rid]) == 1
    branch = ar_function("int branch(")
    calls = gc_calls(branch)
    assert [c["callee"] for c in calls] == [constructor, constructor, mark, rid+"_destroy", rid+"_destroy"], branch
    constructed = [storage_pointer_object(branch, c["args"][0]) for c in calls[:2]]
    assert len(set(constructed)) == 2
    assert [storage_pointer_object(branch, c["args"][0]) for c in calls[-2:]] == constructed[::-1]
    choice = ar_function("int choice(")
    calls = gc_calls(choice)
    assert [c["callee"] for c in calls] == [constructor, constructor, mark, rid+"_destroy"], choice
    actual = [storage_pointer_object(choice, calls[i]["args"][0]) for i in (0, 1, 3)]
    assert len(set(actual)) == 1, choice
    loop = ar_function("void loop(")
    calls = gc_calls(loop)
    assert [c["callee"] for c in calls[:3]] == [constructor, boolean, mark], loop
    assert len(calls) == 5 and all(c["callee"] == rid+"_destroy" for c in calls[3:]), loop
    actual = [storage_pointer_object(loop, calls[i]["args"][0]) for i in (0, 1, 3, 4)]
    assert len(set(actual)) == 1, loop
    for function in (record, branch, choice, loop):
        # Each destruction is guarded and clears its flag. Loops have both
        # back-edge and false-condition cleanup for the same possible owner.
        for index, node in enumerate(function["body"]):
            if node["op"] != "call" or node["callee"] != rid+"_destroy":
                continue
            labels = [n["label"] for n in function["body"][:index] if n["op"] == "label"]
            guards = [n for n in function["body"][:index] if n["op"] == "branch" and n["true"] == labels[-1]]
            assert len(guards) == 1 and guards[0]["condition"]["kind"] == "var", function
            flag = guards[0]["condition"]["name"]
            values = [n["value"] for n in function["body"] if n["op"] == "assign" and n["target"].get("name") == flag]
            assert [v["value"] for v in values[:2]] == [False, True] and all(v["value"] is False for v in values[2:]), values
            block_start = max(i for i, n in enumerate(function["body"][:index]) if n["op"] == "label")
            assert any(n["op"] == "assign" and n["target"].get("name") == flag and n["value"].get("value") is False
                       for n in function["body"][block_start+1:index]), function
    subobject = ar_function("int subobject(")
    calls = gc_calls(subobject)
    assert [c["callee"] for c in calls] == [constructor, constructor, mark, gid+"_destroy"], subobject
    groups = [v for v in subobject["locals"] if v["type"] == gid]
    assert len(groups) == 1 and not any(v["type"] == rid for v in subobject["locals"])
    assert storage_pointer_object(subobject, calls[-1]["args"][0]) == ("object", groups[0]["name"])
    assert [c["callee"] for c in gc_calls(ar_functions[gid+"_destroy"])] == [rid+"_destroy", rid+"_destroy"]
    braces = ar_function("int braces(")
    assert [c["callee"] for c in gc_calls(braces)] == [ar_function("int number(")["name"], constructor, read, rid+"_destroy"], braces
    converted = ar_function("int converted(")
    conversion = ar_function(" operator int()const")["name"]
    assert [c["callee"] for c in gc_calls(converted)] == [conversion, mark], converted
    returned = [n["value"] for n in converted["body"] if n["op"] == "return"]
    assert len(returned) == 1 and uc_reference_origin(converted, returned[0]) == conversion, converted
    for function in extended["functions"]:
        for call in gc_calls(function):
            assert [a["type"] for a in call["args"]] == [p["type"] for p in ar_functions[call["callee"]]["params"]], call
    with tempfile.TemporaryDirectory(prefix="neverc-automatic-reference-relocated-") as temp:
        relocated = check("automatic-reference-relocated", automatic_reference_source,
                          root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == extended, "automatic reference identities depend on the absolute root"

    automatic_reference_rejected = {
        'fresh-brace-scalar-return': 'const int&f(){return {1};}',
        'fresh-equal-record-return': 'struct R{int n;};const R&f(){return {R{1}};}',
        'fresh-brace-member-return': 'struct R{int n;};const int&f(){return {R{1}.n};}',
        'static-brace': 'int f(){static const int&r{1};return r;}',
        'tls-brace': 'int f(){thread_local const int&r{1};return r;}',
        'global-brace': 'const int&r{1};',
        'reference-field': 'struct R{const int&r;};int f(){R r{1};return r.r;}',
        'braced-offset': 'struct R{int a[2];};int f(){const int&r{*(R{{1,2}}.a+1)};return r;}',
        'braced-arrow': 'struct I{int n;};struct R{I a[2];};int f(){const int&r{(R{{{1},{2}}}.a+1)->n};return r;}',
        'unused-throw': 'struct R{int n;~R(){throw 1;}};void f(){const R&r{R{1}};}',
        'constexpr-unsupported': 'constexpr int f(){const double&r{1.0};return 1;}constexpr int n=f();',
        'volatile-owner': 'void f(){const volatile int&&r=1;}',
    }
    for name, source in automatic_reference_rejected.items():
        check("v2-" + 'automatic_reference_rejected' + "-" + name, source, "TR0201", profile="cpp-core-v2")
    automatic_reference_invalid = {
        'mutable-scalar': 'void f(){int&r{1};}',
        'mutable-record': 'struct R{int n;};void f(){R&r={R{1}};}',
        'const-mutation': 'void f(){const int&r{1};++r;}',
        'deleted-copy': 'struct R{int n;R(int v):n(v){}R(const R&)=delete;};void f(){const R&r=R(1);R copy(r);}',
        'deleted-move': 'struct R{int n;R(int v):n(v){}R(R&&)=delete;};void f(){R&&r=R(1);R moved(static_cast<R&&>(r));}',
        'narrow-brace': 'void f(){const unsigned char&r{300};}',
    }
    for name, source in automatic_reference_invalid.items():
        check("v2-" + 'automatic_reference_invalid' + "-" + name, source, "TR0202", profile="cpp-core-v2")
    automatic_reference_missing = {
        'constructor': 'struct R{int n;R(int);};void f(){const R&r=R(1);}',
        'destructor': 'struct R{int n;~R();};void f(){const R&r{R{1}};}',
    }
    for name, source in automatic_reference_missing.items():
        check("v2-" + 'automatic_reference_missing' + "-" + name, source, "TR0203", profile="cpp-core-v2")
    check("v1-automatic-scalar-reference", "int f(){const int&r{1};return r;}", "TR0201")
    check("v1-automatic-record-reference", "struct R{int n;};int f(){const R&r={R{1}};return r.n;}", "TR0201")
    array_temporary_source = """struct R {
 int n;R*self;
 R():n(0),self(this){}
 R(int value):n(value),self(this){}
 ~R(){n=0;}
};
using Items=R[2];using Triple=R[3];using Grid=R[2][2];using Numbers=int[3];
int read(const R(&a)[2]){return a[0].n+a[1].n;}
int sum(const int(&a)[3]){return a[0]+a[1]+a[2];}
void mark(){}
int argument(){int n=read(Items{R(1),R(2)});mark();return n;}
int braced(){int n=read({R(3),R(4)});mark();return n;}
int local(){const Items&r={R(5),R(6)};const Items&same{r};mark();return read(same);}
int element(){const R&r=Items{R(7),R(8)}[1];mark();return r.n;}
int row(){const R(&r)[2]=Grid{{R(1),R(2)},{R(3),R(4)}}[1];mark();return read(r);}
void discarded(){Items{R(1),R(2)};mark();}
void defaults(){Triple{R(3)};mark();}
int nested(){const Items&r={R(read(Items{R(1),R(2)})),R(3)};mark();return read(r);}
int branch(bool b){const R&r=b?Items{R(1),R(2)}[0]:Items{R(3),R(4)}[1];mark();return r.n;}
void loop(int n){for(int i=0;i<n;++i){const Items&r={R(1),R(2)};mark();}}
int scalar(){const Numbers&r={1,2,3};mark();return sum(r);}
bool query(){return noexcept(Items{R(1),R(2)});}
"""
    array_temporaries = check("v2-standalone-array-temporaries-protocol", array_temporary_source, profile="cpp-core-v2")
    at_functions = {f["name"]: f for f in array_temporaries["functions"]}

    def at_line(prefix):
        matches = [i for i, line in enumerate(array_temporary_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(matches) == 1, (prefix, matches)
        return matches[0]

    def at_function(prefix):
        matches = [f for f in array_temporaries["functions"] if f["loc"]["line"] == at_line(prefix)]
        assert len(matches) == 1, (prefix, matches)
        return matches[0]

    def at_pointer(function, expr):
        if expr["kind"] == "cast":
            return at_pointer(function, expr["args"][0])
        if expr["kind"] in ("address", "array_decay"):
            return at_place(function, expr["args"][0])
        assert expr["kind"] == "var", expr
        values = [n["value"] for n in function["body"] if n["op"] == "assign" and n["target"].get("name") == expr["name"]]
        if values:
            assert len(values) == 1, values
            return at_pointer(function, values[0])
        assert any(p["name"] == expr["name"] for p in function["params"]), expr
        return ("parameter", expr["name"])

    def at_place(function, expr):
        if expr["kind"] == "var":
            return ("object", expr["name"])
        if expr["kind"] == "dereference":
            return at_pointer(function, expr["args"][0])
        assert expr["kind"] == "index", expr
        return ("element", at_pointer(function, expr["args"][0]), gc_identity(function, expr["args"][1]))

    rid = next(r["id"] for r in array_temporaries["records"] if r["loc"]["line"] == at_line("struct R {"))
    constructor = at_function(" R(int value)")["name"]
    default = at_function(" R():")["name"]
    read = at_function("int read(")["name"]
    mark = at_function("void mark(")["name"]
    destructor = rid+"_destroy"
    assert [p["type"] for p in at_functions[read]["params"]] == ["cptr:arr:2:"+rid]
    for prefix in ("int argument(", "int braced(", "int local(", "int element(", "void discarded("):
        function = at_function(prefix)
        arrays = [v for v in function["locals"] if v["type"] == "arr:2:"+rid]
        assert len(arrays) == 1 and not any(v["type"] == rid for v in function["locals"]), function
        root = ("object", arrays[0]["name"])
        calls = gc_calls(function)
        constructors = [c for c in calls if c["callee"] == constructor]
        destructors = [c for c in calls if c["callee"] == destructor]
        assert [at_pointer(function, c["args"][0]) for c in constructors] == [("element", root, i) for i in (0, 1)]
        assert [at_pointer(function, c["args"][0]) for c in destructors] == [("element", root, i) for i in (1, 0)]
        callees = [c["callee"] for c in calls]
        if prefix in ("int argument(", "int braced("):
            assert callees == [constructor, constructor, read, destructor, destructor, mark], function
            assert at_pointer(function, calls[2]["args"][0]) == root
        elif prefix == "int local(":
            assert callees == [constructor, constructor, mark, read, destructor, destructor], function
            assert at_pointer(function, calls[3]["args"][0]) == root
        elif prefix == "int element(":
            assert callees == [constructor, constructor, mark, destructor, destructor], function
        else:
            assert callees == [constructor, constructor, destructor, destructor, mark], function
        flags = [n["target"]["name"] for n in function["body"] if n["op"] == "assign" and n["value"].get("value") is True]
        assert len(flags) == 1, function
        values = [n["value"]["value"] for n in function["body"] if n["op"] == "assign" and n["target"].get("name") == flags[0]]
        assert values == [False, True, False], function
    row = at_function("int row(")
    arrays = [v for v in row["locals"] if v["type"] == "arr:2:arr:2:"+rid]
    assert len(arrays) == 1 and not any(v["type"] in (rid, "arr:2:"+rid) for v in row["locals"]), row
    root = ("object", arrays[0]["name"])
    cells = [("element", ("element", root, i), j) for i in (0, 1) for j in (0, 1)]
    calls = gc_calls(row)
    assert [c["callee"] for c in calls] == [constructor]*4+[mark, read]+[destructor]*4, row
    assert [at_pointer(row, c["args"][0]) for c in calls[:4]] == cells
    assert at_pointer(row, calls[5]["args"][0]) == ("element", root, 1)
    assert [at_pointer(row, c["args"][0]) for c in calls[-4:]] == cells[::-1]
    defaults = at_function("void defaults(")
    calls = gc_calls(defaults)
    assert [c["callee"] for c in calls] == [constructor, default, default, destructor, destructor, destructor, mark], defaults
    arrays = [v for v in defaults["locals"] if v["type"] == "arr:3:"+rid]
    assert len(arrays) == 1 and not any(v["type"] == rid for v in defaults["locals"])
    root = ("object", arrays[0]["name"])
    assert [at_pointer(defaults, c["args"][0]) for c in calls[:3]] == [("element", root, i) for i in range(3)]
    assert [at_pointer(defaults, c["args"][0]) for c in calls[3:6]] == [("element", root, i) for i in (2, 1, 0)]
    nested = at_function("int nested(")
    calls = gc_calls(nested)
    assert [c["callee"] for c in calls] == [constructor, constructor, read, constructor, constructor, destructor, destructor, mark, read, destructor, destructor], nested
    arrays = [v for v in nested["locals"] if v["type"] == "arr:2:"+rid]
    assert len(arrays) == 2 and not any(v["type"] == rid for v in nested["locals"]), nested
    inner = at_pointer(nested, calls[2]["args"][0])
    outer = at_pointer(nested, calls[8]["args"][0])
    assert inner != outer
    assert [at_pointer(nested, calls[i]["args"][0]) for i in (5, 6, 9, 10)] == [
        ("element", root, index) for root in (inner, outer) for index in (1, 0)]
    branch = at_function("int branch(")
    calls = gc_calls(branch)
    assert [c["callee"] for c in calls] == [constructor]*4+[mark]+[destructor]*4, branch
    constructed = [at_pointer(branch, c["args"][0]) for c in calls[:4]]
    assert len(set(constructed)) == 4
    assert [at_pointer(branch, c["args"][0]) for c in calls[-4:]] == constructed[::-1]
    flags = [n["target"]["name"] for n in branch["body"] if n["op"] == "assign" and n["value"].get("value") is True]
    assert len(set(flags)) == 2, branch
    for flag in flags:
        values = [n["value"]["value"] for n in branch["body"] if n["op"] == "assign" and n["target"].get("name") == flag]
        assert values == [False, True, False], branch
        assert any(n["op"] == "branch" and n["condition"].get("name") == flag for n in branch["body"])
    loop = at_function("void loop(")
    assert [c["callee"] for c in gc_calls(loop)] == [constructor, constructor, mark, destructor, destructor], loop
    assert len([v for v in loop["locals"] if v["type"] == "arr:2:"+rid]) == 1
    scalar = at_function("int scalar(")
    assert [c["callee"] for c in gc_calls(scalar)] == [mark, at_function("int sum(")["name"]], scalar
    arrays = [v for v in scalar["locals"] if v["type"] == "arr:3:int"]
    assert len(arrays) == 1
    assert at_pointer(scalar, gc_calls(scalar)[1]["args"][0]) == ("object", arrays[0]["name"])
    query = at_function("bool query(")
    assert not gc_calls(query) and not any(v["type"].startswith("arr:") for v in query["locals"]), query
    returned = [n["value"] for n in query["body"] if n["op"] == "return"]
    assert len(returned) == 1 and nq_constant(query, returned[0]) is False
    for function in array_temporaries["functions"]:
        for call in gc_calls(function):
            assert [a["type"] for a in call["args"]] == [p["type"] for p in at_functions[call["callee"]]["params"]], call
    with tempfile.TemporaryDirectory(prefix="neverc-array-temporaries-relocated-") as temp:
        relocated = check("array-temporaries-relocated", array_temporary_source,
                          root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == array_temporaries, "array temporary identities depend on the absolute root"

    array_temporary_rejected = {
        'static-array': 'int f(){static const int(&r)[2]={1,2};return r[0];}',
        'global-array': 'const int(&r)[2]={1,2};',
        'tls-array': 'int f(){thread_local const int(&r)[2]={1,2};return r[0];}',
        'reference-field': 'struct R{const int(&a)[2];};void f(){R r{{1,2}};}',
        'fresh-array-return': 'using A=int[2];const A&f(){return A{1,2};}',
        'fresh-element-return': 'using A=int[2];const int&f(){return A{1,2}[0];}',
        'pointer-offset-reference': 'using A=int[2];int f(){const int&r=*(A{1,2}+1);return r;}',
        'dereference-reference': 'using A=int[2];int f(){const int&r=*A{1,2};return r;}',
        'float-element': 'void f(){const double(&r)[2]={1.0,2.0};}',
        'volatile-element': 'void f(){const volatile int(&&r)[2]={1,2};}',
        'vla': 'void f(int n){int a[n];}',
        'unknown-bound': 'extern int a[];',
        'zero-bound': 'using A=int[0];void f(){const A&r={};}',
        'extent-limit': 'using A=int[65537];void f(){const A&r={};}',
        'storage-limit': 'using A=int[512][512];void f(){const A&r={};}',
        'expanded-storage-budget': 'using A=int[32768];void f(){const A&r={};}',
        'query-unsupported': 'using A=double[2];bool f(){return noexcept(A{1.0,2.0}[0]);}',
    }
    for name, source in array_temporary_rejected.items():
        check("v2-" + 'array_temporary_rejected' + "-" + name, source, "TR0201", profile="cpp-core-v2")
    array_temporary_invalid = {
        'mutable-array-reference': 'using A=int[2];void f(){A&r={1,2};}',
        'const-array-write': 'using A=int[2];void f(){const A&r={1,2};r[0]=3;}',
        'array-assignment': 'using A=int[2];void f(){A&&a={1,2};A&&b={3,4};a=b;}',
        'narrow-element': 'void f(){const unsigned char(&r)[2]={1,300};}',
        'too-many': 'void f(){const int(&r)[2]={1,2,3};}',
        'deleted-move': 'struct R{int n;R(int v):n(v){}R(R&&)=delete;};using A=R[2];void f(){R r(1);const A&a={static_cast<R&&>(r),R(2)};}',
    }
    for name, source in array_temporary_invalid.items():
        check("v2-" + 'array_temporary_invalid' + "-" + name, source, "TR0202", profile="cpp-core-v2")
    array_temporary_missing = {
        'constructor': 'struct R{int n;R(int);};using A=R[2];void f(){const A&r={R(1),R(2)};}',
        'destructor': 'struct R{int n;~R();};using A=R[2];void f(){const A&r={{1},{2}};}',
    }
    for name, source in array_temporary_missing.items():
        check("v2-" + 'array_temporary_missing' + "-" + name, source, "TR0203", profile="cpp-core-v2")
    check("v1-array-temporary-reference", "using A=int[2];int f(){const A&r={1,2};return r[0];}", "TR0201")
    check("v1-array-temporary-decay", "using A=int[2];int f(){return A{1,2}[0];}", "TR0201")
    defaulted_source = """struct Leaf {
  int value; Leaf *self;
  Leaf():value(7),self(this){}
  ~Leaf(){value=0;}
};
struct Implicit { int plain; Leaf leaf; };
struct Explicit { int plain; Leaf leaf; explicit Explicit()=default; ~Explicit()=default; };
struct Outside { int plain; Leaf leaf; Outside(); ~Outside(); };
Outside::Outside()=default;
Outside::~Outside()=default;
struct Trivial { int value; explicit Trivial()=default; ~Trivial()=default; };
struct Lazy { Leaf leaf; explicit Lazy()=default; };
void defaultInit(){Implicit i;Explicit e;Outside o;Trivial t;}
void valueInit(){Implicit i=Implicit();Explicit e=Explicit();Outside o=Outside();Trivial t=Trivial();}
void again(){Implicit first;Implicit second;}
int query(){return sizeof(Lazy{});}
"""
    defaulted = check("v2-defaulted-lifecycle-protocol", defaulted_source, profile="cpp-core-v2")
    defaulted_records = {r["loc"]["line"]: r for r in defaulted["records"]}
    defaulted_functions = {f["name"]: f for f in defaulted["functions"]}
    defaulted_by_line = {f["loc"]["line"]: f for f in defaulted["functions"]}
    constructors = {}
    for line in (1, 6, 7, 8, 11, 12):
        rid = defaulted_records[line]["id"]
        locations = (3,) if line == 1 else ((8, 9) if line == 8 else (line,))
        selected = [f for f in defaulted["functions"] if f["result"] == "void"
                    and f["loc"]["line"] in locations
                    and [p["type"] for p in f["params"]] == ["ptr:" + rid]
                    and f["name"] != rid + "_destroy"]
        assert len(selected) == (1 if line in (1, 6, 7, 8) else 0), (line, selected)
        if selected:
            constructors[line] = selected[0]
    for line in (6, 7, 8):
        function = constructors[line]
        calls = [n for n in function["body"] if n["op"] == "call"]
        assert len(calls) == 1 and calls[0]["callee"] == constructors[1]["name"], function
        assert not any(n["op"] == "assign" and n["target"]["kind"] == "member"
                       for n in function["body"]), "default construction invented scalar initialization"
        rid = defaulted_records[line]["id"]
        cleanup = defaulted_functions[rid + "_destroy"]
        cleanups = [n for n in cleanup["body"] if n["op"] == "call"]
        assert len(cleanups) == 1 and cleanups[0]["callee"] == defaulted_records[1]["id"] + "_destroy", cleanup
    default_body, value_body = defaulted_by_line[13], defaulted_by_line[14]
    for function in (default_body, value_body):
        calls = [n for n in function["body"] if n["op"] == "call"
                 and n["callee"] in {f["name"] for f in constructors.values()}]
        assert [n["callee"] for n in calls] == [constructors[n]["name"] for n in (6, 7, 8)], calls
        for call in calls:
            record_type = call["args"][0]["type"].removeprefix("ptr:")
            local = next(v for v in function["locals"] if v["type"] == record_type)
            assert storage_pointer_object(function, call["args"][0]) == ("object", local["name"])
    defaulted_record_ids = {r["id"] for r in defaulted["records"]}
    zeroed_types = [n["target"]["type"] for n in value_body["body"] if n["op"] == "assign"
                    and n["target"]["type"] in defaulted_record_ids]
    assert zeroed_types == [defaulted_records[n]["id"] for n in (6, 7, 11)], zeroed_types
    assert not any(n["op"] == "assign" and n["target"]["type"] in defaulted_record_ids
                   for n in default_body["body"]), default_body
    repeated_calls = [n for n in defaulted_by_line[15]["body"] if n["op"] == "call"
                      and n["callee"] == constructors[6]["name"]]
    assert len(repeated_calls) == 2, repeated_calls
    assert not any(n["op"] == "call" for n in defaulted_by_line[16]["body"]), defaulted_by_line[16]
    for function in defaulted["functions"]:
        for node in function["body"]:
            if node["op"] == "call":
                callee = defaulted_functions[node["callee"]]
                assert [a["type"] for a in node["args"]] == [p["type"] for p in callee["params"]], node
    with tempfile.TemporaryDirectory(prefix="neverc-defaulted-lifecycle-relocated-") as temp:
        relocated = check("defaulted-lifecycle-relocated", defaulted_source,
                          root=Path(temp) / "project", profile="cpp-core-v2")
        assert relocated == defaulted, "defaulted lifecycle identities depend on the absolute root"
    defaulted_rejected = {
        'deleted-constructor': 'struct R{int n;R()=delete;};',
        'deleted-destructor': 'struct R{int n;~R()=delete;};',
        'defaulted-deleted-constructor': 'struct I{int n;I()=delete;};struct R{I i;R()=default;};',
        'defaulted-deleted-destructor': 'struct I{int n;~I()=delete;};struct R{I i;~R()=default;};',
        'nonpublic-field': 'class R{int n;public:R()=default;};',
        'virtual-destructor': 'struct R{int n;virtual ~R()=default;};',
        'explicit-destruction': 'struct R{int n;~R()=default;};void f(){R r{1};r.~R();}',
        'throwing-member-constructor': 'struct I{int n;I(){throw 1;}};struct R{I i;R()=default;};',
    }
    for name, source in defaulted_rejected.items():
        check("v2-defaulted-reject-" + name, source, "TR0201", profile="cpp-core-v2")
    for name, source in {
        "constructor": "struct I{int n;I();};struct R{I i;R()=default;};void f(){R r;}",
        "destructor": "struct I{int n;~I();};struct R{I i;~R()=default;};void f(){R r{{1}};}",
    }.items():
        check("v2-defaulted-missing-" + name, source, "TR0203", profile="cpp-core-v2")
    check("v1-defaulted-constructor", "struct R{int n;explicit R()=default;};", "TR0201")
    check("v1-defaulted-destructor", "struct R{int n;~R()=default;};", "TR0201")
    destruction_source = """struct R {
  int *value; int tag;
  R(int *v,int n):value(v),tag(n){}
  ~R(){*value=*value*10+tag;}
};
struct Box { R first; R second[2]; };
R make(int *value){return R(value,1);}
int consume(R value){return value.tag;}
int capture(int *value){R local(value,2);return *value;}
void aggregate(int *value){Box box{R(value,1),{R(value,2),R(value,3)}};}
void flow(int *value,bool b){if(b)R local(value,4);while(b){R local(value,5);break;}}
int main(){int value=0;R result=make(&value);return consume(R(&value,6));}
"""
    destruction = check("v2-destruction-protocol", destruction_source, profile="cpp-core-v2")
    destruction_records = {r["loc"]["line"]: r for r in destruction["records"]}
    destruction_functions = {f["name"]: f for f in destruction["functions"]}
    destruction_by_line = {f["loc"]["line"]: f for f in destruction["functions"]}
    record, box = destruction_records[1], destruction_records[6]
    destructors = {r["id"]: destruction_functions[r["id"] + "_destroy"]
                   for r in (record, box)}
    for rid, function in destructors.items():
        assert function["result"] == "void" and function["internal"] and not function["c_export"], function
        assert [p["type"] for p in function["params"]] == ["ptr:" + rid], function
    for function in destruction["functions"]:
        for instruction in function["body"]:
            if instruction["op"] == "call":
                callee = destruction_functions[instruction["callee"]]
                assert [a["type"] for a in instruction["args"]] == [p["type"] for p in callee["params"]], instruction
                if callee["result"] == "void":
                    assert "target" not in instruction, instruction
    destructor_name = destructors[record["id"]]["name"]

    def destruction_calls(function, name=destructor_name):
        return [n for n in function["body"] if n["op"] == "call" and n["callee"] == name]

    # Function result storage belongs to its caller; parameter storage belongs
    # to the callee. Neither path may manufacture another owned copy.
    assert not destruction_calls(destruction_by_line[7])
    consume = destruction_by_line[8]
    consumed = destruction_calls(consume)
    assert len(consumed) == 1, consumed
    assert storage_pointer_object(consume, consumed[0]["args"][0]) == (
        "parameter", consume["params"][0]["name"])
    main_destruct = destruction_functions["main"]
    caller_cleanup = destruction_calls(main_destruct)
    assert len(caller_cleanup) == 1, caller_cleanup
    make_call = next(n for n in main_destruct["body"] if n["op"] == "call"
                     and n["callee"] == destruction_by_line[7]["name"])
    parameter_call = next(n for n in main_destruct["body"] if n["op"] == "call"
                          and n["callee"] == consume["name"])
    result_object = storage_pointer_object(main_destruct, make_call["args"][0])
    assert result_object == storage_pointer_object(main_destruct, caller_cleanup[0]["args"][0])
    assert result_object != storage_pointer_object(main_destruct, parameter_call["args"][0])
    capture = destruction_by_line[9]
    returned = next(n["value"] for n in capture["body"] if n["op"] == "return")
    assert returned["kind"] == "var", returned
    captured_at = [i for i, n in enumerate(capture["body"]) if n["op"] == "assign"
                   and n["target"].get("kind") == "var" and n["target"]["name"] == returned["name"]]
    cleanup_at = [i for i, n in enumerate(capture["body"]) if n["op"] == "call" and n["callee"] == destructor_name]
    assert len(captured_at) == len(cleanup_at) == 1 and captured_at[0] < cleanup_at[0], capture

    # Implicit member destruction is derived from the record, including reverse
    # array indices. It cannot depend on a lazily emitted Clang destructor body.
    box_destructor = destructors[box["id"]]
    members = destruction_calls(box_destructor)
    assert len(members) == 3, members

    def destruction_place(function, pointer):
        if pointer["kind"] == "var":
            assignments = [n["value"] for n in function["body"] if n["op"] == "assign"
                           and n["target"].get("kind") == "var" and n["target"]["name"] == pointer["name"]]
            assert len(assignments) == 1, pointer
            return destruction_place(function, assignments[0])
        assert pointer["kind"] == "address", pointer
        return pointer["args"][0]

    for invocation, expected_index in zip(members[:2], (1, 0)):
        place = destruction_place(box_destructor, invocation["args"][0])
        assert place["kind"] == "index" and place["args"][1]["value"] == expected_index, place
        array = place["args"][0]
        assert array["kind"] == "array_decay" and array["args"][0]["name"] == box["fields"][1]["name"], array
    last_member = destruction_place(box_destructor, members[2]["args"][0])
    assert last_member["kind"] == "member" and last_member["name"] == box["fields"][0]["name"], last_member
    aggregate = destruction_by_line[10]
    assert len(destruction_calls(aggregate, box_destructor["name"])) == 1
    assert not destruction_calls(aggregate), aggregate
    assert len(destruction_calls(destruction_by_line[11])) == 2
    with tempfile.TemporaryDirectory(prefix="neverc-destruction-relocated-") as temp:
        relocated = check("destruction-relocated", destruction_source,
                          root=Path(temp) / "project", profile="cpp-core-v2")
        assert relocated == destruction, "destructor identities depend on the absolute root"

    destruction_rejected = {
        'explicit-deleted': 'struct R{int n;~R()=delete;};',
        'virtual': 'struct R{int n;virtual ~R(){}};',
        'explicit-call': 'struct R{int n;~R(){}};void f(R&r){r.~R();}',
        'explicit-dead-call': 'struct R{int n;~R(){}};void f(R&r){if(false)r.~R();}',
        'explicit-alias-call': 'struct R{int n;~R(){}};using T=R;void f(R&r){r.~T();}',
        'global': 'struct R{int n;~R(){}};const R r{1};',
        'global-containing': 'struct R{int n;~R(){}};struct Box{R r;};const Box box{{1}};',
        'static-local': 'struct R{int n;~R(){}};int f(){static R r{1};return r.n;}',
        'thread-local': 'struct R{int n;~R(){}};int f(){thread_local R r{1};return r.n;}',
        'allocation': 'struct R{int n;~R(){}};R*f(){return new R{1};}',
        'delete': 'struct R{int n;~R(){}};void f(R*p){delete p;}',
        'unwinding': 'struct R{int n;~R(){}};void f(){R r{1};throw 7;}',
        'body-throw': 'struct R{int n;~R(){throw 7;}};',
        'body-try': 'struct R{int n;~R(){try{n=1;}catch(...){n=2;}}};',
        'cleanup-expansion': 'struct R{int n;~R(){}};void f(){R r[65536];}',
    }
    for name, source in destruction_rejected.items():
        check("v2-destruction-reject-" + name, source, "TR0201", profile="cpp-core-v2")
    check("v2-destructor-definition", "struct R{int n;~R();};void f(){R r{1};}",
          "TR0203", profile="cpp-core-v2")
    check("v1-destructor-still-rejected", "struct R{int n;~R(){}};void f(){R r{1};}", "TR0201")
    constructor_source = """struct R {
  int first, second;
  R *self;
  R():second(first+2),first(3),self(this){}
  explicit R(int n):first(n),second(first+2),self(this){}
};
struct O { R value; O(){} };
int main(){
  R local(5);
  const R constant(7);
  R array[3]={R(11)};
  R plain[2];
  R conditional=local.first?R(13):R(17);
  R comma=(local.first=19,R(23));
  O outer;
  return 0;
}
"""
    constructors = check("v2-constructor-destinations", constructor_source, profile="cpp-core-v2")
    records_by_line = {r["loc"]["line"]: r for r in constructors["records"]}
    inner_record, outer_record = records_by_line[1], records_by_line[7]
    inner_id, outer_id = inner_record["id"], outer_record["id"]
    constructors_by_line = {f["loc"]["line"]: f for f in constructors["functions"]}
    constructor_signatures = {4: ["ptr:" + inner_id],
                              5: ["ptr:" + inner_id, "int"],
                              7: ["ptr:" + outer_id]}
    for line, parameter_types in constructor_signatures.items():
        function = constructors_by_line[line]
        assert function["result"] == "void" and not function["c_export"], function
        assert [p["type"] for p in function["params"]] == parameter_types, function
    for line in (4, 5):
        writes = [n["target"]["name"] for n in constructors_by_line[line]["body"]
                  if n.get("op") == "assign" and n["target"].get("kind") == "member"]
        assert writes == [f["name"] for f in inner_record["fields"]], writes
    default_constructor = constructors_by_line[4]["name"]
    explicit_constructor = constructors_by_line[5]["name"]
    outer_constructor = constructors_by_line[7]["name"]
    outer_calls = [n for n in constructors_by_line[7]["body"] if n.get("op") == "call"]
    assert len(outer_calls) == 1 and outer_calls[0]["callee"] == default_constructor, outer_calls
    main_function = next(f for f in constructors["functions"] if f["name"] == "main")
    main_calls = [n for n in main_function["body"] if n.get("op") == "call"]
    assert sum(n["callee"] == default_constructor for n in main_calls) == 4, main_calls
    assert sum(n["callee"] == explicit_constructor for n in main_calls) == 6, main_calls
    assert sum(n["callee"] == outer_constructor for n in main_calls) == 1, main_calls
    local_types = [v["type"] for v in main_function["locals"]]
    assert local_types.count(inner_id) == 4 and local_types.count(outer_id) == 1, local_types
    assert local_types.count("arr:3:" + inner_id) == 1, local_types
    assert local_types.count("arr:2:" + inner_id) == 1, local_types
    constructor_functions = {f["name"]: f for f in constructors["functions"]}
    for node in walk(constructors["functions"]):
        if node.get("op") == "call":
            callee = constructor_functions[node["callee"]]
            assert [a["type"] for a in node["args"]] == [p["type"] for p in callee["params"]], node
        if node.get("op") == "assign":
            assert node["value"]["type"] not in (inner_id, outer_id), "direct construction inserted a record copy"
    with tempfile.TemporaryDirectory(prefix="neverc-constructors-relocated-") as temp:
        relocated = check("constructors-relocated", constructor_source,
                          root=Path(temp) / "project", profile="cpp-core-v2")
        assert relocated == constructors, "constructor identities depend on the absolute root"
    materialized = check("v2-constructor-materialization", """struct R {
  int a[2];
  explicit R(R*& out):a{3,5}{out=this;}
};
int main(){
  R *p=nullptr; int *q=nullptr;
  bool first=(q=R(p).a,q==p->a);
  bool second=(q=R(p).a,q==p->a);
  return first&&second?0:1;
}
""", profile="cpp-core-v2")
    materialized_id = materialized["records"][0]["id"]
    materialized_main = next(f for f in materialized["functions"] if f["name"] == "main")
    materialized_objects = [v["name"] for v in materialized_main["locals"] if v["type"] == materialized_id]
    assert len(set(materialized_objects)) == 2, materialized_objects
    for node in walk(materialized_main):
        if node.get("op") == "assign":
            assert node["value"]["type"] != materialized_id, "materialization inserted a second record copy"
    check("v2-constructor-definition", "struct R{int n;R(int);};int f(){R r(1);return r.n;}",
          "TR0203", profile="cpp-core-v2")
    method_source = """struct R {
  int value, spare;
  int get() const { return value; }
  R& add(int n) { value+=n; return *this; }
  R* self() { return this; }
  int& field() { return value; }
  int read() const & { return get(); }
  static int plus(int a,int b) { return a+b; }
  int choose(signed char) const { return 3; }
  int choose(short) const { return 5; }
};
int main() {
  R r; r.value=7;
  const R& c=r;
  r.add(2).field()=11;
  return c.read()==11 && r.self()==&r && R::plus(2,3)==5 &&
    ((r.plus))(3,4)==7 && c.choose(static_cast<signed char>(1))==3 &&
    c.choose(static_cast<short>(1))==5 ? 0:1;
}
"""
    methods = check("v2-method-signatures", method_source, profile="cpp-core-v2")
    record_id = methods["records"][0]["id"]
    method_signatures = {
        3: ("int", ["cptr:" + record_id]),
        4: ("ptr:" + record_id, ["ptr:" + record_id, "int"]),
        5: ("ptr:" + record_id, ["ptr:" + record_id]),
        6: ("ptr:int", ["ptr:" + record_id]),
        7: ("int", ["cptr:" + record_id]),
        8: ("int", ["int", "int"]),
        9: ("int", ["cptr:" + record_id, "i8"]),
        10: ("int", ["cptr:" + record_id, "i16"]),
    }
    method_functions = {f["loc"]["line"]: f for f in methods["functions"]
                        if f["loc"]["line"] in method_signatures}
    assert set(method_functions) == set(method_signatures), methods
    assert len({f["name"] for f in method_functions.values()}) == len(method_signatures)
    for line, (result_type, parameter_types) in method_signatures.items():
        function = method_functions[line]
        assert function["result"] == result_type, function
        assert [p["type"] for p in function["params"]] == parameter_types, function
        assert not function["c_export"], function
    functions_by_name = {f["name"]: f for f in methods["functions"]}
    called_methods = set()
    for node in walk(methods["functions"]):
        if node.get("op") == "call":
            callee = functions_by_name[node["callee"]]
            assert [a["type"] for a in node["args"]] == [p["type"] for p in callee["params"]], node
            called_methods.add(callee["name"])
        if node.get("op") == "assign":
            assert node["value"]["type"] != record_id, "receiver copied as a whole record"
    assert called_methods == {f["name"] for f in method_functions.values()}, called_methods
    with tempfile.TemporaryDirectory(prefix="neverc-methods-relocated-") as temp:
        relocated = check("methods-relocated", method_source, root=Path(temp) / "project",
                          profile="cpp-core-v2")
        assert relocated == methods, "method identities depend on the absolute root"
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
        'record-parameter-expansion': 'struct R{int n[65536];};int ignore(R a,R b,R c,R d){return 0;}int f(){R r;for(int i=0;i<65536;++i)r.n[i]=0;return ignore(r,r,r,r);}',
        'record-result-fallthrough': 'struct R{int n;};R f(bool b){if(b)return {1};}',
        'record-result-reference-binding': 'struct R{int n;};R f(){return {1};}int main(){const R&r=f();return r.n;}',
        'record-result-method-receiver': 'struct R{int n;int get(){return n;}};R f(){return {1};}int main(){return f().get();}',
        'record-result-c-export': 'struct R{int n;};extern "C" R exported(){return {1};}',
        'record-parameter-c-export': 'struct R{int n;};extern "C" int exported(R r){return r.n;}',
        "untyped-assembly-string": 'asm(""); int main(){}',
        "unsupported-pointer-alias": "using Hidden=float*; int main(){}",
        "unused-volatile-alias": "using Hidden=volatile int; int main(){}",
        "unused-function-alias": "using Hidden=void(); int main(){}",
        "alias-template": "template<class T> using Hidden=T; int main(){}",
        "folded-enum-cast": "enum E:int{v=(static_cast<void>(0),1)}; int main(){}",
        "folded-assert-cast": "static_assert((static_cast<void>(0),true),\"condition\"); int main(){}",
        "folded-assert-type": "static_assert(1.0==1.0,\"condition\"); int main(){}",
        "runtime-string": "static_assert(true,\"message\"); const char *s=\"runtime\"; int main(){}",
    }
    v2_rejections.update({
        "reference-field": "struct R{int&r;};",
        "pointer-global": "int*const p=nullptr;",
        "reference-global": "const int x=1; const int&r=x;",
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
        "array-temporary-dereference": "struct R{int a[2];}; int f(){const int&r=*R{{1,2}}.a; return r;}",
    })
    v2_rejections.update({
        "switch-case-range": "int f(int n){switch(n){case 1 ... 3:return 7;default:return 9;}}",
        "switch-dead-range": "int f(int n){if(false){switch(n){case 1 ... 3:return 7;}}return 0;}",
        "switch-other-attribute": "int f(int n){switch(n){case 0:[[likely]];case 1:return 7;default:return 9;}}",
        "switch-folded-cast": "int f(int n){switch(n){case (void(0),1):return 7;default:return 9;}}",
    })
    v2_rejections.update({
        "void-size": "static_assert(sizeof(void)>0); int main(){}",
        "function-size": "int f(){return 0;} int main(){return sizeof(f);}",
        "preferred-alignment": "int main(){int n=0; return __alignof__(n);}",
        "expression-alignment": "int main(){int n=0; return alignof(n);}",
        "floating-size-type": "int main(){return sizeof(double);}",
        "floating-size-value": "int main(){return sizeof(1.0);}",
        "erased-size-operation": "int main(){return sizeof((void(0),1));}",
    })
    v2_rejections.update({
        "pointer-order-less": "bool f(int*a,int*b){return a<b;}",
        "pointer-order-less-equal": "bool f(int*a,int*b){return a<=b;}",
        "pointer-order-greater": "bool f(int*a,int*b){return a>b;}",
        "pointer-order-greater-equal": "bool f(int*a,int*b){return a>=b;}",
        "dead-pointer-order": "int f(int*p){if(false){bool b=p<p;}return 0;}",
        "pointer-unary-plus": "int*f(int*p){return +p;}",
    })
    v2_rejections.update({
        "temporary-offset-reference": "struct R{int a[2];};int f(){const int&r=*(R{{1,2}}.a+0);return r;}",
        "temporary-reverse-offset-reference": "struct R{int a[2];};int f(){const int&r=(0+R{{1,2}}.a)[0];return r;}",
        "temporary-subtract-reference": "struct R{int a[2];};int f(){const int&r=*(R{{1,2}}.a-0);return r;}",
        "temporary-arrow-reference": "struct E{int n;};struct R{E a[1];};int f(){const int&r=R{{{1}}}.a->n;return r;}",
        "temporary-offset-arrow-reference": "struct E{int n;};struct R{E a[1];};int f(){const int&r=(R{{{1}}}.a+0)->n;return r;}",
        "dead-temporary-offset-reference": "struct R{int a[2];};int f(){if(false){const int&r=*(R{{1,2}}.a+0);}return 0;}",
    })
    v2_rejections.update({
        'method-folded-static-function-value': 'struct R{int n;static int get(){return 1;}};static_assert((R::get,true));',
        'method-folded-parenthesized-static-value': 'struct R{int n;static int get(){return 1;}};static_assert(((R::get),true));',
        'method-method-pointer': 'struct R{int n;int get(){return n;}};auto f(){return &R::get;}',
        'method-static-function-pointer': 'struct R{int n;static int get(){return 1;}};int f(){auto p=&R::get;return p();}',
        'method-virtual-method': 'struct R{int n;virtual int get(){return n;}};',
        'method-base-class': 'struct B{int n;};struct R:B{int get(){return n;}};',
        'method-volatile-method': 'struct R{int n;int get()volatile{return n;}};',
        'method-mutable-field': 'struct R{mutable int n;int get()const{return n;}};',
        'method-reference-field': 'struct R{int&n;int get()const{return n;}};',
        'method-member-template': 'struct R{int n;template<class T>T get(T v){return v;}};',
        'method-static-data': 'struct R{int n;static int value;int get(){return value;}};int R::value=1;',
        'method-default-argument': 'struct R{int n;int get(int v=1){return n+v;}};int f(){R r{1};return r.get();}',
        'method-constant-static-data': 'struct R{int n;static const int value=1;int get(){return value;}};',
        'method-method-comma-callee': 'struct R{int n;static int get(){return 1;}};int f(){return (0,R::get)();}',
    })
    v2_rejections.update({
        'constructor-delegating': 'struct R{int n;R():R(1){} R(int v):n(v){}};',
        'constructor-base-initializer': 'struct B{int n;B(int v):n(v){}};struct R:B{R():B(1){}};',
        'constructor-inherited-constructor': 'struct B{int n;B(int v):n(v){}};struct R:B{using B::B;};',
        'constructor-virtual-method': 'struct R{int n;R():n(1){} virtual int get(){return n;}};',
        'constructor-template-constructor': 'struct R{int n;template<class T> R(T v):n(v){}};',
        'constructor-variadic-constructor': 'struct R{int n;R(int v,...):n(v){}};',
        'constructor-deleted-constructor': 'struct R{int n;R()=delete;};',
        'constructor-default-argument': 'struct R{int n;R(int v=1):n(v){}};',
        'constructor-private-field': 'class R{int n;public:R():n(1){}};',
        'constructor-protected-field': 'struct R{protected:int n;public:R():n(1){}};',
        'constructor-const-field': 'struct R{const int n;R():n(1){}};',
        'constructor-reference-field': 'struct R{int &n;R(int &v):n(v){}};',
        'constructor-mutable-field': 'struct R{mutable int n;R():n(1){}};',
        'constructor-nested-record': 'struct R{struct I{int n;};I i;R():i{1}{}};',
        'constructor-union': 'union R{int n;unsigned u;R():n(1){}};',
        'constructor-bitfield': 'struct R{unsigned n:3;R():n(1){}};',
        'constructor-static-data': 'struct R{int n;static int value;R():n(1){}};int R::value=1;',
        'constructor-folded-unsupported-initializer': 'struct R{int n;constexpr R():n(sizeof(float)){}};constexpr R r;',
        'constructor-folded-throw-body': 'struct R{int n;constexpr R(int v):n(v){if(v)throw 1;}};constexpr R r(0);',
        'constructor-dynamic-global': 'struct R{int n;R():n(1){}};R global;',
        'constructor-global-array': 'struct R{int n;constexpr R(int v):n(v){}};constexpr R global[1]={{1}};',
        'constructor-global-pointer': 'struct R{int n;constexpr R(int v):n(v){}};constexpr R global(1);constexpr const R *pointer=&global;',
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
        "method": 'struct R{int n;int get()const{return n;}};int main(){R r{1};return r.get()-1;}',
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

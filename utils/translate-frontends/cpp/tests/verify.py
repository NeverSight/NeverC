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
        'record-result-method-receiver': 'struct R{int n;int get(){return n;}};R f(){return {1};}int main(){return f().get();}',
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
        'record-result-reference-binding': 'struct R{int n;};R f(){return {1};}int main(){const R&r=f();return r.n;}',
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
        'automatic-reference-brace-record-conversion': 'struct V{int n;};struct R{int n;operator V()const{return V{n};}};int f(){R r{6};const V&v={static_cast<V>(r)};return v.n;}',
        'automatic-reference-unbraced-record-conversion': 'struct V{int n;};struct R{int n;operator V()const{return V{n};}};int f(){R r{6};const V&v=r;return v.n;}',
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
    core_v2.update({
        'empty-record-aggregate': 'struct E{};int f(){E a,b{},c=a;return sizeof(E)==1&&&a!=&b&&&a!=&c?0:1;}',
        'empty-record-class': 'class E{};int f(){E e;return sizeof(e);}',
        'empty-record-defaulted': 'struct E{E()=default;E(const E&)=default;E&operator=(const E&)=default;~E()=default;};void f(){E a;E b=a;a=b;}',
        'empty-record-move': 'struct E{E()=default;E(E&&)=default;E&operator=(E&&)=default;};void f(){E a;E b=static_cast<E&&>(a);a=static_cast<E&&>(b);}',
        'empty-record-user-special-members': 'struct E{E(){}E(const E&){}E(E&&){}E&operator=(const E&){return *this;}~E(){}};void f(){E a;E b=a;E c=static_cast<E&&>(a);a=b;}',
        'empty-record-out-of-line': 'struct E{E();~E();};E::E(){}E::~E(){}void f(){E e;}',
        'empty-record-constexpr': 'struct E{};constexpr E e{};static_assert(sizeof(e)==1);int f(){return alignof(E);}',
        'empty-record-callable': 'struct F{constexpr int operator()(int n)const noexcept{return n+1;}};constexpr F f{};static_assert(f(1)==2);int g(){return F{}(3);}',
        'empty-record-enum-conversion': 'enum class C:unsigned char{one=1};struct E{operator C()const{return C::one;}};int f(){E e;C c=e;return static_cast<int>(c);}',
        'empty-record-empty-conversion-record': 'struct R{operator int()const{return 1;}};int f(){R r;const int&n=r;return n;}',
        'empty-record-empty-record-conversion': 'struct T{int n;};struct R{operator T()const{return {1};}};int f(){R r;const T&t=r;return t.n;}',
        'empty-record-empty-result': 'struct E{};struct F{operator E()const{return {};}};E f(){F f;return f;}',
        'empty-record-reference-result': 'int n=0;struct F{operator int&()const{return n;}};void f(){F f;int&r=f;r=1;}',
        'empty-record-by-value': 'struct E{};bool f(E a,E b){return &a!=&b;}bool g(){E e;return f(e,e);}',
        'empty-record-return': 'struct E{E(){}~E(){}};E f(){return E();}void g(){E e=f();}',
        'empty-record-full-expression': 'struct E{E(){}~E(){}int f()const{return 1;}};int f(){return E{}.f();}',
        'empty-record-local-reference': 'struct E{E(){}~E(){}};void f(){const E&e=E();E&&r=E();}',
        'empty-record-array': 'struct E{E(){}~E(){}};using A=E[2];void f(){const A&a={E(),E()};}',
        'empty-record-array-element': 'struct E{E(){}~E(){}};using A=E[2];void f(){const E&e=A{E(),E()}[1];}',
        'empty-record-nested-members': 'struct E{};struct R{E first,second;int n;E a[2];};int f(){R r{{},{},1,{{},{}}};R s=r;return s.n;}',
        'empty-record-generated-array-assignment': 'struct E{};struct L{int n;L&operator=(const L&r){n=r.n;return *this;}};struct R{E a[2];L l;};void f(R&a,const R&b){a=b;}',
        'empty-record-generated-array-copy': 'struct E{};struct L{int n;L(const L&r):n(r.n){}};struct R{E a[2];L l;};R f(const R&r){return r;}',
        'empty-record-constructor-receiver': 'int bad=0;struct E{E(E*p){if(this!=p)++bad;}};int f(){E e(&e);return bad;}',
        'empty-record-query': 'struct E{E()noexcept{}~E()noexcept{}};static_assert(noexcept(E()));static_assert(sizeof(E{})==1);',
    })
    core_v2.update({
        'void-expression-cast-forms': 'int n=0;int g(){return ++n;}void f(){(void)g();static_cast<void>(g());(void(g()));}',
        'void-expression-no-value': 'void f(){void();void{};}',
        'void-expression-alias': 'using V=void;void f(){V();V{};(V(1));}',
        'void-expression-const-void': 'using V=const void;void f(){V();V{};static_cast<V>(1);}',
        'void-expression-null-literal': 'void f(){static_cast<void>(nullptr);(void(nullptr));nullptr;}',
        'void-expression-uninitialized': 'struct R{int n;};void f(){int n;R r;int a[2];static_cast<void>(n);static_cast<void>(r);static_cast<void>(r.n);static_cast<void>(a);}',
        'void-expression-dereference': 'void f(int*p){static_cast<void>(*p++);}',
        'void-expression-array-index': 'int n=0;int index(){return ++n;}void f(){int a[2];static_cast<void>(a[index()]);}',
        'void-expression-function-designator': 'int g(){return 1;}void f(){static_cast<void>(g);}',
        'void-expression-void-return': 'void g(){}void f(){return static_cast<void>(g());}',
        'void-expression-void-initialized-return': 'using V=void;void f(){return V{};}',
        'void-expression-conditional': 'void g(){}void f(bool b){return b?static_cast<void>(g()):void{};}',
        'void-expression-comma': 'int g(){return 1;}int f(){return(static_cast<void>(g()),2);}',
        'void-expression-record': 'int n=0;struct R{int x;~R(){++n;}};void f(){static_cast<void>(R{1});}',
        'void-expression-record-lvalue': 'struct R{int n;operator int(){return ++n;}};void f(R&r){static_cast<void>(r);}',
        'void-expression-conversion': 'struct R{int n;operator int(){return ++n;}};void f(R&r){static_cast<void>(static_cast<int>(r));}',
        'void-expression-array': 'struct R{int n;~R(){}};using A=R[2];void f(){static_cast<void>(A{{1},{2}});}',
        'void-expression-empty': 'struct E{E(){}~E(){}};void f(){static_cast<void>(E{});}',
        'void-expression-extended-reference': 'struct R{int n;~R(){}};void f(){const R&r=R{1};static_cast<void>(r);}',
        'void-expression-dmi': 'struct R{int n=(static_cast<void>(1),2);};int f(){R r{};return r.n;}',
        'void-expression-constexpr': 'constexpr int f(){void();void{};return(static_cast<void>(1),2);}static_assert(f()==2);',
        'void-expression-constexpr-void': 'constexpr void f(){void{};}static_assert((f(),true));',
        'void-expression-noexcept': 'static_assert(noexcept(void()));static_assert(noexcept(void{}));static_assert(noexcept(static_cast<void>(nullptr)));',
        'void-expression-folded-promotion-1': 'enum E : int { value = (static_cast<void>(0), 1) };',
        'void-expression-folded-promotion-2': 'static_assert((static_cast<void>(0), true), "checked condition");',
        'void-expression-folded-promotion-3': 'enum E:int{v=(static_cast<void>(0),1)}; int main(){}',
        'void-expression-folded-promotion-4': 'static_assert((static_cast<void>(0),true),"condition"); int main(){}',
    })
    core_v2.update({
        'default-argument-scalar': 'int f(int n=3){return n;}int main(){return f()-3;}',
        'default-argument-nested': 'int n=0;int g(int x=++n){return x;}int f(int x=g()){return x;}',
        'default-argument-override': 'int n=0;int f(int x=++n){return x;}int main(){return f(0)+n;}',
        'default-argument-namespace': 'namespace N{int n=3;int f(int x=n){return x;}}int main(){int n=4;return N::f()-3;}',
        'default-argument-redeclaration': 'int f(int a,int b=4);int f(int a=3,int b){return a+b;}int main(){return f()-7;}',
        'default-argument-inherited': 'int f(int n=3);int f(int n){return n;}int main(){return f()-3;}',
        'default-argument-nonfirst': 'int f(int a,int b=2,int c=3){return a+b+c;}int main(){return f(1)-6;}',
        'default-argument-enum': 'enum class E:unsigned char{yes=9};int f(E e=E::yes){return static_cast<int>(e);}',
        'default-argument-bool': 'bool f(bool b=true){return b;}',
        'default-argument-narrow': 'int f(unsigned char n=255){return n;}',
        'default-argument-pointer': 'int n=1;int f(int*p=&n){return ++*p;}',
        'default-argument-null': 'int f(int*p=nullptr){return p?1:0;}',
        'default-argument-reference': 'int n=1;int f(int&r=n){return ++r;}',
        'default-argument-scalar-temporary': 'int f(const int&r=3){return r;}',
        'default-argument-array-temporary': 'using A=int[2];int f(const A&r=A{3,4}){return r[0]+r[1];}',
        'default-argument-record-reference': 'struct R{int n;~R(){}};int f(const R&r=R{3}){return r.n;}',
        'default-argument-subobject': 'struct R{int n;~R(){}};int f(const int&r=R{3}.n){return r;}',
        'default-argument-record-value': 'struct R{int n;~R(){}};int f(R r=R{3}){return r.n;}',
        'default-argument-record-copy': 'struct R{int n;R(int v):n(v){}R(const R&r,int e=1):n(r.n+e){}R&get(){return *this;}};int f(R r=R(2).get()){return r.n;}',
        'default-argument-empty': 'struct R{};int f(R r=R{}){return sizeof(r);}',
        'default-argument-method': 'struct R{int n;int f(int a=2)const{return n+a;}};int main(){R r{1};return r.f()-3;}',
        'default-argument-static-method': 'struct R{static int f(int n=1){return n;}};int main(){return R::f()-1;}',
        'default-argument-constructor': 'struct R{int n;R(int v=1):n(v){}};int main(){R r;return r.n-1;}',
        'default-argument-array-default-constructor': 'struct T{int n;~T(){}};struct R{int n;R(const T&t=T{1}):n(t.n){}};int main(){R r[2];return r[0].n+r[1].n-2;}',
        'default-argument-array-copy-constructor': 'struct T{int n;~T(){}};struct R{int n;R():n(1){}R(const R&r,const T&t=T{2}):n(r.n+t.n){}};struct A{R r[2];};int main(){A a;A b=a;return b.r[0].n+b.r[1].n-6;}',
        'default-argument-dmi': 'int g(int n=2){return n;}struct R{int n=g();};int main(){R r{};return r.n-2;}',
        'default-argument-constexpr': 'constexpr int f(int n=3){return n;}static_assert(f()==3);',
        'default-argument-query': 'void f(int n=3)noexcept{}static_assert(noexcept(f()));',
        'default-argument-void-comma': 'int f(int n=(void{},3)){return n;}static_assert(noexcept(void{}));',
        'default-argument-promotion-1': 'int g(int n=1){return n;}struct R{operator int()const{return g();}};',
        'default-argument-promotion-2': 'struct R{int n;R(R&&r,int extra=0):n(r.n+extra){}};',
        'default-argument-promotion-3': 'struct R{int n;R(const R&r,int extra=0):n(r.n+extra){}};',
        'default-argument-promotion-4': 'struct R{int n;R(int v=1):n(v){}};',
        'default-argument-promotion-5': 'struct R{int n;int get(int v=1){return n+v;}};int f(){R r{1};return r.get();}',
        'default-argument-promotion-6': 'struct R{int n;int operator()(int v=1){return n+v;}};',
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
    leaf_copy_lines = [i for i, text in enumerate(generated_copy_source.splitlines(), 1)
                       if text.startswith(" Leaf(const Leaf&s)")]
    assert len(leaf_copy_lines) == 1, leaf_copy_lines
    for line in (1, 6, 7, 8, 10, 11, 12, 13, 14):
        rid = gc_records[line]["id"]
        source_kind = "ptr:" if line in (12, 13) else "cptr:"
        locations = leaf_copy_lines if line == 1 else ((8, 9) if line == 8 else (line,))
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
        'bitfield': 'struct R{int bits:2;int n=1;};',
        'base': 'struct B{int n=1;};struct R:B{int next=2;};',
        'attribute': 'struct R{[[maybe_unused]] int n=1;};',
        'floating-default': 'struct R{int n=static_cast<int>(1.5);};',
        'lambda-unused': 'struct R{int n=[](){return 1;}();};',
        'lambda-overridden': 'struct R{int n=[](){return 1;}();};void f(){R r{7};}',
        'throw-unused': 'struct R{int n=(throw 1,2);};',
        'throw-overridden': 'struct R{int n=(throw 1,2);R():n(7){}};',
        'new-default': 'struct R{int*p=new int(1);};',
        'reinterpret-default': 'struct R{int*p=reinterpret_cast<int*>(1);};',
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
bool constantAnd(const S&a,const S&b){return a&&b;}
bool constantOr(const S&a,const S&b){return a||b;}
bool castMutable(S&r){return static_cast<bool>(r);}
bool castConst(const S&r){return static_cast<bool>(r);}
bool mutableQuery(S&r){return noexcept(static_cast<bool>(r));}
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
    for prefix, selected, branch in (
            ("bool logicalAnd(", selected_int[0]["name"], "true"),
            ("bool logicalOr(", selected_int[0]["name"], "false"),
            ("bool constantAnd(", bool_name, "true"),
            ("bool constantOr(", bool_name, "false")):
        function = uc_function(prefix)
        calls = gc_calls(function)
        assert [c["callee"] for c in calls] == [selected, selected], function
        assert [storage_pointer_object(function, c["args"][0]) for c in calls] == [
            ("parameter", p["name"]) for p in function["params"]]
        positions = [i for i, node in enumerate(function["body"]) if node["op"] == "call"]
        between = function["body"][positions[0]+1:positions[1]]
        branches = [n for n in between if n["op"] == "branch"]
        labels = [n["label"] for n in between if n["op"] == "label"]
        assert len(branches) == 1 and labels, function
        assert labels[-1] == branches[0][branch], function
    for prefix, selected in (("bool castMutable(", selected_int[0]["name"]),
                             ("bool castConst(", bool_name)):
        function = uc_function(prefix)
        calls = gc_calls(function)
        assert len(calls) == 1 and calls[0]["callee"] == selected, function
        assert storage_pointer_object(function, calls[0]["args"][0]) == (
            "parameter", function["params"][0]["name"]), function


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
    for prefix, expected in (("bool query(", True), ("bool recordQuery(", True),
                             ("bool mutableQuery(", False)):
        function = uc_function(prefix)
        assert not gc_calls(function) and not any(v["type"] in uc_records.values() for v in function["locals"]), function
        returns = [n["value"] for n in function["body"] if n["op"] == "return"]
        assert len(returns) == 1 and nq_constant(function, returns[0]) is expected
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
        'unused-throw': 'struct R{operator int()const{throw 1;}};',
        'query-throw': 'struct R{operator int()const noexcept(false){throw 1;}};bool f(R&r){return noexcept(static_cast<int>(r));}',
        'folded-float': 'struct R{constexpr operator int()const{return static_cast<int>(1.0);}};constexpr R r{};static_assert(int(r)==1,"value");',
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
        'extended-unused-float-operand': 'int f(){const int&r=static_cast<int>(1.0);return r;}',
        'extended-dead-float-operand': 'struct R{int n;};int f(){if(false){const R&r=R{static_cast<int>(1.0)};}return 0;}',
        'extended-query-float-operand': 'int f(){const bool&r=noexcept(static_cast<int>(1.0));return r;}',
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
        'aggregate-list-conversion': 'struct V{int n;};struct R{int n;operator V()const{return V{n};}};int f(){R r{6};const V&v={r};return v.n;}',
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
    er_schema = json.loads((repository / "utils/translate-frontends/docs/schemas/manifest.schema.json").read_text())
    er_layout_schema = er_schema["properties"]["record_layouts"]["items"]["properties"]
    assert er_layout_schema["field_offsets_bits"]["minItems"] == 0
    assert er_layout_schema["size_bits"]["minimum"] == 8
    er_profile_rules = [rule for rule in er_schema["allOf"]
                        if "record_layouts" in rule.get("then", {}).get("required", [])]
    assert len(er_profile_rules) == 1
    assert er_profile_rules[0]["if"]["properties"]["profile"]["const"] == "cpp-core-v2"
    assert er_profile_rules[0]["else"]["not"]["required"] == ["record_layouts"]
    empty_record_source = """int count=0;
void mark(){++count;}
struct E{};
constexpr E global{};
constexpr E otherGlobal{};
struct C {
 C(){mark();}
 C(const C&s){mark();}
 ~C(){mark();}
 int operator()()const{mark();return 1;}
};
struct F {
 operator E()const{return {};}
};
struct D {D()=default;D(const D&)=default;D&operator=(const D&)=default;};
struct Holder{E a[2];int n;};
E&left(E&e){mark();return e;}
const E&right(const E&e){mark();return e;}
E trivialCopy(const E&e){return right(e);}
E&assignment(E&a,const E&b){return left(a)=right(b);}
E&explicitAssignment(E&a,const E&b){return left(a).operator=(right(b));}
D defaultedCopy(const D&d){return d;}
D&defaultedAssignment(D&a,const D&b){return a=b;}
void identities(){E a,b;mark();}
E result(){return E{};}
E converted(const F&f){return f;}
int params(E a,E b){return &a==&b?0:1;}
int pass(const E&e){return params(e,e);}
C userCopy(const C&c){return c;}
void local(){const C&c=C();const C&alias{c};mark();}
void argument(const C&c){mark();}
void full(){argument(C());mark();}
void array(){const C(&a)[2]={C(),C()};mark();}
void discarded(){using A=C[2];A{C(),C()};mark();}
bool query(){return noexcept(C());}
"""
    empty_records = check("v2-empty-records-protocol", empty_record_source, profile="cpp-core-v2")
    er_functions = {f["name"]: f for f in empty_records["functions"]}

    def er_line(prefix):
        matches = [i for i, line in enumerate(empty_record_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(matches) == 1, (prefix, matches)
        return matches[0]

    def er_function(prefix, result=None, parameters=None):
        matches = [f for f in empty_records["functions"] if f["loc"]["line"] == er_line(prefix)
                   and (result is None or f["result"] == result)
                   and (parameters is None or [p["type"] for p in f["params"]] == parameters)]
        assert len(matches) == 1, (prefix, result, parameters, matches)
        return matches[0]

    def er_record(prefix):
        matches = [r for r in empty_records["records"] if r["loc"]["line"] == er_line(prefix)]
        assert len(matches) == 1, (prefix, matches)
        return matches[0]

    def er_pointer(function, expr):
        if expr["kind"] == "cast":
            return er_pointer(function, expr["args"][0])
        if expr["kind"] in ("address", "array_decay"):
            return er_place(function, expr["args"][0])
        assert expr["kind"] == "var", expr
        values = [n["value"] for n in function["body"] if n["op"] == "assign"
                  and n["target"].get("name") == expr["name"]]
        if values:
            assert len(values) == 1, values
            return er_pointer(function, values[0])
        calls = [n for n in gc_calls(function) if n.get("target", {}).get("name") == expr["name"]]
        if calls:
            assert len(calls) == 1 and calls[0]["callee"] in (er_left, er_right), calls
            return er_pointer(function, calls[0]["args"][0])
        assert any(p["name"] == expr["name"] for p in function["params"]), expr
        return ("parameter", expr["name"])

    def er_place(function, expr):
        if expr["kind"] == "var":
            return ("object", expr["name"])
        if expr["kind"] == "dereference":
            return er_pointer(function, expr["args"][0])
        assert expr["kind"] == "index", expr
        return ("element", er_pointer(function, expr["args"][0]), gc_identity(function, expr["args"][1]))

    er_empty = [er_record(prefix) for prefix in ("struct E", "struct C", "struct F", "struct D")]
    for record in er_empty:
        assert record["fields"] == []
        assert record["layout"] == {"size_bits": 8, "abi_align_bits": 8, "field_offsets_bits": []}
    eid, cid, fid, did = [r["id"] for r in er_empty]
    globals_of_e = [g for g in empty_records["globals"] if g["type"] == eid]
    assert len(globals_of_e) == 2 and globals_of_e[0]["name"] != globals_of_e[1]["name"]
    assert all(g["value"]["kind"] == "aggregate" and g["value"]["args"] == [] for g in globals_of_e)
    holder = er_record("struct Holder")
    assert holder["layout"] == {"size_bits": 64, "abi_align_bits": 32, "field_offsets_bits": [0, 32]}
    assert [f["type"] for f in holder["fields"]] == ["arr:2:"+eid, "int"]
    er_left = er_function("E&left(", "ptr:"+eid, ["ptr:"+eid])["name"]
    er_right = er_function("const E&right(", "cptr:"+eid, ["cptr:"+eid])["name"]
    er_mark = er_function("void mark(", "void", [])["name"]
    copy = er_function("E trivialCopy(", "void", ["ptr:"+eid, "cptr:"+eid])
    assert [c["callee"] for c in gc_calls(copy)] == [er_right], copy
    assert not any(n["op"] == "assign" and n["target"]["type"] == eid for n in copy["body"]), copy
    for prefix, callees in (("E&assignment(", [er_right, er_left]),
                            ("E&explicitAssignment(", [er_left, er_right])):
        function = er_function(prefix, "ptr:"+eid, ["ptr:"+eid, "cptr:"+eid])
        assert [c["callee"] for c in gc_calls(function)] == callees, function
        assert not any(n["op"] == "assign" and n["target"]["type"] == eid for n in function["body"]), function
        returned = [n["value"] for n in function["body"] if n["op"] == "return"]
        assert len(returned) == 1
        assert er_pointer(function, returned[0]) == ("parameter", function["params"][0]["name"])
    for prefix, result, parameters in (("D defaultedCopy(", "void", ["ptr:"+did, "cptr:"+did]),
                                      ("D&defaultedAssignment(", "ptr:"+did, ["ptr:"+did, "cptr:"+did])):
        function = er_function(prefix, result, parameters)
        assert not gc_calls(function)
        assert not any(n["op"] == "assign" and n["target"]["type"] == did for n in function["body"])
    identities = er_function("void identities(", "void", [])
    places = [v for v in identities["locals"] if v["type"] == eid]
    assert len(places) == 2 and places[0]["name"] != places[1]["name"]
    result = er_function("E result(", "void", ["ptr:"+eid])
    assert not gc_calls(result) and not any(v["type"] == eid for v in result["locals"])
    conversion = er_function(" operator E(", "void", ["ptr:"+eid, "cptr:"+fid])
    converted = er_function("E converted(", "void", ["ptr:"+eid, "cptr:"+fid])
    assert [c["callee"] for c in gc_calls(converted)] == [conversion["name"]]
    call = gc_calls(converted)[0]
    assert [er_pointer(converted, a) for a in call["args"]] == [("parameter", p["name"]) for p in converted["params"]]
    params = er_function("int params(", "int", ["ptr:"+eid, "ptr:"+eid])
    passed = er_function("int pass(", "int", ["cptr:"+eid])
    call = gc_calls(passed)
    assert len(call) == 1 and call[0]["callee"] == params["name"], passed
    destinations = [er_pointer(passed, a) for a in call[0]["args"]]
    assert len(set(destinations)) == 2 and all(d[0] == "object" for d in destinations), destinations
    ctor = er_function(" C(){", "void", ["ptr:"+cid])["name"]
    copy_ctor = er_function(" C(const C&s)", "void", ["ptr:"+cid, "cptr:"+cid])["name"]
    copied = er_function("C userCopy(", "void", ["ptr:"+cid, "cptr:"+cid])
    assert [c["callee"] for c in gc_calls(copied)] == [copy_ctor]
    assert [er_pointer(copied, a) for a in gc_calls(copied)[0]["args"]] == [("parameter", p["name"]) for p in copied["params"]]
    destructor = cid+"_destroy"
    argument = er_function("void argument(", "void", ["cptr:"+cid])["name"]
    for prefix, expected in (("void local(", [ctor, er_mark, destructor]),
                             ("void full(", [ctor, argument, destructor, er_mark])):
        function = er_function(prefix, "void", [])
        calls = gc_calls(function)
        assert [c["callee"] for c in calls] == expected, function
        objects = [v for v in function["locals"] if v["type"] == cid]
        assert len(objects) == 1
        places = [er_pointer(function, c["args"][0]) for c in calls if c["callee"] in (ctor, destructor)]
        assert places == [("object", objects[0]["name"])]*2
        flags = [n["target"]["name"] for n in function["body"] if n["op"] == "assign" and n["value"].get("value") is True]
        assert len(flags) == 1, function
    for prefix, expected in (("void array(", [ctor, ctor, er_mark, destructor, destructor]),
                             ("void discarded(", [ctor, ctor, destructor, destructor, er_mark])):
        function = er_function(prefix, "void", [])
        calls = gc_calls(function)
        assert [c["callee"] for c in calls] == expected, function
        arrays = [v for v in function["locals"] if v["type"] == "arr:2:"+cid]
        assert len(arrays) == 1 and not any(v["type"] == cid for v in function["locals"])
        root = ("object", arrays[0]["name"])
        assert [er_pointer(function, c["args"][0]) for c in calls if c["callee"] == ctor] == [("element", root, i) for i in (0, 1)]
        assert [er_pointer(function, c["args"][0]) for c in calls if c["callee"] == destructor] == [("element", root, i) for i in (1, 0)]
    query = er_function("bool query(", "bool", [])
    assert not gc_calls(query)
    returned = [n["value"] for n in query["body"] if n["op"] == "return"]
    assert len(returned) == 1 and nq_constant(query, returned[0]) is False
    for function in empty_records["functions"]:
        for call in gc_calls(function):
            assert [a["type"] for a in call["args"]] == [p["type"] for p in er_functions[call["callee"]]["params"]], call
    for node in walk(empty_records):
        assert not (node.get("kind") == "member" and node.get("name") == "nct_emit_empty_storage"), node
        if node.get("kind") == "aggregate" and node.get("type") in (eid, cid, fid, did):
            assert node["args"] == [], node
    with tempfile.TemporaryDirectory(prefix="neverc-empty-records-relocated-") as temp:
        relocated = check("empty-records-relocated", empty_record_source,
                          root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == empty_records, "empty record identities depend on the absolute root"

    empty_record_rejected = {
        'base': 'struct E{};struct D:E{};',
        'union': 'union E{};',
        'virtual': 'struct E{virtual void f(){}};',
        'template': 'template<class T>struct E{};E<int> e;',
        'overaligned': 'struct alignas(2) E{};',
        'attribute': 'struct __attribute__((packed)) E{};',
        'reference-field': 'struct E{int&r;};',
        'static-reference': 'struct E{};void f(){static const E&e=E{};}',
        'thread-reference': 'struct E{};void f(){thread_local const E&e=E{};}',
        'escaping-reference': 'struct E{};const E&f(){return E{};}',
        'unevaluated-unsupported': 'struct E{operator double()const{return 1.0;}};bool f(){E e;return noexcept(static_cast<double>(e));}',
        'extent-limit': 'struct E{};using A=E[65537];',
        'storage-limit': 'struct E{};using A=E[512][512];',
        'global-destruction': 'struct E{~E(){}};const E e{};',
    }
    for name, source in empty_record_rejected.items():
        check("v2-" + 'empty_record_rejected' + "-" + name, source, "TR0201", profile="cpp-core-v2")
    empty_record_invalid = {
        'initializer-arity': 'struct E{};void f(){E e{1};}',
        'missing-field': 'struct E{};int f(){E e;return e.n;}',
        'private-carrier-name': 'struct E{};int f(){E e;return e.nct_emit_empty_storage;}',
        'deleted-copy': 'struct E{E()=default;E(const E&)=delete;};void f(){E a;E b=a;}',
        'deleted-move': 'struct E{E()=default;E(E&&)=delete;};void f(){E a;E b=static_cast<E&&>(a);}',
        'mutable-reference': 'struct E{};void f(){E&r=E{};}',
    }
    for name, source in empty_record_invalid.items():
        check("v2-" + 'empty_record_invalid' + "-" + name, source, "TR0202", profile="cpp-core-v2")
    empty_record_missing = {
        'constructor': 'struct E{E();};void f(){E e;}',
        'destructor': 'struct E{~E();};void f(){E e;}',
    }
    for name, source in empty_record_missing.items():
        check("v2-" + 'empty_record_missing' + "-" + name, source, "TR0203", profile="cpp-core-v2")
    check("v1-empty-record-reference", "struct E{};void f(){E e;}", "TR0201")
    check("v1-empty-record-decay", "struct F{int operator()()const{return 1;}};int f(){F f;return f();}", "TR0201")
    void_expression_source = """int count=0;
void mark(int n){count+=n;}
int source(){return ++count;}
struct R {
 int n;
 R(int value):n(value){mark(1);}
 ~R(){mark(n);}
 operator int()const{return n;}
};
R&receiver(R&r){mark(1);return r;}
void noop(){int n;int a[2];(void)n;(void)a;(void)nullptr;void();void{};}
void named(){static_cast<void>(source);}
void effects(){(void)source();static_cast<void>(source());(void(source()));}
void places(int&x,int*p){(void)x;static_cast<void>(*p);(void)p[0];}
void member(R&r){(void)r;(void)receiver(r);(void)r.n;}
void converted(R&r){(void)static_cast<int>(r);}
void temporary(){(void)R(1);mark(9);}
void comma(){(void)R(1),static_cast<void>(R(2));mark(9);}
void observed(){(void)R(1),mark(9);}
void array(){using A=R[2];static_cast<void>(A{R(1),R(2)});mark(9);}
void returnValue(){R local(9);return static_cast<void>(R(1));}
void returnedCall(){return static_cast<void>(mark(1));}
void returnedEmpty(){return void{};}
void conditional(bool b){return b?(void)R(1):void(R(2));}
void conditionalEmpty(bool b){return b?void{}:void();}
bool query(){return noexcept(static_cast<void>(R(1)));}
bool pureQuery(){return noexcept(void{});}
int sizeQuery(){return sizeof((static_cast<void>(source()),1));}
constexpr int constant(){void();void{};return (static_cast<void>(1),2);}
constexpr int folded=constant();
static_assert((static_cast<void>(0),true));
"""
    void_expressions = check("v2-void-expressions-protocol", void_expression_source, profile="cpp-core-v2")
    ve_functions = {f["name"]: f for f in void_expressions["functions"]}

    def ve_line(prefix):
        matches = [i for i, line in enumerate(void_expression_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(matches) == 1, (prefix, matches)
        return matches[0]

    def ve_function(prefix, result, parameters):
        matches = [f for f in void_expressions["functions"] if f["loc"]["line"] == ve_line(prefix)
                   and f["result"] == result and [p["type"] for p in f["params"]] == parameters]
        assert len(matches) == 1, (prefix, result, parameters, matches)
        return matches[0]

    def ve_pointer(function, expr):
        if expr["kind"] == "cast":
            return ve_pointer(function, expr["args"][0])
        if expr["kind"] in ("address", "array_decay"):
            return ve_place(function, expr["args"][0])
        assert expr["kind"] == "var", expr
        values = [n["value"] for n in function["body"] if n["op"] == "assign" and n["target"].get("name") == expr["name"]]
        if values:
            assert len(values) == 1, values
            return ve_pointer(function, values[0])
        assert any(p["name"] == expr["name"] for p in function["params"]), expr
        return ("parameter", expr["name"])

    def ve_place(function, expr):
        if expr["kind"] == "var":
            return ("object", expr["name"])
        if expr["kind"] == "dereference":
            return ve_pointer(function, expr["args"][0])
        assert expr["kind"] == "index", expr
        return ("element", ve_pointer(function, expr["args"][0]), gc_identity(function, expr["args"][1]))

    rid = next(r["id"] for r in void_expressions["records"] if r["loc"]["line"] == ve_line("struct R {"))
    mark = ve_function("void mark(", "void", ["int"])["name"]
    source = ve_function("int source(", "int", [])["name"]
    constructor = ve_function(" R(int value)", "void", ["ptr:"+rid, "int"])["name"]
    destructor = rid+"_destroy"
    conversion = ve_function(" operator int(", "int", ["cptr:"+rid])["name"]
    for prefix in ("void noop(", "void named(", "void returnedEmpty("):
        function = ve_function(prefix, "void", [])
        assert [n["op"] for n in function["body"]] == ["label", "return"], function
        assert "value" not in function["body"][-1]
    effects = ve_function("void effects(", "void", [])
    assert [c["callee"] for c in gc_calls(effects)] == [source]*3, effects
    places = ve_function("void places(", "void", ["ptr:int", "ptr:int"])
    assert not gc_calls(places)
    assert not any(n["op"] == "assign" and any(v.get("type") == "int" and v.get("kind") in ("dereference", "index", "member")
                   for v in walk(n["value"])) for n in places["body"]), places
    member = ve_function("void member(", "void", ["ptr:"+rid])
    receiver = ve_function("R&receiver(", "ptr:"+rid, ["ptr:"+rid])["name"]
    assert [c["callee"] for c in gc_calls(member)] == [receiver], member
    assert not any(n["op"] == "assign" and n["value"]["type"] in (rid, "int") for n in member["body"]), member
    converted = ve_function("void converted(", "void", ["ptr:"+rid])
    assert [c["callee"] for c in gc_calls(converted)] == [conversion], converted
    for prefix, expected, count in (("void temporary(", [constructor, destructor, mark], 1),
                                    ("void comma(", [constructor, constructor, destructor, destructor, mark], 2),
                                    ("void observed(", [constructor, mark, destructor], 1),
                                    ("void returnValue(", [constructor, constructor, destructor, destructor], 2)):
        function = ve_function(prefix, "void", [])
        calls = gc_calls(function)
        assert [c["callee"] for c in calls] == expected, function
        constructed = [ve_pointer(function, c["args"][0]) for c in calls if c["callee"] == constructor]
        destroyed = [ve_pointer(function, c["args"][0]) for c in calls if c["callee"] == destructor]
        assert len(set(constructed)) == count and destroyed == constructed[::-1], function
        assert len([v for v in function["locals"] if v["type"] == rid]) == count
    array = ve_function("void array(", "void", [])
    calls = gc_calls(array)
    assert [c["callee"] for c in calls] == [constructor, constructor, destructor, destructor, mark], array
    arrays = [v for v in array["locals"] if v["type"] == "arr:2:"+rid]
    assert len(arrays) == 1 and not any(v["type"] == rid for v in array["locals"])
    root = ("object", arrays[0]["name"])
    assert [ve_pointer(array, c["args"][0]) for c in calls[:2]] == [("element", root, i) for i in (0, 1)]
    assert [ve_pointer(array, c["args"][0]) for c in calls[2:4]] == [("element", root, i) for i in (1, 0)]
    returned = ve_function("void returnedCall(", "void", [])
    assert [c["callee"] for c in gc_calls(returned)] == [mark]
    conditional = ve_function("void conditional(", "void", ["bool"])
    calls = gc_calls(conditional)
    assert [c["callee"] for c in calls] == [constructor, constructor, destructor, destructor]
    constructed = [ve_pointer(conditional, c["args"][0]) for c in calls[:2]]
    assert len(set(constructed)) == 2
    assert [ve_pointer(conditional, c["args"][0]) for c in calls[2:]] == constructed[::-1]
    flags = [n["target"]["name"] for n in conditional["body"] if n["op"] == "assign" and n["value"].get("value") is True]
    assert len(set(flags)) == 2
    for flag in flags:
        values = [n["value"]["value"] for n in conditional["body"] if n["op"] == "assign" and n["target"].get("name") == flag]
        assert values == [False, True, False], conditional
        assert any(n["op"] == "branch" and n["condition"].get("name") == flag for n in conditional["body"])
    empty = ve_function("void conditionalEmpty(", "void", ["bool"])
    assert not gc_calls(empty)
    for prefix, expected in (("bool query(", False), ("bool pureQuery(", True)):
        function = ve_function(prefix, "bool", [])
        assert not gc_calls(function) and not any(v["type"] == rid for v in function["locals"])
        returned = [n["value"] for n in function["body"] if n["op"] == "return"]
        assert len(returned) == 1 and nq_constant(function, returned[0]) is expected
    assert not gc_calls(ve_function("int sizeQuery(", "int", []))
    for function in void_expressions["functions"]:
        assert all(v["type"] != "void" for v in function["locals"]+function["params"]), function
        for call in gc_calls(function):
            callee = ve_functions[call["callee"]]
            assert [a["type"] for a in call["args"]] == [p["type"] for p in callee["params"]]
            if callee["result"] == "void":
                assert "target" not in call
        if function["result"] == "void":
            assert all("value" not in n for n in function["body"] if n["op"] == "return"), function
    for node in walk(void_expressions):
        if "kind" in node:
            assert node.get("type") != "void", node
    with tempfile.TemporaryDirectory(prefix="neverc-void-expressions-relocated-") as temp:
        relocated = check("void-expressions-relocated", void_expression_source,
                          root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == void_expressions, "void expression identities depend on the absolute root"

    void_expression_rejected = {
        'floating': 'void f(){static_cast<void>(1.0);}',
        'volatile': 'void f(){volatile int n=1;static_cast<void>(n);}',
        'volatile-void-alias': 'using V=volatile void;void f(){V();}',
        'string': 'void f(){static_cast<void>("text");}',
        'function-pointer': 'void g(){}void f(){static_cast<void>(&g);}',
        'allocation': 'void f(){static_cast<void>(new int(1));}',
        'lambda': 'void f(){static_cast<void>([]{});}',
        'dead-operand': 'void f(){if(false){static_cast<void>(1.0);}}',
        'constexpr-operand': 'constexpr int f(){static_cast<void>(1.0);return 1;}static_assert(f()==1);',
        'assertion-operand': 'static_assert((static_cast<void>(1.0),true));',
        'enum-operand': 'enum E:int{one=(static_cast<void>(1.0),1)};',
        'noexcept-operand': 'bool f(){return noexcept(static_cast<void>(1.0));}',
        'untyped-assembly': 'asm("");void f(){void{};}',
    }
    for name, source in void_expression_rejected.items():
        check("v2-" + 'void_expression_rejected' + "-" + name, source, "TR0201", profile="cpp-core-v2")
    void_expression_invalid = {
        'nonempty-list': 'void f(){void{1};}',
        'extra-arguments': 'void f(){(void(1,2));}',
        'value-from-void': 'int f(){return static_cast<int>(void());}',
        'nonvoid-return': 'int f(){return void();}',
        'void-variable': 'void f(){void value;}',
        'void-reference': 'void f(){void&r=void();}',
        'overloaded-designator': 'void g(int){}void g(bool){}void f(){static_cast<void>(g);}',
        'member-designator': 'struct R{int n;void g(){}};void f(R&r){static_cast<void>(r.g);}',
    }
    for name, source in void_expression_invalid.items():
        check("v2-" + 'void_expression_invalid' + "-" + name, source, "TR0202", profile="cpp-core-v2")
    void_expression_missing = {
        'direct-call': 'void g();void f(){return static_cast<void>(g());}',
        'record-call': 'struct R{int n;~R();};void f(){static_cast<void>(R{1});}',
    }
    for name, source in void_expression_missing.items():
        check("v2-" + 'void_expression_missing' + "-" + name, source, "TR0203", profile="cpp-core-v2")
    check("v1-void-expression-reference", "void f(){static_cast<void>(1);}", "TR0201")
    check("v1-void-expression-decay", "void f(){void();}", "TR0201")
    mutable_global_source = """int count;
extern int shared;
int shared=7;
extern int shared;
const int fixed=5;
bool enabled;
enum class E:unsigned char{one=1};
E state;
unsigned long long large=18446744073709551615ULL;
namespace Left{int value=2;}
namespace Right{int value=3;}
int*address(){return &count;}
int&alias(){return count;}
const int&view(){return count;}
int increment(){return ++count;}
int read(){return count;}
int share(){++shared;return shared;}
void set(){enabled=true;state=E::one;Left::value=4;Right::value=6;}
int useDefault(int&value=count){return ++value;}
int defaulted(){return useDefault();}
const int*fixedAddress(){return &fixed;}
int main(){return read();}
"""
    globals_module = check("v2-mutable-scalar-globals-protocol", mutable_global_source,
                           profile="cpp-core-v2")
    mg_globals = {g["name"]: g for g in globals_module["globals"]}

    def mg_line(prefix):
        lines = [i for i, line in enumerate(mutable_global_source.splitlines(), 1)
                 if line.startswith(prefix)]
        assert len(lines) == 1, (prefix, lines)
        return lines[0]

    def mg_global(prefix, typ, value, mutable=True):
        matches = [g for g in globals_module["globals"] if g["loc"]["line"] == mg_line(prefix)]
        assert len(matches) == 1, (prefix, matches)
        g = matches[0]
        assert g["type"] == typ and g.get("mutable", False) is mutable, g
        assert g["value"]["kind"] == "literal" and g["value"]["type"] == typ, g
        assert g["value"]["value"] == value, g
        return g["name"]

    mg_count = mg_global("int count;", "int", "0")
    mg_shared = mg_global("int shared=", "int", "7")
    mg_fixed = mg_global("const int fixed=", "int", "5", False)
    mg_enabled = mg_global("bool enabled;", "bool", False)
    mg_state = mg_global("E state;", "u8", "0")
    mg_global("unsigned long long large=", "u64", "18446744073709551615")
    mg_left = mg_global("namespace Left{", "int", "2")
    mg_right = mg_global("namespace Right{", "int", "3")
    assert len(mg_globals) == 8 and mg_left != mg_right
    assert all(g["value"]["kind"] == "literal" for g in mg_globals.values())
    assert not globals_module["records"]

    def mg_function(prefix, result, params=()):
        matches = [f for f in globals_module["functions"]
                   if f["loc"]["line"] == mg_line(prefix) and f["result"] == result
                   and tuple(p["type"] for p in f["params"]) == params]
        assert len(matches) == 1, (prefix, matches)
        return matches[0]

    def mg_root(function, expr):
        if expr["kind"] in ("cast", "address", "dereference"):
            return mg_root(function, expr["args"][0])
        assert expr["kind"] == "var", expr
        if expr["name"] in mg_globals:
            return expr["name"]
        assignments = [i["value"] for i in function["body"] if i["op"] == "assign"
                       and i["target"].get("kind") == "var"
                       and i["target"]["name"] == expr["name"]]
        assert len(assignments) == 1, (expr, assignments)
        return mg_root(function, assignments[0])

    for prefix, result, name in (("int*address(", "ptr:int", mg_count),
                                 ("int&alias(", "ptr:int", mg_count),
                                 ("const int&view(", "cptr:int", mg_count),
                                 ("int read(", "int", mg_count),
                                 ("const int*fixedAddress(", "cptr:int", mg_fixed)):
        f = mg_function(prefix, result)
        returns = [i["value"] for i in f["body"] if i["op"] == "return"]
        assert len(returns) == 1 and mg_root(f, returns[0]) == name, f
        assert not gc_calls(f), f
    for prefix, name in (("int increment(", mg_count), ("int share(", mg_shared)):
        f = mg_function(prefix, "int")
        writes = [i for i in f["body"] if i["op"] == "assign"
                  and i["target"].get("name") == name]
        assert len(writes) == 1 and writes[0]["value"]["type"] == "int", f
    f = mg_function("void set(", "void")
    writes = [i["target"]["name"] for i in f["body"] if i["op"] == "assign"
              and i["target"].get("name") in mg_globals]
    assert writes == [mg_enabled, mg_state, mg_left, mg_right], f
    default = mg_function("int useDefault(", "int", ("ptr:int",))
    f = mg_function("int defaulted(", "int")
    calls = gc_calls(f)
    assert len(calls) == 1 and calls[0]["callee"] == default["name"], f
    assert len(calls[0]["args"]) == 1 and mg_root(f, calls[0]["args"][0]) == mg_count, f
    assert not gc_calls(default), default
    signatures = {f["name"]: f for f in globals_module["functions"]}
    assert len(signatures) == 11
    for f in globals_module["functions"]:
        for call in gc_calls(f):
            assert [a["type"] for a in call["args"]] == [p["type"] for p in signatures[call["callee"]]["params"]]
    with tempfile.TemporaryDirectory(prefix="neverc-mutable-globals-relocated-") as temp:
        relocated = check("mutable-globals-relocated", mutable_global_source,
                          root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == globals_module, "global storage identities depend on the absolute root"

    mutable_global_positive = {
        'zero': 'int value;int main(){return value;}',
        'explicit': 'int value=1;int main(){return ++value-2;}',
        'bool': 'bool value;int main(){value=true;return value?0:1;}',
        'enum': 'enum class E:unsigned char{v=7};E value;int main(){value=E::v;return static_cast<int>(value)-7;}',
        'extern-before': 'extern int value;int value=3;int main(){return ++value-4;}',
        'extern-after': 'int value;extern int value;int main(){return value;}',
        'static': 'static int value;int f(){return ++value;}',
        'inline': 'inline int value=4;int f(){return ++value;}',
        'namespaces': 'namespace A{int n;}namespace B{int n=2;}int f(){A::n=3;return A::n+B::n;}',
        'constexpr': 'constexpr int f(){return 3;}int value=f();int main(){return ++value-4;}',
        'alias': 'int value;int&f(){return value;}int main(){f()=4;return value-4;}',
        'pointer': 'int value;int*f(){return &value;}int main(){*f()=5;return value-5;}',
        'default-reference': 'int value;int f(int&v=value){return ++v;}int main(){return f()-1;}',
        'empty-destructor': 'int count;struct E{~E(){++count;}};int main(){{E e;}return count-1;}',
        'wide': 'unsigned long long value=18446744073709551615ULL;int main(){++value;return value!=0;}',
        'character': "char value='a';int main(){value='b';return value!='b';}",
    }
    for name, source in mutable_global_positive.items():
        check("v2-mutable-global-positive-" + name, source, profile="cpp-core-v2")
    mutable_global_reject = {
        'dynamic-call': 'int f(){return 1;}int value=f();',
        'dynamic-read': 'int a=1;int b=a;',
        'dynamic-effect': 'int a=1;int b=++a;',
        'pointer': 'int*value=nullptr;',
        'reference': 'int n;int&value=n;',
        'record': 'struct R{int n;};R value{1};',
        'array': 'int value[2]={1,2};',
        'float': 'double value=1.0;',
        'volatile': 'volatile int value;',
        'atomic': '_Atomic(int) value;',
        'tls': 'thread_local int value;',
        'folded-unsupported': 'int value=static_cast<int>(1.0);',
        'unused-folded-unsupported': 'constexpr int f(){return static_cast<int>(1.0);}int value=f();',
        'variable-template': 'template<class T> int value=1;',
    }
    for name, source in mutable_global_reject.items():
        check("v2-mutable-global-reject-" + name, source, 'TR0201', profile="cpp-core-v2")
    mutable_global_invalid = {
        'duplicate': 'int value=1;int value=2;',
        'conflicting': 'extern int value;bool value;',
        'const-write': 'const int value=1;int main(){value=2;}',
        'narrow-list': 'unsigned char value{300};',
    }
    for name, source in mutable_global_invalid.items():
        check("v2-mutable-global-invalid-" + name, source, 'TR0202', profile="cpp-core-v2")
    mutable_global_missing = {
        'external': 'extern int value;int main(){return value;}',
        'unused-external': 'extern int value;int main(){}',
    }
    for name, source in mutable_global_missing.items():
        check("v2-mutable-global-missing-" + name, source, 'TR0203', profile="cpp-core-v2")
    check("v1-mutable-global-zero", "int value;int main(){return value;}", "TR0201")
    check("v1-mutable-global-write", "int value=1;int main(){return ++value;}", "TR0201")

    range_for_source = """int tick(int v){return v;}
struct Element {
 int n;
 Element(int v):n(v){}
 Element(const Element&e):n(e.n){}
 ~Element(){tick(n);}
};
struct Body {
 Body(){tick(1);}
 ~Body(){tick(2);}
};
struct Iterator {
 int*p;
 Iterator(int*q):p(q){}
 ~Iterator(){tick(3);}
 Iterator&operator++(){++p;return *this;}
 Element operator*(){return Element(*p);}
 bool operator!=(const Iterator&r)const{return p!=r.p;}
};
struct Sentinel {
 int*p;
 Sentinel(int*q):p(q){}
 ~Sentinel(){tick(4);}
};
bool operator!=(const Iterator&i,const Sentinel&s){return i.p!=s.p;}
struct Range {
 int a[2];
 Range():a{1,2}{}
 ~Range(){tick(5);}
 Iterator begin(){return Iterator(a);}
 Iterator end(){return Iterator(a+2);}
};
Range make(){return Range();}
struct Different {
 int a[2];
 Iterator begin() {return Iterator(a);}
 Sentinel end(){return Sentinel(a+2);}
};
void normal(){for(auto&&e:make()){Body b;tick(e.n);}}
void breakPath(){for(auto&&e:make()){Body b;if(e.n)break;tick(e.n);}}
void continuePath(){for(auto&&e:make()){Body b;if(e.n)continue;tick(e.n);}}
int returnPath(){for(auto&&e:make()){Body b;if(e.n)return e.n;tick(e.n);}return 0;}
void copied(){Element items[2]={Element(1),Element(2)};for(Element e:items)tick(e.n);}
void aliases(){int a[2]={1,2};for(int&v:a)++v;}
void different(){for(auto&&e:Different{{1,2}})tick(e.n);}
using Array=int[2];
void arrayTemporary(){for(int v:Array{1,2})tick(v);}
void memberTemporary(){for(int v:Range().a)tick(v);}
namespace Adl {
 struct R{int a[2];};
 int*begin(R&r){return r.a;}
 int*end(R&r){return r.a+2;}
}
void adl(){for(int v:Adl::R{{1,2}})tick(v);}
"""
    range_for = check("v2-range-for-protocol", range_for_source, profile="cpp-core-v2")
    rf_functions = {f["name"]: f for f in range_for["functions"]}

    def rf_line(prefix):
        found = [i for i, line in enumerate(range_for_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0]

    def rf_record(prefix):
        found = [r["id"] for r in range_for["records"] if r["loc"]["line"] == rf_line(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0]

    def rf_function(prefix, result, parameters):
        found = [f for f in range_for["functions"] if f["loc"]["line"] == rf_line(prefix)
                 and f["result"] == result and [p["type"] for p in f["params"]] == parameters]
        assert len(found) == 1, (prefix, result, parameters, found)
        return found[0]

    rf_element = rf_record("struct Element {")
    rf_body = rf_record("struct Body {")
    rf_iterator = rf_record("struct Iterator {")
    rf_sentinel = rf_record("struct Sentinel {")
    rf_range = rf_record("struct Range {")
    rf_different = rf_record("struct Different {")
    rf_adl = rf_record(" struct R{")
    rf_tick = rf_function("int tick(", "int", ["int"])["name"]
    rf_make = rf_function("Range make(", "void", ["ptr:"+rf_range])["name"]
    rf_begin = rf_function(" Iterator begin(){return Iterator(a);}", "void", ["ptr:"+rf_iterator, "ptr:"+rf_range])["name"]
    rf_end = rf_function(" Iterator end(){return Iterator(a+2);}", "void", ["ptr:"+rf_iterator, "ptr:"+rf_range])["name"]
    rf_increment = rf_function(" Iterator&operator++(", "ptr:"+rf_iterator, ["ptr:"+rf_iterator])["name"]
    rf_dereference = rf_function(" Element operator*(", "void", ["ptr:"+rf_element, "ptr:"+rf_iterator])["name"]
    rf_compare = rf_function(" bool operator!=(", "bool", ["cptr:"+rf_iterator, "cptr:"+rf_iterator])["name"]
    rf_body_ctor = rf_function(" Body(){", "void", ["ptr:"+rf_body])["name"]
    rf_element_ctor = rf_function(" Element(int", "void", ["ptr:"+rf_element, "int"])["name"]
    rf_element_copy = rf_function(" Element(const", "void", ["ptr:"+rf_element, "cptr:"+rf_element])["name"]
    rf_element_dtor, rf_body_dtor = rf_element+"_destroy", rf_body+"_destroy"
    rf_iterator_dtor, rf_range_dtor = rf_iterator+"_destroy", rf_range+"_destroy"
    normal = rf_function("void normal(", "void", [])
    normal_calls = gc_calls(normal)
    normal_order = [rf_make, rf_begin, rf_end, rf_compare, rf_dereference, rf_body_ctor,
                    rf_tick, rf_body_dtor, rf_element_dtor, rf_increment,
                    rf_iterator_dtor, rf_iterator_dtor, rf_range_dtor]
    assert [c["callee"] for c in normal_calls] == normal_order, normal
    made, begun, ended, compared, dereferenced, body_constructed = normal_calls[:6]
    range_place = ve_pointer(normal, made["args"][0])
    begin_place, end_place = [ve_pointer(normal, c["args"][0]) for c in (begun, ended)]
    assert begin_place != end_place
    assert [ve_pointer(normal, c["args"][1]) for c in (begun, ended)] == [range_place]*2
    assert [ve_pointer(normal, a) for a in compared["args"]] == [begin_place, end_place]
    assert ve_pointer(normal, dereferenced["args"][1]) == begin_place
    assert ve_pointer(normal, normal_calls[9]["args"][0]) == begin_place
    assert [ve_pointer(normal, c["args"][0]) for c in normal_calls[10:]] == [end_place, begin_place, range_place]
    assert ve_pointer(normal, normal_calls[7]["args"][0]) == ve_pointer(normal, body_constructed["args"][0])
    assert ve_pointer(normal, normal_calls[8]["args"][0]) == ve_pointer(normal, dereferenced["args"][0])
    assert [sum(v["type"] == t for v in normal["locals"])
            for t in (rf_range, rf_iterator, rf_element, rf_body)] == [1, 2, 1, 1]

    # Explore one iteration and the exit using public IR control flow. Boolean
    # stores resolve cleanup guards; unknown source conditions explore both arms.
    # This checks effects on abrupt edges as well as the normal instruction list.
    def rf_paths(function):
        body = function["body"]
        labels = {n["label"]: i for i, n in enumerate(body) if n["op"] == "label"}
        def known(expr, values):
            if expr["kind"] == "literal" and expr["type"] == "bool":
                return expr["value"]
            if expr["kind"] == "cast":
                return known(expr["args"][0], values)
            if expr["kind"] == "var":
                return values.get(expr["name"])
            return None
        work, paths = [(0, {}, (), 0, 0)], set()
        while work:
            pc, values, calls, comparisons, steps = work.pop()
            assert steps < len(body)*4, (function["name"], pc, calls)
            node = body[pc]
            op = node["op"]
            if op == "return":
                paths.add(calls)
                continue
            if op == "assign" and node["target"]["kind"] == "var":
                values = dict(values, **{node["target"]["name"]: known(node["value"], values)})
            if op == "call":
                calls += (node["callee"],)
                target_value = None
                if node["callee"] == rf_compare:
                    comparisons += 1
                    assert comparisons <= 2, calls
                    target_value = comparisons == 1
                if "target" in node:
                    values = dict(values, **{node["target"]["name"]: target_value})
            if op == "branch":
                condition = known(node["condition"], values)
                choices = (True, False) if condition is None else (condition,)
                for choice in choices:
                    assert isinstance(choice, bool), (node, choice)
                    work.append((labels[node["true" if choice else "false"]], values,
                                 calls, comparisons, steps+1))
            else:
                work.append((labels[node["label"]] if op == "jump" else pc+1,
                             values, calls, comparisons, steps+1))
        return paths

    prefix = (rf_make, rf_begin, rf_end, rf_compare, rf_dereference, rf_body_ctor)
    iteration_cleanup = (rf_body_dtor, rf_element_dtor)
    range_cleanup = (rf_iterator_dtor, rf_iterator_dtor, rf_range_dtor)
    normal_path = prefix+(rf_tick,)+iteration_cleanup+(rf_increment, rf_compare)+range_cleanup
    assert rf_paths(normal) == {normal_path}
    assert rf_paths(rf_function("void breakPath(", "void", [])) == {
        normal_path, prefix+iteration_cleanup+range_cleanup}
    assert rf_paths(rf_function("void continuePath(", "void", [])) == {
        normal_path, prefix+iteration_cleanup+(rf_increment, rf_compare)+range_cleanup}
    assert rf_paths(rf_function("int returnPath(", "int", [])) == {
        normal_path, prefix+iteration_cleanup+range_cleanup}

    copied = rf_function("void copied(", "void", [])
    copied_calls = gc_calls(copied)
    assert [c["callee"] for c in copied_calls] == [rf_element_ctor]*2+[rf_element_copy, rf_tick]+[rf_element_dtor]*3
    copy_place = ve_pointer(copied, copied_calls[2]["args"][0])
    assert ve_pointer(copied, copied_calls[4]["args"][0]) == copy_place
    arrays = [v for v in copied["locals"] if v["type"] == "arr:2:"+rf_element]
    assert len(arrays) == 1 and sum(v["type"] == rf_element for v in copied["locals"]) == 1
    aliases = rf_function("void aliases(", "void", [])
    assert not gc_calls(aliases)
    assert any(n["op"] == "assign" and n["target"]["kind"] == "dereference"
               and n["target"]["type"] == "int" for n in aliases["body"])
    different = rf_function("void different(", "void", [])
    different_begin = rf_function(" Iterator begin() {", "void", ["ptr:"+rf_iterator, "ptr:"+rf_different])["name"]
    different_end = rf_function(" Sentinel end(){", "void", ["ptr:"+rf_sentinel, "ptr:"+rf_different])["name"]
    different_compare = rf_function("bool operator!=(", "bool", ["cptr:"+rf_iterator, "cptr:"+rf_sentinel])["name"]
    assert [c["callee"] for c in gc_calls(different)] == [different_begin, different_end,
        different_compare, rf_dereference, rf_tick, rf_element_dtor, rf_increment,
        rf_sentinel+"_destroy", rf_iterator_dtor]
    array_temporary = rf_function("void arrayTemporary(", "void", [])
    assert [c["callee"] for c in gc_calls(array_temporary)] == [rf_tick]
    assert sum(v["type"] == "arr:2:int" for v in array_temporary["locals"]) == 1
    range_ctor = rf_function(" Range():", "void", ["ptr:"+rf_range])["name"]
    member_temporary = rf_function("void memberTemporary(", "void", [])
    assert [c["callee"] for c in gc_calls(member_temporary)] == [range_ctor, rf_tick, rf_range_dtor]
    adl_begin = rf_function(" int*begin(R&", "ptr:int", ["ptr:"+rf_adl])["name"]
    adl_end = rf_function(" int*end(R&", "ptr:int", ["ptr:"+rf_adl])["name"]
    assert [c["callee"] for c in gc_calls(rf_function("void adl(", "void", []))] == [adl_begin, adl_end, rf_tick]
    for function in range_for["functions"]:
        for call in gc_calls(function):
            assert [a["type"] for a in call["args"]] == [p["type"] for p in rf_functions[call["callee"]]["params"]]
    with tempfile.TemporaryDirectory(prefix="neverc-range-for-relocated-") as temp:
        relocated = check("v2-range-for-relocated", range_for_source,
                          root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == range_for, "range-for identities depend on the absolute root"

    range_for_positive = {
        'array-value': 'int main(){int a[2]={1,2},n=0;for(int v:a)n+=v;return n-3;}',
        'array-reference': 'int main(){int a[2]={1,2};for(int&v:a)++v;return a[0]+a[1]-5;}',
        'array-forward-reference': 'int main(){int a[2]={1,2};for(auto&&v:a)++v;return a[0]-2;}',
        'const-array': 'int main(){const int a[2]={1,2};int n=0;for(const auto&v:a)n+=v;return n-3;}',
        'enum-array': 'enum class E:unsigned char{a=1,b=2};int main(){E a[2]={E::a,E::b};int n=0;for(E e:a)n+=static_cast<int>(e);return n-3;}',
        'multidimensional': 'int main(){int a[2][2]={{1,2},{3,4}},n=0;for(auto&row:a)for(int&v:row)n+=v;return n-10;}',
        'array-temporary': 'using A=int[2];int main(){int n=0;for(int v:A{1,2})n+=v;return n-3;}',
        'record-member-temporary': 'struct R{int a[2];~R(){}};int main(){int n=0;for(int v:R{{1,2}}.a)n+=v;return n-3;}',
        'member-range': 'struct R{int a[2];int*begin(){return a;}int*end(){return a+2;}};int main(){int n=0;for(int v:R{{1,2}})n+=v;return n-3;}',
        'const-member-range': 'struct R{int a[2];const int*begin()const{return a;}const int*end()const{return a+2;}};int main(){const R r{{1,2}};int n=0;for(int v:r)n+=v;return n-3;}',
        'adl-range': 'namespace N{struct R{int a[2];};int*begin(R&r){return r.a;}int*end(R&r){return r.a+2;}}int main(){int n=0;for(int v:N::R{{1,2}})n+=v;return n-3;}',
        'adl-single-member-name': 'namespace N{struct R{int a[2];int begin;};int*begin(R&r){return r.a;}int*end(R&r){return r.a+2;}}int main(){int n=0;for(int v:N::R{{1,2},0})n+=v;return n-3;}',
        'defaulted-begin-end': 'int count;int next(){return ++count;}struct R{int a[2];int*begin(int n=next()){return a;}int*end(int n=next()){return a+2;}};int main(){for(int v:R{{1,2}}){}return count-2;}',
        'member-body-this': 'struct R{int a[2];int sum()const{int n=0;for(int v:a)n+=v;return n;}};int main(){return R{{1,2}}.sum()-3;}',
        'constructor-body': 'struct R{int n;R(){n=0;int a[2]={1,2};for(int v:a)n+=v;}};int main(){return R().n-3;}',
        'destructor-body': 'int count;struct R{int a[2];~R(){for(int v:a)count+=v;}};int main(){{R r{{1,2}};}return count-3;}',
        'constexpr-body': 'constexpr int sum(){int a[2]={1,2},n=0;for(int v:a)n+=v;return n;}static_assert(sum()==3);int main(){return sum()-3;}',
        'nested-switch': 'int main(){int a[2]={1,2},n=0;for(int v:a){switch(v){case 1:continue;default:break;}n+=v;}return n-2;}',
        'empty-range': 'struct R{int*b;int*e;int*begin(){return b;}int*end(){return e;}};int main(){for(int v:R{nullptr,nullptr})return 1;return 0;}',
        'user-loop-names': 'int main(){int range=4,begin=5,end=6,a[1]={1};for(int v:a)range+=v;return range+begin+end-16;}',
        'record-iterator': 'struct I{int*p;I&operator++(){++p;return *this;}int&operator*(){return *p;}bool operator!=(const I&r)const{return p!=r.p;}};struct R{int a[2];I begin(){return {a};}I end(){return {a+2};}};int main(){int n=0;for(int v:R{{1,2}})n+=v;return n-3;}',
        'different-sentinel': 'struct S{int*p;};struct I{int*p;I&operator++(){++p;return *this;}int&operator*(){return *p;}bool operator!=(const I&r)const{return p!=r.p;}};bool operator!=(const I&i,const S&s){return i.p!=s.p;}struct R{int a[2];I begin(){return {a};}S end(){return {a+2};}};int main(){int n=0;for(int v:R{{1,2}})n+=v;return n-3;}',
        'copied-element': 'struct E{int n;E(int v):n(v){}E(const E&e):n(e.n){}~E(){}};int main(){E a[2]={E(1),E(2)};int n=0;for(E v:a)n+=v.n;return n-3;}',
        'prvalue-element-reference': 'struct E{int n;~E(){}};struct I{int*p;I&operator++(){++p;return *this;}E operator*(){return {*p};}bool operator!=(const I&r)const{return p!=r.p;}};struct R{int a[2];I begin(){return {a};}I end(){return {a+2};}};int main(){int n=0;for(auto&&v:R{{1,2}})n+=v.n;return n-3;}',
        'prvalue-element-value': 'struct E{int n;~E(){}};struct I{int*p;I&operator++(){++p;return *this;}E operator*(){return {*p};}bool operator!=(const I&r)const{return p!=r.p;}};struct R{int a[2];I begin(){return {a};}I end(){return {a+2};}};int main(){int n=0;for(E v:R{{1,2}})n+=v.n;return n-3;}',
    }
    for name, source in range_for_positive.items():
        check("v2-range-for-positive-" + name, source, profile="cpp-core-v2")
    range_for_reject = {
        'cxx20-init-statement': 'void f(){for(int a[1]={1};int v:a){}}',
        'floating-range': 'void f(){double a[1]={1.0};for(auto v:a){}}',
        'volatile-range': 'void f(){volatile int a[1]={1};for(auto&v:a){}}',
        'structured-binding': 'struct R{int a,b;};void f(){R a[1]={{1,2}};for(auto [x,y]:a){}}',
        'unused-floating-body': 'void f(){int a[1]={1};for(int v:a){double unused=1.0;}}',
        'dead-floating-body': 'void f(){int a[1]={1};if(false)for(int v:a){double unused=1.0;}}',
        'floating-begin-body': 'struct R{int a[1];int*begin(){double v=1.0;return a;}int*end(){return a+1;}};void f(){for(int v:R{{1}}){}}',
        'floating-begin-default': 'struct R{int a[1];int*begin(double v=1.0){return a;}int*end(){return a+1;}};void f(){for(int v:R{{1}}){}}',
        'floating-comparison-body': 'struct I{int*p;int&operator*(){return *p;}I&operator++(){++p;return *this;}bool operator!=(const I&r)const{double v=1.0;return p!=r.p;}};struct R{int a[1];I begin(){return {a};}I end(){return {a+1};}};void f(){for(int v:R{{1}}){}}',
        'floating-dereference-body': 'struct I{int*p;int&operator*(){double v=1.0;return *p;}I&operator++(){++p;return *this;}bool operator!=(const I&r)const{return p!=r.p;}};struct R{int a[1];I begin(){return {a};}I end(){return {a+1};}};void f(){for(int v:R{{1}}){}}',
        'floating-increment-body': 'struct I{int*p;int&operator*(){return *p;}I&operator++(){double v=1.0;++p;return *this;}bool operator!=(const I&r)const{return p!=r.p;}};struct R{int a[1];I begin(){return {a};}I end(){return {a+1};}};void f(){for(int v:R{{1}}){}}',
        'inherited-range': 'struct B{int a[1];int*begin(){return a;}int*end(){return a+1;}};struct R:B{};void f(){R r;for(int v:r){}}',
        'resource-range': 'void f(){int a[65537]={};for(int v:a){}}',
    }
    for name, source in range_for_reject.items():
        check("v2-range-for-reject-" + name, source, 'TR0201', profile="cpp-core-v2")
    range_for_invalid = {
        'static-loop-variable': 'void f(){int a[1]={1};for(static int v:a){}}',
        'pointer-range': 'void f(int*p){for(int v:p){}}',
        'missing-begin': 'struct R{};void f(){for(int v:R{}){}}',
        'both-member-names': 'namespace N{struct R{int begin,end;};int*begin(R&r){return &r.begin;}int*end(R&r){return &r.end;}}void f(){for(int v:N::R{1,2}){}}',
        'ordinary-lookup-not-adl': 'namespace N{struct R{int a[1];};}int*begin(N::R&r){return r.a;}int*end(N::R&r){return r.a+1;}void f(){for(int v:N::R{{1}}){}}',
        'const-element-write': 'void f(){const int a[1]={1};for(int&v:a){}}',
        'prvalue-nonconst-lvalue': 'struct E{int n;~E(){}};struct I{int*p;I&operator++(){++p;return *this;}E operator*(){return {*p};}bool operator!=(const I&r)const{return p!=r.p;}};struct R{int a[2];I begin(){return {a};}I end(){return {a+2};}};void f(){for(E&v:R{{1,2}}){}}',
        'deleted-begin': 'struct R{int*begin()=delete;int*end(){return nullptr;}};void f(){for(int v:R{}){}}',
        'missing-increment': 'struct I{int*p;int&operator*(){return *p;}bool operator!=(const I&r)const{return p!=r.p;}};struct R{int a[1];I begin(){return {a};}I end(){return {a+1};}};void f(){for(int v:R{{1}}){}}',
    }
    for name, source in range_for_invalid.items():
        check("v2-range-for-invalid-" + name, source, 'TR0202', profile="cpp-core-v2")
    range_for_missing = {
        'external-begin': 'struct R{int a[1];int*begin();int*end(){return a+1;}};void f(){for(int v:R{{1}}){}}',
        'external-end': 'struct R{int a[1];int*begin(){return a;}int*end();};void f(){for(int v:R{{1}}){}}',
        'external-increment': 'struct I{int*p;int&operator*(){return *p;}I&operator++();bool operator!=(const I&r)const{return p!=r.p;}};struct R{int a[1];I begin(){return {a};}I end(){return {a+1};}};void f(){for(int v:R{{1}}){}}',
    }
    for name, source in range_for_missing.items():
        check("v2-range-for-missing-" + name, source, 'TR0203', profile="cpp-core-v2")
    check("v1-range-for-array", "int main(){int a[1]={1};for(int v:a){}return 0;}", "TR0201")
    check("v1-range-for-record", "struct R{int a[1];int*begin(){return a;}int*end(){return a+1;}};int main(){for(int v:R{{1}}){}return 0;}", "TR0201")

    nonpublic_source = """int tick(int n){return n;}
class Private {
 unsigned char tag;
 int n;
 int values[2];
public:
 Private(int v):tag(1),n(v),values{v,v+1}{}
 int get()const{return n;}
 void set(int v){n=v;}
 int&ref(){return n;}
 const int&ref()const{return n;}
 int*data(){return values;}
 int sum()const{int result=0;for(int v:values)result+=v;return result;}
};
struct Twin {unsigned char tag;int n;int values[2];};
struct Protected {
protected:int n;
public:
 Protected(int v):n(v){}
 int read()const{return n;}
};
class Factory {
 int n;
 Factory(int v):n(v){}
 Factory(const Factory&r):n(r.n){}
public:
 static Factory make(int v){return Factory(v);}
 static Factory clone(const Factory&r){return Factory(r);}
 int value()const{return n;}
};
class Token {
 int n;
public:
 Token(int v):n(v){}
 Token(const Token&r):n(r.n){}
 ~Token(){tick(n);}
};
class Box {
 Token items[2];
public:
 Box():items{Token(1),Token(2)}{}
 Box(const Box&)=default;
 ~Box()=default;
};
void use(){Private p(3);p.set(4);tick(p.get());p.ref()=5;tick(p.sum());}
void factories(){Factory a=Factory::make(3);Factory b=Factory::clone(a);tick(b.value());}
void boxes(){Box a;Box b=a;}
"""
    nonpublic = check("v2-nonpublic-fields-protocol", nonpublic_source, profile="cpp-core-v2")
    np_functions = {f["name"]: f for f in nonpublic["functions"]}

    def np_line(prefix):
        found = [i for i, line in enumerate(nonpublic_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0]

    def np_record(prefix):
        found = [r for r in nonpublic["records"] if r["loc"]["line"] == np_line(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0]

    def np_function(prefix, result, parameters):
        found = [f for f in nonpublic["functions"] if f["loc"]["line"] == np_line(prefix)
                 and f["result"] == result and [p["type"] for p in f["params"]] == parameters]
        assert len(found) == 1, (prefix, result, parameters, found)
        return found[0]

    def np_pointer(function, expr):
        if expr["kind"] == "cast":
            return np_pointer(function, expr["args"][0])
        if expr["kind"] in ("address", "array_decay"):
            return np_place(function, expr["args"][0])
        assert expr["kind"] == "var", expr
        if any(p["name"] == expr["name"] for p in function["params"]):
            return ("parameter", expr["name"])
        values = [n["value"] for n in function["body"] if n["op"] == "assign"
                  and n["target"].get("kind") == "var" and n["target"]["name"] == expr["name"]]
        assert len(values) == 1, (expr, values)
        return np_pointer(function, values[0])

    def np_place(function, expr):
        kind = expr["kind"]
        if kind == "var":
            return ("object", expr["name"])
        if kind == "dereference":
            return np_pointer(function, expr["args"][0])
        if kind == "member":
            return ("field", np_place(function, expr["args"][0]), expr["name"])
        assert kind == "index", expr
        return ("element", np_pointer(function, expr["args"][0]), gc_identity(function, expr["args"][1]))

    private = np_record("class Private {")
    twin = np_record("struct Twin {")
    protected = np_record("struct Protected {")
    factory = np_record("class Factory {")
    token = np_record("class Token {")
    box = np_record("class Box {")
    pid, fid, tid, bid = [r["id"] for r in (private, factory, token, box)]
    assert [f["type"] for f in private["fields"]] == ["u8", "int", "arr:2:int"]
    assert [f["type"] for f in twin["fields"]] == [f["type"] for f in private["fields"]]
    assert private["layout"] == twin["layout"]
    assert all("access" not in f for r in nonpublic["records"] for f in r["fields"])
    member = private["fields"][1]["name"]
    ctor = np_function(" Private(int", "void", ["ptr:"+pid, "int"])
    written = {n["target"]["name"] for n in ctor["body"] if n["op"] == "assign" and n["target"]["kind"] == "member"}
    assert {f["name"] for f in private["fields"][:2]} <= written
    for prefix, result, params in ((" int get()", "int", ["cptr:"+pid]),
                                  (" void set(", "void", ["ptr:"+pid, "int"]),
                                  (" int&ref()", "ptr:int", ["ptr:"+pid]),
                                  (" const int&ref()", "cptr:int", ["cptr:"+pid])):
        function = np_function(prefix, result, params)
        accesses = [n for n in walk(function["body"]) if n.get("kind") == "member"]
        assert accesses and all(n["name"] == member for n in accesses), function
        expected = ("field", ("parameter", function["params"][0]["name"]), member)
        assert all(np_place(function, n) == expected for n in accesses)
        assert not gc_calls(function)
        if result in ("ptr:int", "cptr:int"):
            returned = [n["value"] for n in function["body"] if n["op"] == "return"]
            assert len(returned) == 1 and np_pointer(function, returned[0]) == expected
    setter = np_function(" void set(", "void", ["ptr:"+pid, "int"])
    assert any(n["op"] == "assign" and n["target"].get("name") == member for n in setter["body"])
    sum_function = np_function(" int sum()", "int", ["cptr:"+pid])
    assert not gc_calls(sum_function)
    assert any(n.get("kind") == "member" and n["name"] == private["fields"][2]["name"] for n in walk(sum_function["body"]))
    assert any(n["op"] == "branch" for n in sum_function["body"])
    read = np_function(" int read()", "int", ["cptr:"+protected["id"]])
    assert any(n.get("kind") == "member" and n["name"] == protected["fields"][0]["name"] for n in walk(read["body"]))
    factory_ctor = np_function(" Factory(int", "void", ["ptr:"+fid, "int"])["name"]
    factory_copy = np_function(" Factory(const", "void", ["ptr:"+fid, "cptr:"+fid])["name"]
    for prefix, params, selected in ((" static Factory make(", ["ptr:"+fid, "int"], factory_ctor),
                                     (" static Factory clone(", ["ptr:"+fid, "cptr:"+fid], factory_copy)):
        function = np_function(prefix, "void", params)
        calls = gc_calls(function)
        assert [c["callee"] for c in calls] == [selected]
        assert np_pointer(function, calls[0]["args"][0]) == ("parameter", function["params"][0]["name"])
        assert not any(v["type"] == fid for v in function["locals"])
    token_copy = np_function(" Token(const", "void", ["ptr:"+tid, "cptr:"+tid])["name"]
    box_copy = np_function(" Box(const", "void", ["ptr:"+bid, "cptr:"+bid])
    copies = gc_calls(box_copy)
    assert [c["callee"] for c in copies] == [token_copy]*2
    field = box["fields"][0]["name"]
    for i, call in enumerate(copies):
        for arg, parameter in zip(call["args"], box_copy["params"]):
            assert np_pointer(box_copy, arg) == ("element", ("field", ("parameter", parameter["name"]), field), i)
    destructor = np_functions[bid+"_destroy"]
    assert [c["callee"] for c in gc_calls(destructor)] == [tid+"_destroy"]*2
    owner = ("field", ("parameter", destructor["params"][0]["name"]), field)
    assert [np_pointer(destructor, c["args"][0]) for c in gc_calls(destructor)] == [("element", owner, i) for i in (1, 0)]
    for function in nonpublic["functions"]:
        for call in gc_calls(function):
            assert [a["type"] for a in call["args"]] == [p["type"] for p in np_functions[call["callee"]]["params"]]
    with tempfile.TemporaryDirectory(prefix="neverc-nonpublic-fields-relocated-") as temp:
        relocated = check("v2-nonpublic-fields-relocated", nonpublic_source,
                          root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == nonpublic, "field identities depend on the absolute root"

    nonpublic_positive = {
        'class-default-private': 'class R{int n;public:R(int v):n(v){}int get()const{return n;}};int main(){return R(3).get()-3;}',
        'explicit-private': 'struct R{private:int n;public:R(int v):n(v){}int get()const{return n;}};int main(){return R(3).get()-3;}',
        'protected-data': 'struct R{protected:int n;public:R(int v):n(v){}int get()const{return n;}};int main(){return R(3).get()-3;}',
        'private-helper': 'class R{int n;int impl()const{return n;}public:R(int v):n(v){}int get()const{return impl();}};int main(){return R(3).get()-3;}',
        'private-factory': 'class R{int n;R(int v):n(v){}public:static R make(int n){return R(n);}int get()const{return n;}};int main(){return R::make(3).get()-3;}',
        'private-copy': 'class R{int n;R(const R&r):n(r.n){}public:R(int v):n(v){}static R clone(const R&r){return R(r);}int get()const{return n;}};int main(){R r(3);return R::clone(r).get()-3;}',
        'private-default-name': 'class R{static int seed(){return 3;}int n;public:R(int v=seed()):n(v){}int get()const{return n;}};int main(){return R().get()-3;}',
        'private-pointer': 'class R{int n;public:R():n(3){}int*data(){return &n;}const int*data()const{return &n;}};int main(){R r;*r.data()=4;const R&v=r;return *v.data()-4;}',
        'private-array-range': 'class R{int a[2]={1,2};public:int sum()const{int n=0;for(int v:a)n+=v;return n;}};int main(){return R().sum()-3;}',
        'private-member-cleanup': 'int n;struct V{int v;~V(){n+=v;}};class R{V a[2];public:R():a{{1},{2}}{}};int main(){{R r;}return n-3;}',
        'private-qualified-definition': 'class R{int n;int impl()const;public:R(int);int get()const;};R::R(int v):n(v){}int R::impl()const{return n;}int R::get()const{return impl();}int main(){return R(3).get()-3;}',
        'private-same-access-sections': 'class R{int a=1;public:int get()const{return a+b;}private:int b=2;};int main(){return R().get()-3;}',
        'promoted-1-private-field': 'class R{int n;public:R(R&&)=default;};',
        'promoted-2-private-field': 'class R{int n=1;};',
        'promoted-3-private-field': 'class R{int n;public:R&operator=(const R&)=default;};',
        'promoted-4-nonpublic-field': 'class R{int n;public:R(const R&)=default;};',
        'promoted-5-nonpublic-field': 'class R{int n;public:R()=default;};',
        'promoted-6-private-field': 'class R{int n;public:R():n(1){}};',
        'promoted-7-protected-field': 'struct R{protected:int n;public:R():n(1){}};',
    }
    for name, source in nonpublic_positive.items():
        check("v2-nonpublic-fields-positive-" + name, source, profile="cpp-core-v2")
    nonpublic_reject = {
        'mixed-access': 'struct R{int a;private:int b;public:R():a(1),b(2){}int get(){return a+b;}};',
        'inheritance': 'class R{protected:int n=1;};class D:public R{public:int get(){return n;}};',
        'const-field': 'class R{const int n=1;public:int get()const{return n;}};',
        'reference-field': 'class R{int&n;public:R(int&v):n(v){}};',
        'mutable-field': 'class R{mutable int n=1;public:int get()const{return ++n;}};',
        'bitfield': 'class R{unsigned int n:2;public:R():n(1){}};',
        'floating-field': 'class R{double n=1.0;};',
        'unused-floating-helper': 'class R{int n=1;double hidden(){return 1.0;}public:int get()const{return n;}};',
        'pointer-to-member': 'class R{int n=1;public:static int R::*field(){return &R::n;}};',
    }
    for name, source in nonpublic_reject.items():
        check("v2-nonpublic-fields-reject-" + name, source, 'TR0201', profile="cpp-core-v2")
    nonpublic_invalid = {
        'private-read': 'class R{int n=1;};int f(const R&r){return r.n;}',
        'private-write': 'class R{int n=1;};void f(R&r){r.n=3;}',
        'private-address': 'class R{int n=1;};int*f(R&r){return &r.n;}',
        'protected-read': 'struct R{protected:int n=1;};int f(const R&r){return r.n;}',
        'private-constructor': 'class R{int n;R():n(1){}};void f(){R r;}',
        'private-method': 'class R{int n=1;int get()const{return n;}};int f(const R&r){return r.get();}',
        'private-copy': 'class R{int n;R(const R&r):n(r.n){}public:R(int v):n(v){}};void f(){R a(1);R b=a;}',
        'private-destructor': 'class R{int n=1;~R(){}};void f(){R r;}',
        'private-range-begin': 'class R{int a[1]={1};int*begin(){return a;}public:int*end(){return a+1;}};void f(){R r;for(int v:r){}}',
        'private-range-end': 'class R{int a[1]={1};int*end(){return a+1;}public:int*begin(){return a;}};void f(){R r;for(int v:r){}}',
        'private-aggregate-initializer': 'class R{int n;};void f(){R r{1};}',
        'private-unevaluated': 'class R{int n=1;};bool f(const R&r){return noexcept(r.n);}',
    }
    for name, source in nonpublic_invalid.items():
        check("v2-nonpublic-fields-invalid-" + name, source, 'TR0202', profile="cpp-core-v2")
    nonpublic_missing = {
    }
    for name, source in nonpublic_missing.items():
        check("v2-nonpublic-fields-missing-" + name, source, 'TR0203', profile="cpp-core-v2")
    check("v1-nonpublic-fields-array", "class R{int n=1;};int main(){return 0;}", "TR0201")
    check("v1-nonpublic-fields-record", "class R{int n;public:R():n(1){}};int main(){R r;return 0;}", "TR0201")

    folded_void_source = """static_assert((void(0),true));
enum E:int{value=(void(0),3)};
int touch(int&n){return ++n;}
int selected(int n){switch(n){case (void(0),1):return 7;default:return 9;}}
int queried(int&n){return sizeof((void(touch(n)),1));}
void evaluated(int&n){(void(touch(n)));}
int folded(){return value;}
"""
    folded_void = check("v2-folded-void-protocol", folded_void_source, profile="cpp-core-v2")

    def fv_function(prefix, result, parameters):
        lines = [i for i, line in enumerate(folded_void_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(lines) == 1, (prefix, lines)
        found = [f for f in folded_void["functions"] if f["loc"]["line"] == lines[0]
                 and f["result"] == result and [p["type"] for p in f["params"]] == parameters]
        assert len(found) == 1, (prefix, result, parameters, found)
        return found[0]

    touch = fv_function("int touch(", "int", ["ptr:int"])["name"]
    for prefix, parameters, value in (("int queried(", ["ptr:int"], 4), ("int folded(", [], 3)):
        function = fv_function(prefix, "int", parameters)
        assert not gc_calls(function)
        returned = [n["value"] for n in function["body"] if n["op"] == "return"]
        assert len(returned) == 1 and gc_identity(function, returned[0]) == value
    evaluated = fv_function("void evaluated(", "void", ["ptr:int"])
    assert [c["callee"] for c in gc_calls(evaluated)] == [touch]
    selected = fv_function("int selected(", "int", ["int"])
    assert not gc_calls(selected)
    assert any(n["op"] == "branch" for n in selected["body"])
    assert {gc_identity(selected, n["value"]) for n in selected["body"] if n["op"] == "return"} == {7, 9}
    folded_void_positive = {
        'switch-folded-cast': 'int f(int n){switch(n){case (void(0),1):return 7;default:return 9;}}',
        'erased-size-operation': 'int main(){return sizeof((void(0),1));}',
        'folded-functional-void': 'static_assert((void(0),true)); int main(){}',
    }
    for name, source in folded_void_positive.items():
        check("v2-folded-void-positive-" + name, source, profile="cpp-core-v2")
    folded_void_reject = {
        'unsupported-void-sizeof': 'int f(){return sizeof((void(1.0),1));}',
        'unsupported-void-case': 'int f(int n){switch(n){case (void(1.0),1):return 7;default:return 9;}}',
        'unsupported-void-assert': 'static_assert((void(1.0),true));int main(){}',
    }
    for name, source in folded_void_reject.items():
        check("v2-folded-void-reject-" + name, source, 'TR0201', profile="cpp-core-v2")

    friends_source = """int tick(int n){return n;}
class R;
class Other;
class Reader {
public:int view(const R&)const;
};
class R {
 int n;
 friend class Inspector;
 friend int Reader::view(const R&)const;
 friend int together(const R&,const Other&);
public:
 R(int v):n(v){}
 friend int get(const R&r){return r.n;}
 friend int&ref(R&r){return r.n;}
 friend const int*data(const R&r){return &r.n;}
 friend int extra(const R&r,int v=4){return r.n+v;}
 friend int operator+(const R&r,int v){return r.n+v;}
 friend int operator+(int v,const R&r){return r.n+v;}
};
class Other {
 int n;
 friend int together(const R&,const Other&);
public:Other(int v):n(v){}
};
int together(const R&a,const Other&b){return a.n+b.n;}
class Inspector {
public:int inspect(const R&r)const{return r.n;}
};
int Reader::view(const R&r)const{return r.n;}
class Range {
 int a[2];
 friend int*begin(Range&r){return r.a;}
 friend int*end(Range&r){return r.a+2;}
public:Range():a{1,2}{}
};
class Factory {
 int n;
 Factory(int v):n(v){}
 Factory(const Factory&r):n(r.n){}
 friend Factory make(int);
 friend Factory clone(const Factory&);
public:int value()const{return n;}
};
Factory make(int v){return Factory(v);}
Factory clone(const Factory&r){return Factory(r);}
int inspectScoped(int);
class Scoped {
 int n;
 Scoped(int v):n(v){}
 ~Scoped(){tick(n);}
 friend int inspectScoped(int n){Scoped value(n);return value.n;}
};
void use(){R r(3);tick(get(r));ref(r)=5;tick(extra(r));tick(r+1);tick(2+r);}
void ranged(){for(int v:Range{})tick(v);}
void factories(){Factory a=make(3);Factory b=clone(a);tick(b.value());}
void members(){R r(3);Other other(4);Reader reader;Inspector inspector;tick(reader.view(r));tick(inspector.inspect(r));tick(together(r,other));}
"""
    friends = check("v2-friends-protocol", friends_source, profile="cpp-core-v2")
    fr_functions = {f["name"]: f for f in friends["functions"]}
    assert len(fr_functions) == len(friends["functions"]), "friend redeclarations duplicated definitions"

    def fr_line(prefix):
        found = [i for i, line in enumerate(friends_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0]

    def fr_record(prefix):
        found = [r for r in friends["records"] if r["loc"]["line"] == fr_line(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0]

    def fr_function(prefix, result, parameters):
        found = [f for f in friends["functions"] if f["loc"]["line"] == fr_line(prefix)
                 and f["result"] == result and [p["type"] for p in f["params"]] == parameters]
        assert len(found) == 1, (prefix, result, parameters, found)
        return found[0]

    record = fr_record("class R {")
    other = fr_record("class Other {")
    reader = fr_record("class Reader {")
    inspector = fr_record("class Inspector {")
    range_record = fr_record("class Range {")
    factory = fr_record("class Factory {")
    scoped = fr_record("class Scoped {")
    rid, field = record["id"], record["fields"][0]["name"]
    getter = fr_function(" friend int get(", "int", ["cptr:"+rid])
    reference = fr_function(" friend int&ref(", "ptr:int", ["ptr:"+rid])
    pointer = fr_function(" friend const int*data(", "cptr:int", ["cptr:"+rid])
    extra = fr_function(" friend int extra(", "int", ["cptr:"+rid, "int"])
    left_operator = fr_function(" friend int operator+(const", "int", ["cptr:"+rid, "int"])
    right_operator = fr_function(" friend int operator+(int", "int", ["int", "cptr:"+rid])
    assert left_operator["name"] != right_operator["name"]
    for function in (getter, reference, pointer):
        assert not gc_calls(function)
        accesses = [n for n in walk(function["body"]) if n.get("kind") == "member"]
        expected = ("field", ("parameter", function["params"][0]["name"]), field)
        assert accesses and all(np_place(function, n) == expected for n in accesses)
        if function["result"] in ("ptr:int", "cptr:int"):
            returned = [n["value"] for n in function["body"] if n["op"] == "return"]
            assert len(returned) == 1 and np_pointer(function, returned[0]) == expected
        assert not any(v["type"] == rid for v in function["locals"])
    view = fr_function("int Reader::view(", "int", ["cptr:"+reader["id"], "cptr:"+rid])
    inspect = fr_function("public:int inspect(", "int", ["cptr:"+inspector["id"], "cptr:"+rid])
    for function in (view, inspect):
        expected = ("field", ("parameter", function["params"][1]["name"]), field)
        accesses = [n for n in walk(function["body"]) if n.get("kind") == "member"]
        assert accesses and all(np_place(function, n) == expected for n in accesses)
    together = fr_function("int together(", "int", ["cptr:"+rid, "cptr:"+other["id"]])
    assert len([f for f in friends["functions"] if f["name"] == together["name"]]) == 1
    tick = fr_function("int tick(", "int", ["int"])["name"]
    ctor = fr_function(" R(int", "void", ["ptr:"+rid, "int"])["name"]
    use = fr_function("void use(", "void", [])
    calls = gc_calls(use)
    assert [c["callee"] for c in calls] == [ctor, getter["name"], tick, reference["name"],
        extra["name"], tick, left_operator["name"], tick, right_operator["name"], tick]
    destination = np_pointer(use, calls[0]["args"][0])
    for call_index, arg_index in ((1, 0), (3, 0), (4, 0), (6, 0), (8, 1)):
        assert np_pointer(use, calls[call_index]["args"][arg_index]) == destination
    assert gc_identity(use, calls[4]["args"][1]) == 4
    begin = fr_function(" friend int*begin(", "ptr:int", ["ptr:"+range_record["id"]])["name"]
    end = fr_function(" friend int*end(", "ptr:int", ["ptr:"+range_record["id"]])["name"]
    range_ctor = fr_function("public:Range():", "void", ["ptr:"+range_record["id"]])["name"]
    ranged = fr_function("void ranged(", "void", [])
    calls = gc_calls(ranged)
    assert [c["callee"] for c in calls] == [range_ctor, begin, end, tick]
    assert len({np_pointer(ranged, c["args"][0]) for c in calls[:3]}) == 1
    fid = factory["id"]
    factory_ctor = fr_function(" Factory(int", "void", ["ptr:"+fid, "int"])["name"]
    factory_copy = fr_function(" Factory(const", "void", ["ptr:"+fid, "cptr:"+fid])["name"]
    for prefix, parameters, selected in (("Factory make(", ["ptr:"+fid, "int"], factory_ctor),
                                         ("Factory clone(", ["ptr:"+fid, "cptr:"+fid], factory_copy)):
        function = fr_function(prefix, "void", parameters)
        calls = gc_calls(function)
        assert [c["callee"] for c in calls] == [selected]
        assert np_pointer(function, calls[0]["args"][0]) == ("parameter", function["params"][0]["name"])
        assert not any(v["type"] == fid for v in function["locals"])
    sid = scoped["id"]
    scoped_ctor = fr_function(" Scoped(int", "void", ["ptr:"+sid, "int"])["name"]
    scope_function = fr_function(" friend int inspectScoped(", "int", ["int"])
    calls = gc_calls(scope_function)
    assert [c["callee"] for c in calls] == [scoped_ctor, sid+"_destroy"]
    assert np_pointer(scope_function, calls[0]["args"][0]) == np_pointer(scope_function, calls[1]["args"][0])
    assert sum(v["type"] == sid for v in scope_function["locals"]) == 1
    for function in friends["functions"]:
        for call in gc_calls(function):
            assert [a["type"] for a in call["args"]] == [p["type"] for p in fr_functions[call["callee"]]["params"]]
    with tempfile.TemporaryDirectory(prefix="neverc-friends-relocated-") as temp:
        relocated = check("v2-friends-relocated", friends_source,
                          root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == friends, "friend identities depend on the absolute root"

    friends_positive = {
        'hidden-getter': 'class R{int n=1;friend int get(const R&r){return r.n;}};int main(){R r;return get(r)-1;}',
        'hidden-reference': 'class R{int n=1;friend int&ref(R&r){return r.n;}};int main(){R r;ref(r)=3;return ref(r)-3;}',
        'hidden-pointer': 'class R{int n=1;friend const int*data(const R&r){return &r.n;}};int main(){R r;return *data(r)-1;}',
        'hidden-operator': 'class R{int n=1;friend int operator+(const R&r,int n){return r.n+n;}};int main(){R r;return (r+2)-3;}',
        'hidden-default': 'class R{int n=1;friend int get(const R&r,int v=2){return r.n+v;}};int main(){R r;return get(r)-3;}',
        'friend-class': 'class R{int n=1;friend class A;};class A{public:static int get(const R&r){return r.n;}};int main(){R r;return A::get(r)-1;}',
        'friend-alias': 'class A;using B=A;class R{int n=1;friend B;};class A{public:static int get(const R&r){return r.n;}};int main(){R r;return A::get(r)-1;}',
        'friend-ignored-int': 'class R{friend int;int n=1;};int main(){R r;return 0;}',
        'friend-ignored-pointer-alias': 'using P=int*;class R{friend P;int n=1;};int main(){R r;return 0;}',
        'friend-member': 'class R;class A{public:static int get(const R&);};class R{int n=1;friend int A::get(const R&);};int A::get(const R&r){return r.n;}int main(){R r;return A::get(r)-1;}',
        'friend-declaration-definition': 'class R{int n=1;friend int get(const R&);};int get(const R&r){return r.n;}int main(){R r;return get(r)-1;}',
        'friend-qualified-free': 'class R;int get(const R&);class R{int n=1;friend int ::get(const R&);};int get(const R&r){return r.n;}int main(){R r;return get(r)-1;}',
        'friend-private-constructor': 'class R{int n;R(int v):n(v){}friend R make(int);public:int get()const{return n;}};R make(int v){return R(v);}int main(){return make(3).get()-3;}',
        'friend-private-destruction': 'int use();class R{int n=1;~R(){}friend int use(){R r;return r.n;}};int main(){return use()-1;}',
        'friend-adl-range': 'class R{int a[2]={1,2};friend int*begin(R&r){return r.a;}friend int*end(R&r){return r.a+2;}};int main(){int n=0;for(int v:R{})n+=v;return n-3;}',
        'friend-redeclarations': 'class B;class A{int n=1;friend int sum(const A&,const B&);};class B{int n=2;friend int sum(const A&,const B&);};int sum(const A&a,const B&b){return a.n+b.n;}int main(){A a;B b;return sum(a,b)-3;}',
        'promoted-1': 'struct R{int n;friend int operator+(const R&r,int v){return r.n+v;}};',
        'promoted-2': 'class R{int n=1;friend int get(const R&r){return r.n;}};',
    }
    for name, source in friends_positive.items():
        check("v2-friends-positive-" + name, source, profile="cpp-core-v2")
    friends_reject = {
        'function-template': 'class R{int n=1;template<class T>friend int get(const R&r,T v){return r.n+v;}};',
        'class-template': 'template<class T>class A{};class R{int n=1;template<class T>friend class A;};',
        'dependent-friend': 'template<class T>class R{int n=1;friend T;};',
        'unsupported-friend-form': 'template<class T>class A{public:struct B{};};class R{int n=1;template<class T>friend class A<T>::B;};',
        'floating-body': 'class R{int n=1;friend int get(const R&r){double ignored=1.0;return r.n;}};',
        'floating-default': 'class R{int n=1;friend int get(const R&r,double v=1.0){return r.n;}};',
        'floating-friend-type': 'class R{int n=1;friend double;};',
        'dead-floating-body': 'class R{int n=1;friend int get(const R&r){if(false){double ignored=1.0;}return r.n;}};',
        'inheritance': 'struct B{int n;};class R:public B{friend int get(const R&r){return r.n;}};',
    }
    for name, source in friends_reject.items():
        check("v2-friends-reject-" + name, source, 'TR0201', profile="cpp-core-v2")
    friends_invalid = {
        'nonfriend-private': 'class R{int n=1;friend int get(const R&r){return r.n;}};int other(const R&r){return r.n;}',
        'not-reciprocal': 'class B;class A{int n=1;friend class B;public:int get(const B&);};class B{int n=2;};int A::get(const B&b){return b.n;}',
        'not-transitive': 'class R{int n=1;friend class A;};class A{friend class B;};class B{public:int get(const R&r){return r.n;}};',
        'other-overload': 'class R{int n=1;friend int get(const R&);};int get(const R&r){return r.n;}int get(R&r){return r.n;}',
        'other-member': 'class R;class A{public:int allowed(const R&);int denied(const R&);};class R{int n=1;friend int A::allowed(const R&);};int A::allowed(const R&r){return r.n;}int A::denied(const R&r){return r.n;}',
        'hidden-qualified-lookup': 'class R{int n=1;friend int get(const R&r){return r.n;}};int f(){R r;return ::get(r);}',
        'hidden-scalar-lookup': 'class R{int n=1;friend int get(int v){return v;}};int f(){return get(1);}',
        'without-object': 'class R{int n=1;friend int get(const R&){return n;}};',
    }
    for name, source in friends_invalid.items():
        check("v2-friends-invalid-" + name, source, 'TR0202', profile="cpp-core-v2")
    friends_missing = {
        'friend-function': 'class R{int n=1;friend int get(const R&);};',
        'friend-member': 'class R;class A{public:int get(const R&);};class R{int n=1;friend int A::get(const R&);};',
    }
    for name, source in friends_missing.items():
        check("v2-friends-missing-" + name, source, 'TR0203', profile="cpp-core-v2")
    check("v1-friends-array", "struct R{int n;friend int get(const R&r){return r.n;}};int main(){R r{1};return get(r)-1;}", "TR0201")
    check("v1-friends-record", "struct A{int n;};struct R{int n;friend struct A;};int main(){return 0;}", "TR0201")

    nested_records_source = """int tick(int n){return n;}
class Outer {
 int seed;
 class Inner {
  int n;
 public:
  Inner(int v):n(v){}
  int own()const{return n;}
  int combined(const Outer&o)const{return n+o.seed;}
 };
public:
 using Item=Inner;
 Outer(int n):seed(n){}
 Item make(int n)const{return Item(n+seed);}
};
struct First {
 struct Item {int n;}; // First
 Item item;
};
struct Second {
 struct Item {int n;}; // Second
 Item item;
};
int identify(const First::Item&i){return i.n;}
int identify(const Second::Item&i){return i.n+1;}
struct Deep {
 struct Middle {
  struct Leaf {int n;};
  Leaf leaf;
 };
 Middle middle;
 Middle::Leaf array[2];
};
struct Links {
 struct B;
 struct A {A*self;B*other;};
 struct B {A*other;};
 A first;
 B second;
};
struct Empty {
 struct Item {};
 Item first,second;
};
struct Outlined {struct Item;};
struct Outlined::Item {
 int n;
 int get()const;
};
int Outlined::Item::get()const{return n;}
struct Owned {
 struct Element {
  int n;
  Element(int v):n(v){}
  Element(const Element&s):n(s.n){tick(n);}
  Element(Element&&s):n(s.n){s.n=0;tick(n);}
  ~Element(){tick(n);}
 };
 Element first;
 Element array[2];
 Owned():first(1),array{Element(2),Element(3)}{}
 Owned(const Owned&)=default;
 Owned(Owned&&)=default;
 ~Owned()=default;
};
class Range {
 int values[2];
 class Iterator {
  int*p;
 public:
  Iterator(int*q):p(q){}
  int&operator*()const{return *p;}
  Iterator&operator++(){++p;return *this;}
  bool operator!=(const Iterator&i)const{return p!=i.p;}
 };
public:
 Range():values{1,2}{}
 Iterator begin(){return Iterator(values);}
 Iterator end(){return Iterator(values+2);}
};
void useOuter(){Outer o(3);Outer::Item i(4);tick(i.own());tick(i.combined(o));auto m=o.make(5);tick(m.own());}
void useOwned(){Owned a;Owned b=a;Owned c=static_cast<Owned&&>(b);tick(0);}
void useRange(){for(int&v:Range{})tick(v);}
"""
    nested_records = check("v2-nested-records-protocol", nested_records_source, profile="cpp-core-v2")
    nr_functions = {f["name"]: f for f in nested_records["functions"]}
    nr_records = {r["id"]: r for r in nested_records["records"]}
    assert len(nr_functions) == len(nested_records["functions"])
    assert len(nr_records) == len(nested_records["records"])

    def nr_line(prefix):
        found = [i for i, line in enumerate(nested_records_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0]

    def nr_record(prefix):
        found = [r for r in nested_records["records"] if r["loc"]["line"] == nr_line(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0]

    def nr_function(prefix, result, parameters):
        found = [f for f in nested_records["functions"] if f["loc"]["line"] == nr_line(prefix)
                 and f["result"] == result and [p["type"] for p in f["params"]] == parameters]
        assert len(found) == 1, (prefix, result, parameters, found)
        return found[0]

    # Source scope is identity; equal names/layouts do not merge declarations.
    first = nr_record("struct First {")
    second = nr_record("struct Second {")
    first_item = nr_record(" struct Item {int n;}; // First")
    second_item = nr_record(" struct Item {int n;}; // Second")
    assert first_item["id"] != second_item["id"]
    assert first_item["layout"] == second_item["layout"]
    for owner, item in ((first, first_item), (second, second_item)):
        assert [f["type"] for f in owner["fields"]] == [item["id"]]
    left = nr_function("int identify(const First", "int", ["cptr:"+first_item["id"]])
    right = nr_function("int identify(const Second", "int", ["cptr:"+second_item["id"]])
    assert left["name"] != right["name"]
    order = {r["id"]: i for i, r in enumerate(nested_records["records"])}
    for record in nested_records["records"]:
        for field in record["fields"]:
            leaf = field["type"]
            while leaf.startswith("arr:"):
                leaf = leaf.split(":", 2)[2]
            if leaf in nr_records:
                assert order[leaf] < order[record["id"]], (record, leaf)
    deep = nr_record("struct Deep {")
    middle = nr_record(" struct Middle {")
    leaf = nr_record("  struct Leaf {")
    assert [f["type"] for f in deep["fields"]] == [middle["id"], "arr:2:"+leaf["id"]]
    assert [f["type"] for f in middle["fields"]] == [leaf["id"]]
    assert deep["layout"] == {"size_bits": 96, "abi_align_bits": 32, "field_offsets_bits": [0, 32]}
    assert middle["layout"] == leaf["layout"] == first_item["layout"] == {
        "size_bits": 32, "abi_align_bits": 32, "field_offsets_bits": [0]}
    a = nr_record(" struct A {")
    b = nr_record(" struct B {A*")
    assert [f["type"] for f in a["fields"]] == ["ptr:"+a["id"], "ptr:"+b["id"]]
    assert [f["type"] for f in b["fields"]] == ["ptr:"+a["id"]]
    # Pointer cycles require forward declarations, not a by-value sorting edge.
    assert order[a["id"]] < order[b["id"]]
    empty = nr_record("struct Empty {")
    empty_item = nr_record(" struct Item {};")
    assert empty_item["fields"] == [] and empty_item["layout"] == {
        "size_bits": 8, "abi_align_bits": 8, "field_offsets_bits": []}
    assert [f["type"] for f in empty["fields"]] == [empty_item["id"]]*2
    assert empty["layout"] == {"size_bits": 16, "abi_align_bits": 8, "field_offsets_bits": [0, 8]}
    outlined = nr_record("struct Outlined::Item {")
    outlined_get = nr_function("int Outlined::Item::get(", "int", ["cptr:"+outlined["id"]])
    assert not gc_calls(outlined_get)
    outer = nr_record("class Outer {")
    inner = nr_record(" class Inner {")
    oid, iid = outer["id"], inner["id"]
    assert [f["type"] for f in outer["fields"]] == ["int"]
    assert [f["type"] for f in inner["fields"]] == ["int"]
    assert inner["layout"] == outer["layout"] == first_item["layout"]
    own = nr_function("  int own(", "int", ["cptr:"+iid])
    combined = nr_function("  int combined(", "int", ["cptr:"+iid, "cptr:"+oid])
    for function, expected in (
        (own, {("field", ("parameter", own["params"][0]["name"]), inner["fields"][0]["name"])}),
        (combined, {("field", ("parameter", combined["params"][0]["name"]), inner["fields"][0]["name"]),
                    ("field", ("parameter", combined["params"][1]["name"]), outer["fields"][0]["name"])})):
        accesses = {np_place(function, n) for n in walk(function["body"]) if n.get("kind") == "member"}
        assert accesses == expected, (function, accesses, expected)
        assert not gc_calls(function)
    inner_ctor = nr_function("  Inner(int", "void", ["ptr:"+iid, "int"])["name"]
    outer_ctor = nr_function(" Outer(int", "void", ["ptr:"+oid, "int"])["name"]
    factory = nr_function(" Item make(", "void", ["ptr:"+iid, "cptr:"+oid, "int"])
    calls = gc_calls(factory)
    assert [c["callee"] for c in calls] == [inner_ctor]
    assert np_pointer(factory, calls[0]["args"][0]) == ("parameter", factory["params"][0]["name"])
    assert not any(v["type"] in (iid, oid) for v in factory["locals"])
    use_outer = nr_function("void useOuter(", "void", [])
    tick = nr_function("int tick(", "int", ["int"])["name"]
    calls = gc_calls(use_outer)
    assert [c["callee"] for c in calls] == [outer_ctor, inner_ctor, own["name"], tick,
        combined["name"], tick, factory["name"], own["name"], tick]
    outer_storage, inner_storage = [np_pointer(use_outer, c["args"][0]) for c in calls[:2]]
    assert [np_pointer(use_outer, arg) for arg in calls[4]["args"]] == [inner_storage, outer_storage]
    assert np_pointer(use_outer, calls[6]["args"][1]) == outer_storage
    assert np_pointer(use_outer, calls[6]["args"][0]) == np_pointer(use_outer, calls[7]["args"][0])
    owned = nr_record("struct Owned {")
    element = nr_record(" struct Element {")
    wid, eid = owned["id"], element["id"]
    field, array = [f["name"] for f in owned["fields"]]
    assert [f["type"] for f in owned["fields"]] == [eid, "arr:2:"+eid]
    selected_ctor = nr_function("  Element(int", "void", ["ptr:"+eid, "int"])["name"]
    selected_copy = nr_function("  Element(const", "void", ["ptr:"+eid, "cptr:"+eid])["name"]
    selected_move = nr_function("  Element(Element&&", "void", ["ptr:"+eid, "ptr:"+eid])["name"]
    default_ctor = nr_function(" Owned():", "void", ["ptr:"+wid])
    copy_ctor = nr_function(" Owned(const", "void", ["ptr:"+wid, "cptr:"+wid])
    move_ctor = nr_function(" Owned(Owned&&", "void", ["ptr:"+wid, "ptr:"+wid])

    def nr_members(parameter):
        owner = ("parameter", parameter["name"])
        return [("field", owner, field)] + [("element", ("field", owner, array), i) for i in (0, 1)]

    calls = gc_calls(default_ctor)
    assert [c["callee"] for c in calls] == [selected_ctor]*3
    assert [np_pointer(default_ctor, c["args"][0]) for c in calls] == nr_members(default_ctor["params"][0])
    assert [gc_identity(default_ctor, c["args"][1]) for c in calls] == [1, 2, 3]
    for function, selected in ((copy_ctor, selected_copy), (move_ctor, selected_move)):
        calls = gc_calls(function)
        assert [c["callee"] for c in calls] == [selected]*3
        for i, parameter in enumerate(function["params"]):
            assert [np_pointer(function, c["args"][i]) for c in calls] == nr_members(parameter)
    destructor = nr_functions[wid+"_destroy"]
    calls = gc_calls(destructor)
    assert [c["callee"] for c in calls] == [eid+"_destroy"]*3
    assert [np_pointer(destructor, c["args"][0]) for c in calls] == list(reversed(nr_members(destructor["params"][0])))
    use_owned = nr_function("void useOwned(", "void", [])
    calls = gc_calls(use_owned)
    assert [c["callee"] for c in calls] == [default_ctor["name"], copy_ctor["name"], move_ctor["name"], tick] + [wid+"_destroy"]*3
    destinations = [np_pointer(use_owned, c["args"][0]) for c in calls[:3]]
    assert len(set(destinations)) == 3
    assert [np_pointer(use_owned, c["args"][0]) for c in calls[4:]] == list(reversed(destinations))
    assert [np_pointer(use_owned, calls[i]["args"][1]) for i in (1, 2)] == destinations[:2]
    range_record = nr_record("class Range {")
    iterator = nr_record(" class Iterator {")
    rid, itid = range_record["id"], iterator["id"]
    assert [f["type"] for f in iterator["fields"]] == ["ptr:int"]
    range_ctor = nr_function(" Range():", "void", ["ptr:"+rid])["name"]
    iterator_ctor = nr_function("  Iterator(int*", "void", ["ptr:"+itid, "ptr:int"])["name"]
    begin = nr_function(" Iterator begin(", "void", ["ptr:"+itid, "ptr:"+rid])
    end = nr_function(" Iterator end(", "void", ["ptr:"+itid, "ptr:"+rid])
    for function in (begin, end):
        calls = gc_calls(function)
        assert [c["callee"] for c in calls] == [iterator_ctor]
        assert np_pointer(function, calls[0]["args"][0]) == ("parameter", function["params"][0]["name"])
    comparison = nr_function("  bool operator!=(", "bool", ["cptr:"+itid, "cptr:"+itid])["name"]
    dereference = nr_function("  int&operator*(", "ptr:int", ["cptr:"+itid])["name"]
    increment = nr_function("  Iterator&operator++(", "ptr:"+itid, ["ptr:"+itid])["name"]
    use_range = nr_function("void useRange(", "void", [])
    calls = gc_calls(use_range)
    assert [c["callee"] for c in calls] == [range_ctor, begin["name"], end["name"], comparison, dereference, tick, increment]
    assert np_pointer(use_range, calls[1]["args"][1]) == np_pointer(use_range, calls[0]["args"][0]) == np_pointer(use_range, calls[2]["args"][1])
    begin_storage, end_storage = [np_pointer(use_range, c["args"][0]) for c in calls[1:3]]
    assert begin_storage != end_storage
    assert [np_pointer(use_range, a) for a in calls[3]["args"]] == [begin_storage, end_storage]
    assert [np_pointer(use_range, calls[i]["args"][0]) for i in (4, 6)] == [begin_storage]*2
    for function in nested_records["functions"]:
        for call in gc_calls(function):
            assert [a["type"] for a in call["args"]] == [p["type"] for p in nr_functions[call["callee"]]["params"]]
    with tempfile.TemporaryDirectory(prefix="neverc-nested-records-relocated-") as temp:
        relocated = check("v2-nested-records-relocated", nested_records_source,
                          root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == nested_records, "nested identities depend on the absolute root"

    nested_grants_source = """int acquire();
int live=0;
class Grant {
 int n=3;
 friend int read(const Grant&);
 friend int read(const Grant&,int);
public:
 class Inner {
  int own=5;
 public:
  friend int inside(const Inner&i){return i.own;}
  friend int read(const Grant&r){return r.n;}
  friend int read(const Grant&r,int add){return r.n+add;}
  int member(const Grant&r)const{return r.n+own;}
 };
};
struct Public {
 int n=7;
 struct Inner {
  friend int publicRead(const Public&r){return r.n;}
 };
};
int publicRead(const Public&);
class Life {
 int n;
 Life():n(9){++live;}
 ~Life(){n=99;--live;}
 friend int acquire();
public:
 struct Inner {
  friend int acquire(){Life item;return item.n;}
 };
};
int main(){
 Grant outer;Grant::Inner inner;Public publicValue{7};
 if(inside(inner)!=5)return 1;
 if(read(outer)!=3||read(outer,4)!=7)return 2;
 if(inner.member(outer)!=8)return 3;
 if(publicRead(publicValue)!=7)return 4;
 if(acquire()!=9||live)return 5;
 return 0;
}
"""
    nested_grants = check("v2-nested-friend-grants", nested_grants_source, profile="cpp-core-v2")
    ng_functions = {f["name"]: f for f in nested_grants["functions"]}
    assert len(ng_functions) == len(nested_grants["functions"])

    def ng_line(prefix):
        found = [i for i, line in enumerate(nested_grants_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0]

    def ng_record(prefix):
        found = [r for r in nested_grants["records"] if r["loc"]["line"] == ng_line(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0]

    def ng_function(prefix):
        found = [f for f in nested_grants["functions"] if f["loc"]["line"] == ng_line(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0]

    grant = ng_record("class Grant {")
    inner = ng_record(" class Inner {")
    public_record = ng_record("struct Public {")
    life = ng_record("class Life {")
    for prefix, record, params in (
            ("  friend int inside(", inner, []),
            ("  friend int read(const Grant&r){", grant, []),
            ("  friend int read(const Grant&r,int", grant, ["int"]),
            ("  friend int publicRead(", public_record, [])):
        function = ng_function(prefix)
        assert function["result"] == "int"
        assert [p["type"] for p in function["params"]] == ["cptr:"+record["id"], *params]
        accesses = [n for n in walk(function["body"]) if n.get("kind") == "member"]
        expected = ("field", ("parameter", function["params"][0]["name"]), record["fields"][0]["name"])
        assert accesses and all(np_place(function, n) == expected for n in accesses)
        assert not gc_calls(function)
    assert ng_function("  friend int read(const Grant&r){")["name"] != ng_function("  friend int read(const Grant&r,int")["name"]
    acquire = ng_function("  friend int acquire(){")
    assert acquire["result"] == "int" and not acquire["params"]
    calls = gc_calls(acquire)
    assert [c["callee"] for c in calls] == [ng_function(" Life():")["name"], life["id"]+"_destroy"]
    assert np_pointer(acquire, calls[0]["args"][0]) == np_pointer(acquire, calls[1]["args"][0])
    assert sum(v["type"] == life["id"] for v in acquire["locals"]) == 1
    returned = [n["value"] for n in acquire["body"] if n["op"] == "return"]
    assert len(returned) == 1 and returned[0]["kind"] == "var"
    captures = [i for i, n in enumerate(acquire["body"]) if n["op"] == "assign"
                and n["target"].get("kind") == "var" and n["target"]["name"] == returned[0]["name"]]
    assert len(captures) == 1 and captures[0] < acquire["body"].index(calls[1])
    for function in nested_grants["functions"]:
        for call in gc_calls(function):
            assert call["callee"] in ng_functions
            assert [a["type"] for a in call["args"]] == [p["type"] for p in ng_functions[call["callee"]]["params"]]
    with tempfile.TemporaryDirectory(prefix="neverc-nested-grants-relocated-") as temp:
        relocated = check("v2-nested-friend-grants-relocated", nested_grants_source,
                          root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == nested_grants

    nested_records_positive = {
        'unevaluated-outer-field': 'class R{int n;public:struct I{int size()const{return sizeof(n);}};};int main(){R::I i;return i.size()-sizeof(int);}',
        'public': 'struct R{struct I{int n;};I i;};int main(){R r{{3}};return r.i.n-3;}',
        'private-alias': 'class R{class I{int n;public:I(int v):n(v){}int get()const{return n;}};public:using Item=I;};int main(){R::Item i(3);return i.get()-3;}',
        'protected-alias': 'class R{protected:struct I{int n;};public:using Item=I;};int main(){R::Item i{3};return i.n-3;}',
        'private-factory-auto': 'class R{struct I{int n;};public:static I make(){return I{3};}};int main(){auto i=R::make();return i.n-3;}',
        'explicit-outer': 'class R{int n=3;public:struct I{int n=4;int get(const R&r)const{return n+r.n;}};};int main(){R r;R::I i;return i.get(r)-7;}',
        'inner-friend': 'struct R{class I{int n=3;friend struct R;};I i;int get()const{return i.n;}};int main(){R r;return r.get()-3;}',
        'out-of-line-type': 'struct R{struct I;};struct R::I{int n;};int main(){R::I i{3};return i.n-3;}',
        'out-of-line-member': 'struct R{struct I{int n;I(int);int get()const;};};R::I::I(int v):n(v){}int R::I::get()const{return n;}int main(){R::I i(3);return i.get()-3;}',
        'distinct-scopes': 'struct A{struct I{int n;};};struct B{struct I{int n;};};int f(const A::I&i){return i.n;}int f(const B::I&i){return i.n+1;}int main(){A::I a{3};B::I b{3};return f(a)+f(b)-7;}',
        'multilevel-array': 'struct R{struct I{struct V{int n;};V v[2];};I i;};int main(){R r{{{{1},{2}}}};return r.i.v[1].n-2;}',
        'forward-self': 'struct R{struct I;struct I{I*p;};};int main(){R::I i{nullptr};i.p=&i;return i.p!=&i;}',
        'mutual-pointers': 'struct R{struct B;struct A{B*p;};struct B{A*p;};};int main(){R::A a{nullptr};R::B b{&a};a.p=&b;return a.p->p!=&a;}',
        'empty': 'struct R{struct I{};I a,b;};int main(){R r;return &r.a==&r.b;}',
        'alias-enum': 'struct R{struct I{using N=unsigned int;enum class E:N{x=3};N get()const{return static_cast<N>(E::x);}};};int main(){R::I i;return i.get()-3u;}',
        'local': 'int main(){struct R{struct I{int n;};I i;};R r{{3}};return r.i.n-3;}',
        'nested-range': 'class R{class I{int*p;public:I(int*q):p(q){}int&operator*(){return *p;}I&operator++(){++p;return *this;}bool operator!=(const I&r)const{return p!=r.p;}};int a[2]={1,2};public:I begin(){return I(a);}I end(){return I(a+2);}};int main(){int n=0;for(int v:R{})n+=v;return n-3;}',
        'generated-copy': 'struct R{struct I{int n;I(int v):n(v){}I(const I&i):n(i.n+1){}};I i[2];};int main(){R a{{1,2}};R b=a;return b.i[0].n+b.i[1].n-5;}',
        'generated-move': 'struct R{struct I{int n;I(int v):n(v){}I(I&&i):n(i.n){i.n=0;}};I i[2];};int main(){R a{{1,2}};R b=static_cast<R&&>(a);return a.i[0].n+a.i[1].n+b.i[1].n-2;}',
        'nested-destructor': 'int n=0;struct R{struct I{int id;~I(){n=n*10+id;}};I i[2];};int main(){{R r{{{1},{2}}};}return n-21;}',
        'tagged-typedef': 'struct R{typedef struct I{int n;} Item;Item item;};int main(){R r{{3}};return r.item.n-3;}',
        'existing-top-level-anonymous': 'typedef struct{int n;} R;int main(){R r{3};return r.n-3;}',
        'promoted-1': 'class R{struct V{int n;};V v;};',
        'promoted-2': 'class R{struct V{int n;};friend int get(const R&){return 1;}};',
        'promoted-3': 'struct R{struct I{int n;};I i;R():i{1}{}};',
        'promoted-4': 'struct E{struct I{};};',
    }
    for name, source in nested_records_positive.items():
        check("v2-nested-records-positive-" + name, source, profile="cpp-core-v2")
    nested_records_reject = {
        'anonymous-field': 'struct R{struct{int n;} value;};',
        'anonymous-typedef': 'struct R{typedef struct{int n;} I;I i;};',
        'nested-template': 'struct R{template<class T>struct I{T n;};};',
        'dependent-type': 'template<class T>struct R{struct I{T n;};};',
        'union': 'struct R{union I{int n;unsigned int u;};};',
        'anonymous-union': 'struct R{union{int n;unsigned int u;};};',
        'inherited': 'struct R{struct B{int n;};struct I:B{};};',
        'virtual': 'struct R{struct I{virtual int get(){return 1;}};};',
        'float-field': 'struct R{struct I{double n;};};',
        'bitfield': 'struct R{struct I{int n:2;};};',
        'reference-field': 'struct R{struct I{int&n;};};',
        'const-field': 'struct R{struct I{const int n;};};',
        'mutable-field': 'struct R{struct I{mutable int n;};};',
        'unused-body': 'struct R{struct I{int get(){double d=1.0;return 1;}};};',
        'erased-alias': 'struct R{struct I{using Unsupported=double;int n;};};',
        'oversized-array': 'struct R{struct I{int a[65537];};I i;};',
    }
    for name, source in nested_records_reject.items():
        check("v2-nested-records-reject-" + name, source, 'TR0201', profile="cpp-core-v2")
    nested_records_invalid = {
        'nested-friend-private-field': 'class R{int n=1;public:struct I{friend int get(const R&r){return r.n;}};};',
        'nested-friend-protected-field': 'class R{protected:int n=1;public:struct I{friend int get(const R&r){return r.n;}};};',
        'nested-friend-static-method': 'class R{static int value(){return 1;}public:struct I{friend int get(){return R::value();}};};',
        'nested-friend-static-data': 'class R{static const int n=1;public:struct I{friend int get(){return R::n;}};};',
        'nested-friend-type': 'class R{using Value=int;public:struct I{friend int get(){R::Value n=1;return n;}};};',
        'nested-friend-return-type': 'class R{using Value=int;public:struct I{friend Value get(){return 1;}};};',
        'nested-friend-enum': 'class R{enum E{x=1};public:struct I{friend int get(){return R::x;}};};',
        'nested-friend-constructor': 'class R{R(){}public:struct I{friend int get(){R r;return 1;}};};',
        'nested-friend-destructor': 'class R{~R(){}public:struct I{friend int get(){R r;return 1;}};};',
        'nested-friend-default': 'class R{static const int n=1;public:struct I{friend int get(int value=R::n){return value;}};};',
        'nested-friend-unevaluated': 'class R{int n=1;public:struct I{friend int get(const R&r){return sizeof(r.n);}};};',
        'nested-friend-out-of-line-type': 'class R{int n=1;public:struct I;};struct R::I{friend int get(const R&r){return r.n;}};',
        'nested-friend-out-of-line-function': 'class R{int n=1;public:struct I{friend int get(const R&);};};int get(const R&r){return r.n;}',
        'nested-friend-wrong-overload': 'class R{int n=1;friend int get(const R&);public:struct I{friend int get(const R&r){return r.n;}friend int get(const R&r,int){return r.n;}};};',
        'nested-friend-local-class': 'class R{int n=1;public:struct I{friend int get(const R&r){struct L{static int read(const R&r){return r.n;}};return L::read(r);}};};',
        'nested-friend-transitive-grant': 'class R{int n=1;friend struct A;public:struct I{friend int get(const R&r){return r.n;}};};struct A{friend int get(const R&);};',
        'nested-friend-without-outer-grant': 'class R{int n=1;struct I{friend int get(const R&r){return r.n;}};};',
        'private-type': 'class R{struct I{int n;};};R::I f(){return R::I{1};}',
        'protected-type': 'class R{protected:struct I{int n;};};R::I f(){return R::I{1};}',
        'outer-without-grant': 'struct R{class I{int n=1;};I i;int get()const{return i.n;}};',
        'without-outer-object': 'class R{int n=1;public:struct I{int get()const{return n;}};};',
        'incomplete-by-value': 'struct R{struct I;I i;};struct R::I{int n;};',
        'cyclic-by-value': 'struct R{struct I{R r;};I i;};',
        'distinct-conversion': 'struct A{struct I{int n;};};struct B{struct I{int n;};};void f(){A::I a{1};B::I b=a;}',
        'invalid-nested-access': 'struct R{class I{int n=1;};};int f(const R::I&i){return i.n;}',
    }
    for name, source in nested_records_invalid.items():
        check("v2-nested-records-invalid-" + name, source, 'TR0202', profile="cpp-core-v2")
    nested_records_missing = {
    }
    for name, source in nested_records_missing.items():
        check("v2-nested-records-missing-" + name, source, 'TR0203', profile="cpp-core-v2")
    check("v1-nested-records-field", "struct R{struct I{int n;};I i;};int main(){R r{{1}};return r.i.n-1;}", "TR0201")
    check("v1-nested-records-type", "struct R{struct I{int n;};int n;};int main(){R::I i{1};return i.n-1;}", "TR0201")

    # Cached earlier roots and preorder nested declarations enforce the same
    # by-value depth bound. Pointer cycles are covered independently above.
    for dependency_first in (False, True):
        for depth in (64, 65):
            if dependency_first:
                source = "struct R0{int n;};" + "".join(
                    f"struct R{i}{{R{i-1} value;}};" for i in range(1, depth+1))
            else:
                source = "".join(f"struct R{i}{{" for i in range(depth+1)) + "int n;};"
                source += "".join(f"R{i} value;}};" for i in range(depth, 0, -1))
            check(f"v2-nested-record-depth-{dependency_first}-{depth}", source,
                  "TR0201" if depth == 65 else None, profile="cpp-core-v2")

    static_members_source = """int tick(int n){return n;}
constexpr int seed(){return 7;}
struct Shared {
 inline static int value=2;
 inline static int zero;
 inline static bool enabled=false;
 static const int inside=3;
 static const int outside;
 static constexpr int constant=5;
 int field;
};
const int Shared::inside;
const int Shared::outside=4;
constexpr int Shared::constant;
struct Other {inline static int value=6;};
struct Nested {struct Item{inline static int value=7;};};
class Hidden {
 inline static int secret=8;
public:static int&ref(){return secret;}
};
struct Receiver {
 inline static int shared=11;
 static constexpr int constant=12;
 Receiver(){tick(1);}
 ~Receiver(){tick(2);}
};
struct Default {
 inline static int initial=seed();
 int n=initial;
};
Shared&select(Shared&r){tick(0);return r;}
Shared*choosePointer(Shared&r){tick(0);return &r;}
Receiver make(){return Receiver();}
int read(){return Shared::value;}
int&ref(){return Shared::value;}
const int&fixed(){return Shared::constant;}
int*pointer(){return &Shared::value;}
void dot(Shared&r){select(r).value=13;}
int arrow(Shared&r){return choosePointer(r)->value;}
int&lasting(){return make().shared;}
int*lastingPointer(){return &make().shared;}
const int&lastingConst(){return make().constant;}
void temporaryWrite(){make().shared=14;tick(3);}
void discarded(Shared&r){select(r).value;}
int bump(int&n=Shared::value){return ++n;}
int defaults(){return bump();}
int unevaluated(){return sizeof(make().shared);}
"""
    static_members = check("v2-static-members-protocol", static_members_source, profile="cpp-core-v2")
    sm_functions = {f["name"]: f for f in static_members["functions"]}
    sm_globals = {g["name"]: g for g in static_members["globals"]}
    assert len(sm_globals) == len(static_members["globals"]), "static redeclarations duplicated storage"
    assert len(sm_functions) == len(static_members["functions"])

    def sm_line(prefix):
        found = [i for i, line in enumerate(static_members_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0]

    def sm_global(prefix, kind, value, mutable):
        found = [g for g in static_members["globals"] if g["loc"]["line"] == sm_line(prefix)]
        assert len(found) == 1, (prefix, found)
        glob = found[0]
        assert glob["type"] == kind and glob["value"]["kind"] == "literal"
        assert glob["value"]["type"] == kind
        assert glob["value"]["value"] == (value if kind == "bool" else str(value))
        assert glob.get("mutable", False) is mutable
        if not mutable:
            assert "mutable" not in glob
        return glob["name"]

    def sm_record(prefix):
        found = [r for r in static_members["records"] if r["loc"]["line"] == sm_line(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0]

    def sm_function(prefix, result, parameters):
        found = [f for f in static_members["functions"] if f["loc"]["line"] == sm_line(prefix)
                 and f["result"] == result and [p["type"] for p in f["params"]] == parameters]
        assert len(found) == 1, (prefix, result, parameters, found)
        return found[0]

    value = sm_global(" inline static int value=2;", "int", 2, True)
    zero = sm_global(" inline static int zero;", "int", 0, True)
    enabled = sm_global(" inline static bool enabled=", "bool", False, True)
    inside = sm_global("const int Shared::inside;", "int", 3, False)
    outside = sm_global("const int Shared::outside=", "int", 4, False)
    constant = sm_global(" static constexpr int constant=5;", "int", 5, False)
    other = sm_global("struct Other {", "int", 6, True)
    nested = sm_global("struct Nested {", "int", 7, True)
    secret = sm_global(" inline static int secret=", "int", 8, True)
    shared = sm_global(" inline static int shared=", "int", 11, True)
    receiver_constant = sm_global(" static constexpr int constant=12;", "int", 12, False)
    initial = sm_global(" inline static int initial=", "int", 7, True)
    assert len(sm_globals) == 12
    assert len({value, other, nested}) == 3
    assert all(g["loc"]["line"] != sm_line("constexpr int Shared::constant;") for g in static_members["globals"])
    record = sm_record("struct Shared {")
    receiver = sm_record("struct Receiver {")
    hidden = sm_record("class Hidden {")
    assert [f["type"] for f in record["fields"]] == ["int"]
    assert record["layout"] == {"size_bits": 32, "abi_align_bits": 32, "field_offsets_bits": [0]}
    for empty_record in (receiver, hidden, sm_record("struct Other {")):
        assert empty_record["fields"] == [] and empty_record["layout"] == {
            "size_bits": 8, "abi_align_bits": 8, "field_offsets_bits": []}
    for r in static_members["records"]:
        assert all(f["name"] not in sm_globals for f in r["fields"])
    for prefix, result, glob in (("int&ref(", "ptr:int", value),
                                 ("const int&fixed(", "cptr:int", constant),
                                 ("int*pointer(", "ptr:int", value),
                                 ("public:static int&ref(", "ptr:int", secret)):
        function = sm_function(prefix, result, [])
        returns = [n["value"] for n in function["body"] if n["op"] == "return"]
        assert len(returns) == 1 and np_pointer(function, returns[0]) == ("object", glob)
        assert not gc_calls(function)
    read = sm_function("int read(", "int", [])
    assert any(n.get("kind") == "var" and n.get("name") == value for n in walk(read["body"]))
    assert not gc_calls(read)
    rid = record["id"]
    select = sm_function("Shared&select(", "ptr:"+rid, ["ptr:"+rid])["name"]
    choose_pointer = sm_function("Shared*choosePointer(", "ptr:"+rid, ["ptr:"+rid])["name"]
    dot = sm_function("void dot(", "void", ["ptr:"+rid])
    arrow = sm_function("int arrow(", "int", ["ptr:"+rid])
    discarded = sm_function("void discarded(", "void", ["ptr:"+rid])
    for function, callee in ((dot, select), (arrow, choose_pointer), (discarded, select)):
        calls = gc_calls(function)
        assert [c["callee"] for c in calls] == [callee]
        assert np_pointer(function, calls[0]["args"][0]) == ("parameter", function["params"][0]["name"])
        assert not any(n.get("kind") == "member" for n in walk(function["body"])), function
        # Reference forwarding may spell &*parameter without loading it. The
        # result of select/choosePointer must never be dereferenced for a static.
        forwarded = {id(n["args"][0]) for n in walk(function["body"])
                     if n.get("kind") == "address" and n["args"][0].get("kind") == "dereference"}
        assert all(id(n) in forwarded for n in walk(function["body"])
                   if n.get("kind") == "dereference"), function
        assert not any(v["type"] == rid for v in function["locals"])
    writes = [n for n in dot["body"] if n["op"] == "assign" and n["target"].get("name") == value]
    assert len(writes) == 1 and gc_identity(dot, writes[0]["value"]) == 13
    assert any(n.get("name") == value for n in walk(arrow["body"]))
    assert not any(n.get("name") == value for n in walk(discarded["body"]))
    receiver_id = receiver["id"]
    make = sm_function("Receiver make(", "void", ["ptr:"+receiver_id])["name"]
    destroy = receiver_id+"_destroy"
    for prefix, result, glob in (("int&lasting(", "ptr:int", shared),
                                 ("int*lastingPointer(", "ptr:int", shared),
                                 ("const int&lastingConst(", "cptr:int", receiver_constant)):
        function = sm_function(prefix, result, [])
        calls = gc_calls(function)
        assert [c["callee"] for c in calls] == [make, destroy]
        destination = np_pointer(function, calls[0]["args"][0])
        assert destination != ("object", glob)
        assert np_pointer(function, calls[1]["args"][0]) == destination
        returned = [n["value"] for n in function["body"] if n["op"] == "return"]
        assert len(returned) == 1 and np_pointer(function, returned[0]) == ("object", glob)
        assert sum(v["type"] == receiver_id for v in function["locals"]) == 1
    tick = sm_function("int tick(", "int", ["int"])["name"]
    temporary_write = sm_function("void temporaryWrite(", "void", [])
    calls = gc_calls(temporary_write)
    assert [c["callee"] for c in calls] == [make, destroy, tick]
    store_index = [i for i, n in enumerate(temporary_write["body"])
                   if n["op"] == "assign" and n["target"].get("name") == shared]
    destroy_index = [i for i, n in enumerate(temporary_write["body"])
                     if n["op"] == "call" and n["callee"] == destroy]
    assert len(store_index) == len(destroy_index) == 1 and store_index[0] < destroy_index[0]
    bump = sm_function("int bump(", "int", ["ptr:int"])["name"]
    defaults = sm_function("int defaults(", "int", [])
    calls = gc_calls(defaults)
    assert [c["callee"] for c in calls] == [bump]
    assert np_pointer(defaults, calls[0]["args"][0]) == ("object", value)
    unevaluated = sm_function("int unevaluated(", "int", [])
    assert not gc_calls(unevaluated)
    assert [gc_identity(unevaluated, n["value"]) for n in unevaluated["body"] if n["op"] == "return"] == [4]
    seed = sm_function("constexpr int seed(", "int", [])["name"]
    assert not any(c["callee"] == seed for f in static_members["functions"] for c in gc_calls(f))
    for function in static_members["functions"]:
        for call in gc_calls(function):
            assert [a["type"] for a in call["args"]] == [p["type"] for p in sm_functions[call["callee"]]["params"]]
    with tempfile.TemporaryDirectory(prefix="neverc-static-members-relocated-") as temp:
        relocated = check("v2-static-members-relocated", static_members_source,
                          root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == static_members, "static member identities depend on the absolute root"

    static_members_positive = {
        'inline-zero': 'struct R{inline static int n;};int main(){return R::n;}',
        'inline-value': 'struct R{inline static int n=3;};int main(){return ++R::n-4;}',
        'out-of-line': 'struct R{static int n;};int R::n=3;int main(){return R::n-3;}',
        'const-out-of-line': 'struct R{static const int n;};const int R::n=3;int main(){const int&r=R::n;return r-3;}',
        'class-initializer': 'struct R{static const int n=3;};const int R::n;int main(){const int*r=&R::n;return *r-3;}',
        'constexpr-inline': 'struct R{static constexpr int n=3;};int main(){const int&r=R::n;return &r!=&R::n;}',
        'constexpr-redeclaration': 'struct R{static constexpr int n=3;};constexpr int R::n;int main(){return R::n-3;}',
        'inline-const': 'struct R{inline static const int n=3;};int main(){return *&R::n-3;}',
        'bool-enum': 'struct R{enum class E:unsigned int{a=1u,b=2u};inline static bool ok=false;inline static E e=E::a;};int main(){R::ok=true;R::e=R::E::b;return !R::ok||static_cast<unsigned int>(R::e)!=2u;}',
        'const-receiver': 'struct R{inline static int n=1;};int main(){const R r{};r.n=3;return R::n-3;}',
        'private': 'class R{inline static int n=3;public:static int&ref(){return n;}};int main(){R::ref()=4;return R::ref()-4;}',
        'friend': 'class R{inline static int n=3;friend int get(const R&){return n;}};int main(){R r;return get(r)-3;}',
        'nested': 'struct R{struct I{inline static int n=3;};};int main(){return R::I::n-3;}',
        'distinct': 'struct A{inline static int n=1;};struct B{inline static int n=2;};int main(){A::n=3;return B::n-2;}',
        'default-reference': 'struct R{inline static int n=1;};int f(int&n=R::n){return ++n;}int main(){return f()-2;}',
        'default-member': 'struct R{inline static int initial=1;int n=initial;};int main(){R a;R::initial=3;R b;return a.n+b.n-4;}',
        'constant-call': 'constexpr int seed(){return 3;}struct R{inline static int n=seed();};int main(){return R::n-3;}',
        'initializer-class-scope': 'struct R{static constexpr int first=2;static int second;};int R::second=first+1;int main(){return R::second-3;}',
        'shared-reference': 'struct R{inline static int n=1;int&ref(){return n;}};int main(){R a,b;a.ref()=3;return &a.ref()!=&b.ref();}',
        'partial-receiver': 'struct R{int uninitialized;inline static int n=3;};int main(){R r;return r.n-3;}',
        'receiver-effects': 'struct R{inline static int n=3;};R&get(R&r,int&n){++n;return r;}int main(){R r;int n=0;get(r,n).n=4;return n+R::n-5;}',
        'lasting-reference': 'int dead=0;struct R{inline static int n=3;~R(){++dead;}};int&f(){return R{}.n;}int main(){int&r=f();return &r!=&R::n||dead!=1;}',
        'lasting-pointer': 'struct R{inline static int n=3;~R(){}};int*f(){return &R{}.n;}int main(){return f()!=&R::n;}',
        'lasting-const-reference': 'struct R{static constexpr int n=3;~R(){}};const int&f(){return R{}.n;}int main(){return &f()!=&R::n;}',
        'promoted-1': 'struct R{static int x;int n=1;};int R::x=0;',
        'promoted-2': 'struct R{static int value;};int R::value=1;',
        'promoted-3': 'struct R{int n;static int value;int get(){return value;}};int R::value=1;',
        'promoted-4': 'struct R{int n;static int value;R():n(1){}};int R::value=1;',
    }
    for name, source in static_members_positive.items():
        check("v2-static-members-positive-" + name, source, profile="cpp-core-v2")
    static_members_reject = {
        'dynamic-call': 'int value(){return 3;}struct R{inline static int n=value();};',
        'dynamic-write': 'int n=0;struct R{inline static int value=++n;};',
        'floating': 'struct R{inline static double n=1.0;};',
        'pointer': 'struct R{inline static int*n=nullptr;};',
        'reference': 'int n=0;struct R{inline static int&value=n;};',
        'array': 'struct R{inline static int a[2]={1,2};};',
        'record': 'struct I{int n;};struct R{inline static I i{1};};',
        'tls': 'struct R{inline static thread_local int n=1;};',
        'volatile': 'struct R{inline static volatile int n=1;};',
        'variable-template': 'struct R{template<class T>inline static int n=1;};',
        'dependent': 'template<class T>struct R{inline static T n=1;};',
        'folded-unsupported': 'struct R{inline static int n=static_cast<int>(1.0);};',
        'folded-body': 'constexpr int f(){return static_cast<int>(1.0);}struct R{inline static int n=f();};',
    }
    for name, source in static_members_reject.items():
        check("v2-static-members-reject-" + name, source, 'TR0201', profile="cpp-core-v2")
    static_members_invalid = {
        'private': 'class R{inline static int n=1;};int f(){return R::n;}',
        'protected': 'class R{protected:inline static int n=1;};int f(){return R::n;}',
        'write-const': 'struct R{static constexpr int n=1;};void f(){R::n=2;}',
        'duplicate': 'struct R{static int n;};int R::n=1;int R::n=2;',
        'noninline-initializer': 'struct R{static int n=1;};',
        'local-class': 'int f(){struct R{static int n;};return 0;}',
        'const-pointer': 'struct R{static constexpr int n=1;};int*f(){return &R::n;}',
    }
    for name, source in static_members_invalid.items():
        check("v2-static-members-invalid-" + name, source, 'TR0202', profile="cpp-core-v2")
    static_members_missing = {
        'mutable-definition': 'struct R{static int n;};int f(){return R::n;}',
        'unused-definition': 'struct R{static int n;};',
        'default-definition': 'struct R{static int n;};int f(int&n=R::n){return n;}',
        'unevaluated-definition': 'struct R{static int n;};int f(){return sizeof(R::n);}',
        'const-address': 'struct R{static const int n=1;};const int*f(){return &R::n;}',
    }
    for name, source in static_members_missing.items():
        check("v2-static-members-missing-" + name, source, 'TR0203', profile="cpp-core-v2")
    check("v1-static-members-outline", "struct R{int n;static int value;};int R::value=1;", "TR0201")
    check("v1-static-members-inline", "struct R{int n;inline static int value=1;};", "TR0201")

    static_values_source = """int tick(int n){return n;}
struct Values {
 static const int first=3;
 static const int second=5;
};
struct Defined {static const int value=7;};
const int Defined::value;
struct Early {
 int get()const{return later;}
 static const int later=9;
};
struct Receiver {
 static const int value=11;
 int field;
 Receiver(){tick(1);}
 ~Receiver(){tick(2);}
};
Receiver make(){return Receiver();}
struct Default {int n=Values::first;};
int direct(){return Values::first;}
int conditional(bool choose){return choose?Values::first:Values::second;}
int mixed(bool choose,const int&v){return choose?Values::first:v;}
int comma(int&n){return (++n,Values::first);}
int effect(){return make().value;}
int effectConditional(bool choose){return choose?make().value:Values::first;}
void bare(){make().value;}
void discardedConditional(bool choose){choose?make().value:Values::first;}
void discardedMixed(bool choose,int&v){choose?Values::first:v;}
void plainDiscard(){Values::first;(void)Values::second;(Values::first,Values::second);}
int takes(int n=Values::first){return n;}
int defaults(){return takes();}
int takeReference(const int&n){return n;}
int newValue(){return takeReference(+Values::first);}
void construct(){Default d;tick(d.n);}
const int*definedAddress(){return &Defined::value;}
int query(){return sizeof(&Values::first);}
bool pure(){return noexcept(Values::first);}
"""
    static_values = check("v2-static-values-protocol", static_values_source, profile="cpp-core-v2")
    sv_functions = {f["name"]: f for f in static_values["functions"]}

    def sv_line(prefix):
        found = [i for i, line in enumerate(static_values_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0]

    def sv_record(prefix):
        found = [r for r in static_values["records"] if r["loc"]["line"] == sv_line(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0]

    def sv_function(prefix, result, parameters):
        found = [f for f in static_values["functions"] if f["loc"]["line"] == sv_line(prefix)
                 and f["result"] == result and [p["type"] for p in f["params"]] == parameters]
        assert len(found) == 1, (prefix, result, parameters, found)
        return found[0]

    assert len(static_values["globals"]) == 1, "declaration-only constants invented storage"
    defined = static_values["globals"][0]
    assert defined["loc"]["line"] == sv_line("const int Defined::value;")
    assert defined["type"] == "int" and defined["value"]["kind"] == "literal"
    assert defined["value"]["value"] == "7" and "mutable" not in defined
    for prefix in ("struct Values {", "struct Defined {", "struct Early {"):
        record = sv_record(prefix)
        assert record["fields"] == [] and record["layout"] == {
            "size_bits": 8, "abi_align_bits": 8, "field_offsets_bits": []}
    direct = sv_function("int direct(", "int", [])
    early = sv_function(" int get(", "int", ["cptr:"+sv_record("struct Early {")["id"]])
    for function, value in ((direct, 3), (early, 9)):
        assert not gc_calls(function)
        assert [gc_identity(function, n["value"]) for n in function["body"] if n["op"] == "return"] == [value]
        assert not any(n.get("kind") in ("address", "member") for n in walk(function["body"]))
    conditional = sv_function("int conditional(", "int", ["bool"])
    mixed = sv_function("int mixed(", "int", ["bool", "cptr:int"])
    for function, values in ((conditional, {3, 5}), (mixed, {3})):
        assert not gc_calls(function)
        assert any(n["op"] == "branch" for n in function["body"])
        literal_stores = [n for n in function["body"] if n["op"] == "assign"
                          and n["target"]["kind"] == "var" and n["target"]["type"] == "int"
                          and n["value"].get("kind") == "literal"]
        assert {gc_identity(function, n["value"]) for n in literal_stores} == values
        places = {("object", n["target"]["name"]) for n in literal_stores}
        addresses = [np_pointer(function, n) for n in walk(function["body"])
                     if n.get("kind") == "address" and n["type"] == "cptr:int"]
        assert places <= set(addresses)
        if function is conditional:
            assert set(addresses) == places
        else:
            assert set(addresses) == places | {("parameter", function["params"][1]["name"])}
        assert all(n["value"]["type"] == "int" for n in function["body"] if n["op"] == "return")
    comma = sv_function("int comma(", "int", ["ptr:int"])
    assert not gc_calls(comma)
    assert [gc_identity(comma, n["value"]) for n in comma["body"] if n["op"] == "return"] == [3]
    assert any(n["op"] == "assign" and n["target"]["kind"] == "dereference" for n in comma["body"])
    receiver = sv_record("struct Receiver {")
    assert [f["type"] for f in receiver["fields"]] == ["int"]
    rid = receiver["id"]
    make = sv_function("Receiver make(", "void", ["ptr:"+rid])["name"]
    for prefix, result, params in (("int effect(", "int", []),
                                   ("int effectConditional(", "int", ["bool"]),
                                   ("void bare(", "void", []),
                                   ("void discardedConditional(", "void", ["bool"])):
        function = sv_function(prefix, result, params)
        calls = gc_calls(function)
        assert [c["callee"] for c in calls] == [make, rid+"_destroy"]
        assert np_pointer(function, calls[0]["args"][0]) == np_pointer(function, calls[1]["args"][0])
        assert sum(v["type"] == rid for v in function["locals"]) == 1
        assert not any(n.get("kind") == "member" for n in walk(function["body"]))
        if result == "void":
            assert not any(v["type"] == "int" for v in function["locals"]), "discarded constant created storage"
        elif not params:
            assert [gc_identity(function, n["value"]) for n in function["body"] if n["op"] == "return"] == [11]
        if params:
            assert any(n["op"] == "branch" for n in function["body"])
    plain_discard = sv_function("void plainDiscard(", "void", [])
    discarded_mixed = sv_function("void discardedMixed(", "void", ["bool", "ptr:int"])
    for function in (plain_discard, discarded_mixed):
        assert not gc_calls(function)
        assert not any(v["type"] in ("int", "ptr:int", "cptr:int") for v in function["locals"])
        assert not any(n.get("kind") in ("address", "dereference", "member") for n in walk(function["body"]))
    assert not any(n.get("name") == discarded_mixed["params"][1]["name"] for n in walk(discarded_mixed["body"]))
    takes = sv_function("int takes(", "int", ["int"])["name"]
    defaults = sv_function("int defaults(", "int", [])
    calls = gc_calls(defaults)
    assert [c["callee"] for c in calls] == [takes]
    assert gc_identity(defaults, calls[0]["args"][0]) == 3
    take_reference = sv_function("int takeReference(", "int", ["cptr:int"])["name"]
    new_value = sv_function("int newValue(", "int", [])
    calls = gc_calls(new_value)
    assert [c["callee"] for c in calls] == [take_reference]
    place = np_pointer(new_value, calls[0]["args"][0])
    assert place[0] == "object" and any(v["name"] == place[1] and v["type"] == "int" for v in new_value["locals"])
    stores = [n for n in new_value["body"] if n["op"] == "assign" and n["target"].get("name") == place[1]]
    plus = [n for n in new_value["body"] if n["op"] == "assign"
            and n["value"].get("kind") == "unary" and n["value"].get("operator") == "+"]
    assert len(plus) == 1 and gc_identity(new_value, plus[0]["value"]["args"][0]) == 3
    assert len(stores) == 1 and stores[0]["value"] == plus[0]["target"]
    default_record = sv_record("struct Default {")
    default_constructor = sv_function("struct Default {", "void", ["ptr:"+default_record["id"]])
    assert not gc_calls(default_constructor)
    stores = [n for n in default_constructor["body"] if n["op"] == "assign"
              and n["target"].get("name") == default_record["fields"][0]["name"]]
    assert len(stores) == 1 and gc_identity(default_constructor, stores[0]["value"]) == 3
    defined_address = sv_function("const int*definedAddress(", "cptr:int", [])
    assert [np_pointer(defined_address, n["value"]) for n in defined_address["body"] if n["op"] == "return"] == [("object", defined["name"])]
    query = sv_function("int query(", "int", [])
    pure = sv_function("bool pure(", "bool", [])
    assert not gc_calls(query) and not gc_calls(pure)
    assert [gc_identity(query, n["value"]) for n in query["body"] if n["op"] == "return"] == [static_values["target"]["pointer_bits"]//8]
    assert [gc_identity(pure, n["value"]) for n in pure["body"] if n["op"] == "return"] == [True]
    for function in static_values["functions"]:
        for call in gc_calls(function):
            assert [a["type"] for a in call["args"]] == [p["type"] for p in sv_functions[call["callee"]]["params"]]
    with tempfile.TemporaryDirectory(prefix="neverc-static-values-relocated-") as temp:
        relocated = check("v2-static-values-relocated", static_values_source,
                          root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == static_values, "static value identities depend on the absolute root"

    static_values_positive = {
        'qualified': 'struct R{static const int n=3;};int main(){return R::n-3;}',
        'unused': 'struct R{static const int n=3;};',
        'early': 'struct R{int get()const{return n;}static const int n=3;};int main(){R r;return r.get()-3;}',
        'bool-enum': 'struct R{enum class E:unsigned int{a=3};static const bool b=true;static const E e=E::a;};int main(){return !R::b||static_cast<unsigned int>(R::e)!=3u;}',
        'widths': 'struct R{static const signed char a=-1;static const unsigned short b=65535;static const long long c=-2147483649LL;static const unsigned long long d=0xffffffffffffffffULL;};int main(){return R::a!=-1||R::b!=65535||R::c!=-2147483649LL||R::d!=0xffffffffffffffffULL;}',
        'private': 'class R{static const int n=3;public:static int get(){return n;}};int main(){return R::get()-3;}',
        'protected': 'class R{protected:static const int n=3;public:static int get(){return n;}};int main(){return R::get()-3;}',
        'nested': 'struct R{struct I{static const int n=3;};};int main(){return R::I::n-3;}',
        'constant-contexts': 'struct R{static const int n=3;};static_assert(R::n==3);enum E{x=R::n};int main(){int a[R::n]={1,2,3};return a[x-1]-3;}',
        'conditional': 'struct R{static const int a=3,b=5;};int f(bool b){return b?R::a:R::b;}int main(){return f(true)+f(false)-8;}',
        'mixed-conditional': 'struct R{static const int n=3;};int f(bool b,int&n){return b?R::n:n;}int main(){int n=5;return f(true,n)+f(false,n)-8;}',
        'comma': 'struct R{static const int n=3;};int f(int&n){return (++n,R::n);}int main(){int n=0;int v=f(n);return v+n-4;}',
        'default-value': 'struct R{static const int n=3;};int f(int n=R::n){return n;}int main(){return f()-3;}',
        'default-member': 'struct R{static const int n=3;int value=n;};int main(){R r;return r.value-3;}',
        'new-prvalue-reference': 'struct R{static const int n=3;};int f(const int&n){return n;}int main(){return f(+R::n)-3;}',
        'discard': 'struct R{static const int n=3;};void f(){R::n;(void)R::n;}',
        'discard-comma': 'struct R{static const int a=3,b=5;};void f(){(R::a,R::b);}',
        'discard-conditional': 'struct R{static const int n=3;};void f(bool b,int&n){b?R::n:n;}',
        'discard-if': 'struct R{static const int n=3;};void f(bool b){if(b)R::n;else R::n;}',
        'discard-loops': 'struct R{static const int n=3;};void f(){for(R::n;false;R::n)R::n;while(false)R::n;do R::n;while(false);}',
        'discard-case': 'struct R{static const int n=3;};void f(int n){switch(n){case 1:R::n;break;default:R::n;}}',
        'discard-range': 'struct R{static const int n=3;};void f(){int a[1]={1};for(int v:a)R::n;}',
        'discard-temporary': 'int dead=0;struct R{static const int n=3;~R(){++dead;}};int main(){R{}.n;return dead-1;}',
        'discard-conditional-temporary': 'int dead=0;struct R{static const int n=3;~R(){++dead;}};int main(){true?R{}.n:R::n;false?R{}.n:R::n;return dead-1;}',
        'query': 'struct R{static const int n=3;};int main(){return sizeof(R::n)!=sizeof(int)||sizeof(&R::n)!=sizeof(const int*)||!noexcept(R::n);}',
        'with-definition': 'struct R{static const int n=3;};const int R::n;int main(){const int&r=R::n;return &r!=&R::n;}',
        'promoted-1': 'struct R{int n;static const int value=1;int get(){return value;}};',
    }
    for name, source in static_values_positive.items():
        check("v2-static-values-positive-" + name, source, profile="cpp-core-v2")
    static_values_reject = {
        'floating': 'struct R{static const double n;};const double R::n=1.0;',
        'folded-float': 'struct R{static const int n=static_cast<int>(1.0);};',
        'folded-body': 'constexpr int f(){return static_cast<int>(1.0);}struct R{static const int n=f();};',
        'array': 'struct R{static const int n[2];};',
        'pointer': 'struct R{static const int*n;};',
        'volatile-storage': 'struct R{static const volatile int n;};const volatile int R::n=3;',
        'tls': 'struct R{static thread_local const int n=3;};',
        'variable-template': 'struct R{template<class T>static const int n=3;};',
    }
    for name, source in static_values_reject.items():
        check("v2-static-values-reject-" + name, source, 'TR0201', profile="cpp-core-v2")
    static_values_invalid = {
        'volatile-in-class-initializer': 'struct R{static const volatile int n=3;};',
        'private': 'class R{static const int n=3;};int f(){return R::n;}',
        'write': 'struct R{static const int n=3;};void f(){R::n=4;}',
        'bad-initializer': 'int f(){return 3;}struct R{static const int n=f();};',
    }
    for name, source in static_values_invalid.items():
        check("v2-static-values-invalid-" + name, source, 'TR0202', profile="cpp-core-v2")
    static_values_missing = {
        'address': 'struct R{static const int n=3;};const int*f(){return &R::n;}',
        'reference': 'struct R{static const int n=3;};const int&f(){return R::n;}',
        'local-reference': 'struct R{static const int n=3;};void f(){const int&r=R::n;}',
        'default-reference': 'struct R{static const int n=3;};int f(const int&n=R::n){return n;}',
        'conditional-reference': 'struct R{static const int a=3,b=5;};const int&f(bool b){return b?R::a:R::b;}',
        'reference-argument': 'struct R{static const int n=3;};int f(const int&n){return n;}int g(){return f(R::n);}',
        'discarded-address': 'struct R{static const int n=3;};void f(){(void)&R::n;}',
        'discarded-parenthesized-address': 'struct R{static const int n=3;};void f(){(&((R::n)));}',
        'discarded-reference-call': 'struct R{static const int n=3;};int use(const int&n){return n;}void f(){use(R::n);}',
        'discarded-reference-cast': 'struct R{static const int n=3;};void f(){static_cast<const int&>(R::n);}',
        'discarded-hidden-reference': 'struct R{static const int n=3;};struct S{static const int n=4;};S make(const int&n){return S{};}void f(){make(R::n).n;}',
        'dead-address': 'struct R{static const int n=3;};void f(){if(false){(void)&R::n;}}',
        'folded-address': 'struct R{static const int n=3;};static_assert(&R::n!=nullptr);',
        'missing-initializer': 'struct R{static const int n;};',
        'mutable-still-missing': 'struct R{static int n;};',
    }
    for name, source in static_values_missing.items():
        check("v2-static-values-missing-" + name, source, 'TR0203', profile="cpp-core-v2")
    check("v1-static-values-method", "struct R{int n;static const int value=3;int get(){return value;}};", "TR0201")
    check("v1-static-values-assertion", "struct R{int n;static const int value=3;};static_assert(R::value==3);", "TR0201")

    static_locals_source = """constexpr int seed(){return 11;}
int&state(){
 static int n;
 return n;
}
const int*fixed(){
 static const int n=7;
 return &n;
}
int initialized(){
 static int n=seed();
 return ++n;
}
int scopes(bool choose){
 if(choose){static int n=10;return ++n;}
 else {static int n=20;return ++n;}
}
int over(int){static int n=30;return ++n;}
int over(bool){static int n=40;return ++n;}
int bypass(){switch(1){static int n=50;case 1:return ++n;}}
int forInit(){for(static int n=60;;){return ++n;}}
int loop(int count){int last=0;for(int i=0;i<count;++i){static int n=70;last=++n;}return last;}
struct R{int field;int&member(){static int n=80;return n;}};
bool flip(){static bool n=false;n=!n;return n;}
int automatic(){int n=90;return ++n;}
void write(int n){state()=n;}
int*address(){return &state();}
"""
    static_locals = check("v2-static-locals-protocol", static_locals_source, profile="cpp-core-v2")
    sl_functions = {f["name"]: f for f in static_locals["functions"]}

    def sl_line(prefix):
        lines = [i for i, line in enumerate(static_locals_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(lines) == 1, (prefix, lines)
        return lines[0]

    def sl_function(prefix, result, parameters):
        found = [f for f in static_locals["functions"] if f["loc"]["line"] == sl_line(prefix)
                 and f["result"] == result and [p["type"] for p in f["params"]] == parameters]
        assert len(found) == 1, (prefix, found)
        return found[0]

    def sl_global(prefix, value, mutable=True, kind="int"):
        found = [g for g in static_locals["globals"] if g["loc"]["line"] == sl_line(prefix)]
        assert len(found) == 1, (prefix, found)
        global_value = found[0]
        assert global_value["type"] == kind and global_value["value"]["kind"] == "literal"
        assert global_value["value"]["type"] == kind and int(global_value["value"]["value"]) == value
        assert global_value.get("mutable", False) == mutable
        if not mutable:
            assert "mutable" not in global_value
        return global_value

    assert len(static_locals["globals"]) == 12
    zero = sl_global(" static int n;", 0)
    fixed_global = sl_global(" static const int n=7;", 7, False)
    initialized_global = sl_global(" static int n=seed();", 11)
    left_global = sl_global(" if(choose){", 10)
    right_global = sl_global(" else {", 20)
    over_int_global = sl_global("int over(int)", 30)
    over_bool_global = sl_global("int over(bool)", 40)
    bypass_global = sl_global("int bypass()", 50)
    for_global = sl_global("int forInit()", 60)
    loop_global = sl_global("int loop(", 70)
    member_global = sl_global("struct R{", 80)
    flip_global = sl_global("bool flip()", 0, kind="bool")
    names = {g["name"] for g in static_locals["globals"]}
    assert len(names) == 12
    for function in static_locals["functions"]:
        assert not names & {v["name"] for v in function["locals"]}, "static storage became an automatic local"
        assert not any(n["op"] == "assign" and n["target"].get("name") in names
                       and n["value"].get("kind") == "literal" for n in function["body"]), "static initializer ran at block entry"
    state = sl_function("int&state()", "ptr:int", [])
    fixed = sl_function("const int*fixed()", "cptr:int", [])
    record = static_locals["records"][0]
    assert len(static_locals["records"]) == 1 and [f["type"] for f in record["fields"]] == ["int"]
    member = sl_function("struct R{", "ptr:int", ["ptr:"+record["id"]])
    for function, global_value in ((state, zero), (fixed, fixed_global), (member, member_global)):
        assert not gc_calls(function)
        assert [np_pointer(function, n["value"]) for n in function["body"] if n["op"] == "return"] == [("object", global_value["name"])]
        assert not any(n.get("kind") == "member" for n in walk(function["body"]))
    initialized = sl_function("int initialized()", "int", [])
    bypass = sl_function("int bypass()", "int", [])
    for_init = sl_function("int forInit()", "int", [])
    loop = sl_function("int loop(", "int", ["int"])
    over_int = sl_function("int over(int)", "int", ["int"])
    over_bool = sl_function("int over(bool)", "int", ["bool"])
    for function, global_value in ((initialized, initialized_global), (bypass, bypass_global),
                                   (for_init, for_global), (loop, loop_global),
                                   (over_int, over_int_global), (over_bool, over_bool_global)):
        assert not gc_calls(function), "constant initializer became a runtime call"
        writes = [n for n in function["body"] if n["op"] == "assign" and n["target"].get("name") == global_value["name"]]
        assert len(writes) == 1 and writes[0]["value"]["kind"] == "binary" and writes[0]["value"]["operator"] == "+"
        reads = [n for n in function["body"] if n["op"] == "assign" and n["value"].get("name") == global_value["name"]]
        assert reads and all(n["value"]["type"] == "int" for n in reads)
    scopes = sl_function("int scopes(", "int", ["bool"])
    assert any(n["op"] == "branch" for n in scopes["body"])
    assert {n["target"]["name"] for n in scopes["body"] if n["op"] == "assign" and n["target"].get("name") in names} == {left_global["name"], right_global["name"]}
    automatic = sl_function("int automatic()", "int", [])
    assert any(n["op"] == "assign" and n["value"].get("kind") == "literal" and n["value"].get("value") == "90" for n in automatic["body"])
    assert not any(n.get("kind") == "var" and n.get("name") in names for n in walk(automatic["body"]))
    write = sl_function("void write(", "void", ["int"])
    address = sl_function("int*address()", "ptr:int", [])
    for function in (write, address):
        calls = gc_calls(function)
        assert [c["callee"] for c in calls] == [state["name"]]
        target = calls[0]["target"]["name"]
        if function is write:
            stores = [n for n in function["body"] if n["op"] == "assign" and n["target"].get("kind") == "dereference"]
            assert len(stores) == 1 and stores[0]["target"]["args"][0].get("name") == target
            assert gc_identity(function, stores[0]["value"]) == ("parameter", function["params"][0]["name"])
        else:
            # Reference return forwarding can be represented by &* of the call result.
            returned = [n["value"] for n in function["body"] if n["op"] == "return"]
            assert len(returned) == 1
            value = returned[0]
            while value["kind"] in ("address", "dereference", "cast", "var"):
                if value["kind"] != "var":
                    value = value["args"][0]
                elif value["name"] == target:
                    break
                else:
                    definitions = [n["value"] for n in function["body"] if n["op"] == "assign" and n["target"].get("name") == value["name"]]
                    assert len(definitions) == 1
                    value = definitions[0]
            assert value["kind"] == "var" and value["name"] == target
    for function in static_locals["functions"]:
        for call in gc_calls(function):
            assert [a["type"] for a in call["args"]] == [p["type"] for p in sl_functions[call["callee"]]["params"]]
    with tempfile.TemporaryDirectory(prefix="neverc-static-locals-relocated-") as temp:
        relocated = check("v2-static-locals-relocated", static_locals_source,
                          root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == static_locals, "static local storage identities depend on the absolute root"

    static_locals_positive = {
        'zero': 'int&f(){static int n;return n;}int main(){f()=3;return f()-3;}',
        'constant': 'int f(){static int n=3;return ++n;}int main(){return f()+f()-9;}',
        'const-address': 'const int*f(){static const int n=3;return &n;}int main(){return *f()!=3||f()!=f();}',
        'constexpr': 'const int&f(){static constexpr int n=3;return n;}int main(){return &f()!=&f();}',
        'bool-enum': 'enum class E:unsigned char{high=255};int f(){static bool b=true;static E e=E::high;return b?static_cast<int>(e):0;}',
        'widths': 'unsigned long long f(){static signed char a=-1;static unsigned short b=65535;static long long c=-2147483649LL;static unsigned long long d=0xffffffffffffffffULL;return a==-1&&b==65535&&c==-2147483649LL?d:0;}',
        'alias': 'int*f(){static int n;return &n;}int main(){int&r=*f();r=7;return *f()-7;}',
        'overloads': 'int*f(int){static int n;return &n;}int*f(bool){static int n;return &n;}int main(){return f(0)==f(false);}',
        'sibling-scopes': 'int*f(bool b){if(b){static int n;return &n;}else{static int n;return &n;}}int main(){return f(true)==f(false);}',
        'inline': 'inline int f(){static int n;return ++n;}int a(){return f();}int b(){return f();}int main(){return a()+b()-3;}',
        'method': 'struct R{int n;int&f(){static int value;return value;}static int*g(){static int value;return &value;}};int main(){R a{},b{};a.f()=3;return b.f()!=3||&a.f()==R::g();}',
        'constructor-destructor': 'int result=0;struct R{int n;R(){static int value=3;n=++value;}~R(){static int count;result=++count;}};int main(){{R a,b;if(a.n!=4||b.n!=5)return 1;}return result-2;}',
        'recursion': 'int f(int d){static int n;++n;if(d)f(d-1);return n;}int main(){return f(2)!=3||f(0)!=4;}',
        'loop-body': 'int f(){int last=0;for(int i=0;i<2;++i){static int n=3;last=++n;}return last;}int main(){return f()!=5||f()!=7;}',
        'for-init': 'int f(){for(static int n=0;;){return ++n;}}int main(){return f()!=1||f()!=2;}',
        'if-init': 'int f(){if(static int n=0;true)return ++n;return 0;}int main(){return f()!=1||f()!=2;}',
        'switch-init': 'int f(){switch(static int n=0;0){default:return ++n;}}int main(){return f()!=1||f()!=2;}',
        'switch-bypass': 'int f(){switch(1){static int n=3;case 1:return ++n;}}int main(){return f()!=4||f()!=5;}',
        'automatic-shadow': 'int f(){static int n=3;{int n=9;if(n!=9)return 0;}return ++n;}int main(){return f()!=4||f()!=5;}',
        'initializer-call': 'constexpr int seed(){return 3;}int f(){static int n=seed();return ++n;}int main(){return f()!=4||f()!=5;}',
        'initializer-local-constant': 'int f(){static const int first=3;static int second=first+1;return ++second;}int main(){return f()!=5||f()!=6;}',
        'initializer-class-constant': 'struct R{static const int n=3;};int f(){static int n=R::n;return ++n;}',
        'initializer-query': 'int f(){int ignored;static int n=sizeof(ignored);return ++n;}int main(){return f()!=sizeof(int)+1||f()!=sizeof(int)+2;}',
        'inferred': 'int f(){static auto n=3;return ++n;}int main(){return f()!=4||f()!=5;}',
        'unused-skipped': 'void f(bool b){static int unused=3;if(b){static const int unused=4;}}',
        'nested-nonconstexpr-method': 'constexpr int f(bool b){struct R{int get(){static int n=3;return ++n;}};if(b){R r{};return r.get();}return 0;}int main(){return f(true)!=4||f(true)!=5||f(false)!=0;}',
        'promoted-1': 'int f(){static int value;return ++value;}',
        'promoted-2': 'int f(){static int n=1;return ++n;}',
    }
    for name, source in static_locals_positive.items():
        check("v2-static-locals-positive-" + name, source, profile="cpp-core-v2")
    static_locals_reject = {
        'dynamic-call': 'int seed(){return 3;}int f(){static int n=seed();return n;}',
        'dynamic-parameter': 'int f(int p){static int n=p;return n;}',
        'dynamic-global': 'int value=3;int f(){static int n=value;return n;}',
        'dynamic-self': 'int f(){static int n=n;return n;}',
        'unused-dynamic': 'int seed(){return 3;}void f(){static int unused=seed();}',
        'skipped-dynamic': 'int seed(){return 3;}void f(){if(false){static int unused=seed();}}',
        'folded-float': 'int f(){static int n=static_cast<int>(1.0);return n;}',
        'folded-body': 'constexpr int seed(){return static_cast<int>(1.0);}int f(){static int n=seed();return n;}',
        'tls': 'int f(){thread_local int n=3;return n;}',
        'static-tls': 'int f(){static thread_local int n=3;return n;}',
        'volatile': 'int f(){static volatile int n=3;return n;}',
        'floating': 'double f(){static double n=3.0;return n;}',
        'pointer': 'int*f(){static int*n=nullptr;return n;}',
        'reference': 'int value=3;int&f(){static int&n=value;return n;}',
        'array': 'int f(){static int n[2]={1,2};return n[0];}',
        'record': 'struct R{int n;};int f(){static R r{3};return r.n;}',
        'record-destruction': 'struct R{int n;~R(){}};int f(){static R r{3};return r.n;}',
        'extern': 'int value=3;int f(){extern int value;return value;}',
        'constexpr-function': 'constexpr int f(bool b){if(b){static int n=3;return n;}return 0;}',
        'constexpr-method': 'struct R{constexpr int f(bool b)const{if(b){static const int n=3;return n;}return 0;}};',
        'constexpr-skipped': 'constexpr int f(){if(false){static int n=3;}return 0;}',
    }
    for name, source in static_locals_reject.items():
        check("v2-static-locals-reject-" + name, source, 'TR0201', profile="cpp-core-v2")
    static_locals_invalid = {
        'const-write': 'void f(){static const int n=3;n=4;}',
        'const-uninitialized': 'void f(){static const int n;}',
        'duplicate': 'void f(){static int n=3;static int n=4;}',
        'scope': 'void f(){if(true){static int n=3;}n=4;}',
    }
    for name, source in static_locals_invalid.items():
        check("v2-static-locals-invalid-" + name, source, 'TR0202', profile="cpp-core-v2")
    check("v1-static-locals-mutable", "int f(){static int n;return ++n;}", "TR0201")
    check("v1-static-locals-const", "int f(){static const int n=3;return n;}", "TR0201")

    imports_source = """namespace Original {
int state=3;
const int constant=7;
enum E { entry=4 };
using Number=int;
struct R { int n; };
int choose(int){return 10;}
int change(int&n,int amount=2){n+=amount;return n;}
void mark(int&n){n=n*10+4;}
}
namespace Alias=Original;
namespace Again=Alias;
namespace Early {
using Original::choose;
using Original::choose;
using Original::state,Original::constant,Original::R,Original::Number;
using Original::E,Original::entry;
}
namespace Reexport {
using Early::state;
using Early::R;
}
namespace Original {
int choose(bool){return 20;}
}
namespace Directed {
using namespace Again;
int late(){return choose(true);}
}
namespace Other {
int state=19;
}
namespace Defaults { int value(int); }
namespace DefaultUse {
using Defaults::value;
}
namespace Defaults {
int value(int n=6){return n;}
}
namespace Guard {
struct G {
 int*trace;
 G(int*p):trace(p){*trace=*trace*10+1;}
 ~G(){*trace=*trace*10+2;}
};
}
int early(){return Early::choose(true);}
int original(){return Original::choose(true);}
int lateImport(){using Original::choose;return choose(true);}
int defaulted(){return DefaultUse::value();}
int*originalAddress(){return &Original::state;}
int*aliasAddress(){namespace Local=Again;return &Local::state;}
int*importAddress(){using Reexport::state;return &state;}
const int*constantAddress(){using Early::constant;return &constant;}
void write(int value){using Early::state;state=value;}
int change(){using Original::change;using Early::state;return change(state);}
int hidden(){using namespace Original;int state=23;return state;}
int other(){using Other::state;return state;}
Reexport::R&same(Early::R&r){return r;}
Early::Number enumValue(){using Early::entry;return entry;}
int localEnum(){enum E{entry=12};{using E::entry;return entry;}}
void cleanup(int&trace){
 using Guard::G;
 G g(&trace);
 namespace Local=Original;
 using Local::mark;
 mark(trace);
}
int main(){return early()-10;}
"""
    imports = check("v2-name-imports-protocol", imports_source, profile="cpp-core-v2")
    ni_functions = {f["name"]: f for f in imports["functions"]}
    assert len(ni_functions) == len(imports["functions"]), "imports duplicated function definitions"

    def ni_line(prefix):
        lines = [i for i, line in enumerate(imports_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(lines) == 1, (prefix, lines)
        return lines[0]

    def ni_function(prefix, result, parameters):
        found = [f for f in imports["functions"] if f["loc"]["line"] == ni_line(prefix)
                 and f["result"] == result and [p["type"] for p in f["params"]] == parameters]
        assert len(found) == 1, (prefix, result, parameters, found)
        return found[0]

    def ni_global(prefix, value, mutable):
        found = [g for g in imports["globals"] if g["loc"]["line"] == ni_line(prefix)]
        assert len(found) == 1, (prefix, found)
        result = found[0]
        assert result["type"] == "int" and result["value"]["kind"] == "literal"
        assert result["value"]["type"] == "int" and result["value"]["value"] == str(value)
        assert result.get("mutable", False) == mutable
        if not mutable:
            assert "mutable" not in result
        return result

    assert len(imports["globals"]) == 3
    state = ni_global("int state=3;", 3, True)
    constant = ni_global("const int constant=7;", 7, False)
    other_state = ni_global("int state=19;", 19, True)
    assert state["name"] != other_state["name"]
    assert len(imports["records"]) == 2
    records = {r["loc"]["line"]: r for r in imports["records"]}
    record = records[ni_line("struct R {")]
    guard = records[ni_line("struct G {")]
    rid, gid = record["id"], guard["id"]
    assert [f["type"] for f in record["fields"]] == ["int"]
    assert record["layout"] == {"size_bits": 32, "abi_align_bits": 32, "field_offsets_bits": [0]}
    assert [f["type"] for f in guard["fields"]] == ["ptr:int"]
    choose_int = ni_function("int choose(int)", "int", ["int"])
    choose_bool = ni_function("int choose(bool)", "int", ["bool"])
    assert choose_int["name"] != choose_bool["name"]
    for prefix, callee in (("int early()", choose_int), ("int original()", choose_bool),
                           ("int lateImport()", choose_bool), ("int late()", choose_bool)):
        function = ni_function(prefix, "int", [])
        calls = gc_calls(function)
        assert len(calls) == 1 and calls[0]["callee"] == callee["name"]
        assert [a["type"] for a in calls[0]["args"]] == [p["type"] for p in callee["params"]]
        assert gc_identity(function, calls[0]["args"][0]) == 1
    defaulted = ni_function("int defaulted()", "int", [])
    default_callee = ni_function("int value(int n=6)", "int", ["int"])
    calls = gc_calls(defaulted)
    assert len(calls) == 1 and calls[0]["callee"] == default_callee["name"]
    assert gc_identity(defaulted, calls[0]["args"][0]) == 6
    for prefix, global_value, result in (("int*originalAddress()", state, "ptr:int"),
                                         ("int*aliasAddress()", state, "ptr:int"),
                                         ("int*importAddress()", state, "ptr:int"),
                                         ("const int*constantAddress()", constant, "cptr:int")):
        function = ni_function(prefix, result, [])
        assert not gc_calls(function)
        assert [np_pointer(function, n["value"]) for n in function["body"] if n["op"] == "return"] == [("object", global_value["name"])]
    write = ni_function("void write(", "void", ["int"])
    writes = [n for n in write["body"] if n["op"] == "assign" and n["target"].get("name") == state["name"]]
    assert len(writes) == 1
    assert gc_identity(write, writes[0]["value"]) == ("parameter", write["params"][0]["name"])
    change = ni_function("int change()", "int", [])
    change_callee = ni_function("int change(int&", "int", ["ptr:int", "int"])
    calls = gc_calls(change)
    assert len(calls) == 1 and calls[0]["callee"] == change_callee["name"]
    assert np_pointer(change, calls[0]["args"][0]) == ("object", state["name"])
    assert gc_identity(change, calls[0]["args"][1]) == 2
    hidden = ni_function("int hidden()", "int", [])
    assert [gc_identity(hidden, n["value"]) for n in hidden["body"] if n["op"] == "return"] == [23]
    assert not any(n.get("kind") == "var" and n.get("name") in {state["name"], other_state["name"]} for n in walk(hidden["body"]))
    other = ni_function("int other()", "int", [])
    assert {n["name"] for n in walk(other["body"]) if n.get("kind") == "var" and n.get("name") in {state["name"], other_state["name"]}} == {other_state["name"]}
    same = ni_function("Reexport::R&same(", "ptr:"+rid, ["ptr:"+rid])
    assert [np_pointer(same, n["value"]) for n in same["body"] if n["op"] == "return"] == [("parameter", same["params"][0]["name"])]
    for prefix, value in (("Early::Number enumValue()", 4), ("int localEnum()", 12)):
        function = ni_function(prefix, "int", [])
        assert not gc_calls(function)
        assert [gc_identity(function, n["value"]) for n in function["body"] if n["op"] == "return"] == [value]
    cleanup = ni_function("void cleanup(", "void", ["ptr:int"])
    constructor = ni_function(" G(int*", "void", ["ptr:"+gid, "ptr:int"])
    mark = ni_function("void mark(", "void", ["ptr:int"])
    calls = gc_calls(cleanup)
    assert [call["callee"] for call in calls] == [constructor["name"], mark["name"], gid+"_destroy"]
    assert np_pointer(cleanup, calls[0]["args"][0]) == np_pointer(cleanup, calls[2]["args"][0])
    assert np_pointer(cleanup, calls[0]["args"][1]) == np_pointer(cleanup, calls[1]["args"][0]) == ("parameter", cleanup["params"][0]["name"])
    import_lines = {i for i, line in enumerate(imports_source.splitlines(), 1)
                    if line.lstrip().startswith("using ") or (line.lstrip().startswith("namespace ") and "=" in line)}
    for collection in (imports["globals"], imports["records"], imports["functions"]):
        assert not any(item["loc"]["line"] in import_lines for item in collection), "lookup declaration created an entity"
    for function in imports["functions"]:
        assert not any(n["loc"]["line"] in import_lines for n in function["body"]), "lookup declaration emitted an operation"
        for call in gc_calls(function):
            assert [a["type"] for a in call["args"]] == [p["type"] for p in ni_functions[call["callee"]]["params"]]
    with tempfile.TemporaryDirectory(prefix="neverc-name-imports-relocated-") as temp:
        relocated = check("v2-name-imports-relocated", imports_source,
                          root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == imports, "namespace imports changed identities after relocation"

    imports_positive = {
        'alias': 'namespace N{int f(){return 3;}}namespace A=N;int main(){return A::f()-3;}',
        'alias-chain': 'namespace N{int n=3;}namespace A=N;namespace B=A;int main(){return &B::n!=&N::n;}',
        'nested-alias': 'namespace N{namespace Inner{int n=3;}}namespace A=N::Inner;int main(){return A::n-3;}',
        'block-alias': 'namespace N{int n=3;}int f(){namespace A=N;return A::n;}',
        'alias-repeated': 'namespace N{int n=3;}namespace A=N;namespace A=N;int f(){return A::n;}',
        'directive': 'namespace N{int n=3;}using namespace N;int f(){return n;}',
        'block-directive': 'namespace N{int n=3;}int f(){using namespace N;return ++n;}',
        'alias-directive': 'namespace N{int n=3;}namespace A=N;using namespace A;int f(){return n;}',
        'anonymous': 'namespace{int n=3;}namespace N{using ::n;}int*f(){return &N::n;}',
        'reopened': 'namespace N{int f(int){return 1;}}namespace A=N;namespace N{int f(bool){return 2;}}int main(){return A::f(true)-2;}',
        'function-overloads': 'namespace N{int f(int){return 1;}int f(bool){return 2;}}using N::f;int main(){return f(true)-2;}',
        'early-overloads': 'namespace N{int f(int){return 1;}}using N::f;namespace N{int f(bool){return 2;}}int main(){return f(true)-1;}',
        'directive-late-overloads': 'namespace N{int f(int){return 1;}}using namespace N;namespace N{int f(bool){return 2;}}int main(){return f(true)-2;}',
        'late-default': 'namespace N{int f(int);}using N::f;namespace N{int f(int n=3){return n;}}int main(){return f()-3;}',
        'mutable-global': 'namespace N{int n=3;}using N::n;int main(){int*p=&n;*p=4;return N::n-4;}',
        'const-global': 'namespace N{const int n=3;}using N::n;const int*f(){return &n;}int main(){return f()!=&N::n;}',
        'record': 'namespace N{struct R{int n;};}using N::R;int main(){R r{3};N::R&v=r;return v.n-3;}',
        'alias-type': 'namespace N{using Number=int;}using N::Number;Number f(){return 3;}',
        'typedef-type': 'namespace N{typedef int Number;}using N::Number;Number f(){return 3;}',
        'enum-type': 'namespace N{enum class E:int{n=3};}using N::E;int main(){return static_cast<int>(E::n)-3;}',
        'enum-namespace-value': 'namespace N{enum E{n=3};}using N::n;int main(){return n-3;}',
        'enum-qualified-value': 'namespace N{enum E{n=3};}using N::E::n;int main(){return n-3;}',
        'enum-local-value': 'int f(){enum E{n=4};{using E::n;return n;}}',
        'enum-method-value': 'struct R{int f(){enum E{n=4};{using E::n;return n;}}};int main(){R r;return r.f()-4;}',
        'reexport': 'namespace N{int n=3;}namespace A{using N::n;}namespace B{using A::n;}int main(){return &B::n!=&N::n;}',
        'repeat': 'namespace N{int n=3;}using N::n;using N::n;int f(){using N::n;using N::n;return n;}',
        'comma': 'namespace N{int a=1,b=2;}using N::a,N::b;int f(){using N::a,N::b;return a+b;}',
        'local-hiding': 'namespace N{int n=3;}int f(){using namespace N;int n=4;return n;}',
        'operator': 'namespace N{struct R{int n;};int operator+(const R&r,int n){return r.n+n;}}using N::operator+;int main(){return operator+(N::R{3},2)-5;}',
        'adl': 'namespace N{struct R{int n;};int f(const R&r){return r.n;}}using N::R;int main(){R r{3};return f(r)-3;}',
        'hidden-friend': 'namespace N{struct R{int n;friend int f(const R&r){return r.n;}};}using N::R;int main(){R r{3};return f(r)-3;}',
        'body-erasure': 'namespace N{int n=3;}int f(){for(int i=0;i<1;++i){namespace A=N;using A::n;if(n)return n;}return 0;}',
        'switch-erasure': 'namespace N{int n=3;}int f(){switch(1){case 1:using N::n;namespace A=N;return n+A::n;default:return 0;}}',
        'unused': 'namespace N{int n=3;}namespace A=N;using namespace A;using A::n;void f(){using A::n;namespace B=A;}',
        'existing-declaration': 'namespace N{int f(){return 3;}using N::f;}int main(){return N::f()-3;}',
        'typename': 'namespace N{struct R{int n;};}using typename N::R;int main(){R r{3};return r.n-3;}',
    }
    for name, source in imports_positive.items():
        check("v2-name-imports-positive-" + name, source, profile="cpp-core-v2")
    imports_reject = {
        'scoped-enumerator': 'enum class E{n=3};using E::n;int f(){return static_cast<int>(n);}',
        'scoped-enumerator-local': 'int f(){enum class E{n=3};using E::n;return static_cast<int>(n);}',
        'using-enum': 'enum class E{n=3};using enum E;int f(){return static_cast<int>(n);}',
        'class-using': 'struct B{int n;};struct D:B{using B::n;};',
        'inherited-constructor': 'struct B{int n;B(int v):n(v){}};struct D:B{using B::B;};',
        'dependent': 'template<class T>struct R:T{using T::n;};',
        'pack': 'template<class...T>struct R:T...{using T::n...;};',
        'unsupported-type': 'namespace N{using T=double;}using N::T;',
        'unused-body': 'namespace N{int f(){double n=3;return static_cast<int>(n);}}using N::f;',
        'unused-initializer': 'namespace N{int n=static_cast<int>(3.0);}using N::n;',
        'skipped-body': 'namespace N{int f(){if(false){double n=3;}return 0;}}using N::f;',
        'folded-body': 'namespace N{constexpr int f(){return static_cast<int>(3.0);}}using N::f;const int value=f();',
        'inactive-include': '#if 0\n#include "missing.h"\n#endif\nnamespace N{int n=3;}using N::n;',
        'active-include': '#include <vector>\nnamespace N{int n=3;}using N::n;',
    }
    for name, source in imports_reject.items():
        check("v2-name-imports-reject-" + name, source, 'TR0201', profile="cpp-core-v2")
    imports_invalid = {
        'missing-namespace': 'namespace A=Missing;',
        'missing-name': 'namespace N{}using N::missing;',
        'namespace-as-using': 'namespace N{namespace Inner{}}using N::Inner;',
        'ambiguous-directive': 'namespace A{int n=1;}namespace B{int n=2;}using namespace A;using namespace B;int f(){return n;}',
        'conflicting-using': 'namespace A{int n=1;}namespace B{int n=2;}using A::n;using B::n;',
        'private-member': 'class R{static const int n=3;};using R::n;',
        'class-member': 'struct R{static const int n=3;};using R::n;',
        'member-enumerator': 'struct R{enum E{n=3};};using R::E::n;',
        'member-type': 'struct R{using T=int;};using R::T;',
        'for-init-using': 'namespace N{int n=3;}void f(){for(using N::n;;)break;}',
        'if-init-using': 'namespace N{int n=3;}void f(){if(using N::n;true){}}',
        'switch-init-using': 'namespace N{int n=3;}void f(){switch(using N::n;0){}}',
        'for-init-directive': 'namespace N{}void f(){for(using namespace N;;)break;}',
        'if-init-alias': 'namespace N{}void f(){if(namespace A=N;true){}}',
        'alias-conflict': 'namespace N{}int A;namespace A=N;',
        'out-of-scope': 'namespace N{int n=3;}int f(){{using N::n;}return n;}',
        'const-write': 'namespace N{const int n=3;}using N::n;void f(){n=4;}',
        'hidden-friend-import': 'namespace N{struct R{int n;friend int read(const R&r){return r.n;}};}using N::read;',
    }
    for name, source in imports_invalid.items():
        check("v2-name-imports-invalid-" + name, source, 'TR0202', profile="cpp-core-v2")
    imports_missing = {
        'function': 'namespace N{int f();}using N::f;int main(){return f();}',
        'unused-function': 'namespace N{int f();}using N::f;',
        'global': 'namespace N{extern int n;}using N::n;int f(){return n;}',
        'unused-global': 'namespace N{extern int n;}using N::n;',
    }
    for name, source in imports_missing.items():
        check("v2-name-imports-missing-" + name, source, 'TR0203', profile="cpp-core-v2")
    check("v1-name-imports-alias", "namespace N{int n=3;}namespace A=N;", "TR0201")
    check("v1-name-imports-declaration", "namespace N{int n=3;}using N::n;", "TR0201")
    check("v1-name-imports-directive", "namespace N{int n=3;}using namespace N;", "TR0201")

    for alias_count in (64, 65):
        for directive in (False, True):
            source = "namespace Original{int n=3;}namespace A0=Original;"
            source += "".join(f"namespace A{i}=A{i-1};" for i in range(1, alias_count))
            last = f"A{alias_count-1}"
            source += (f"using namespace {last};int f(){{return n;}}" if directive
                       else f"int f(){{return {last}::n;}}")
            check(f"v2-name-imports-boundary-{alias_count}-{directive}", source,
                  None if alias_count == 64 else "TR0201", profile="cpp-core-v2")

    inline_source = """namespace API {
struct Outer { int n; };
inline namespace V1 {
int state=3;
const int constant=7;
struct R { int n; };
struct Inner { int n; };
int choose(bool){return 20;}
int read(const Outer&r){return r.n+1;}
int outside();
inline namespace Deep {
int depth=5;
}
}
int choose(int){return 10;}
int read(const Inner&r){return r.n+2;}
namespace V2 {
int state=19;
struct R { bool flag;int n; };
}
}
namespace API::V1 {
int bump(){return ++state;}
}
namespace API::V1::Deep {
int deep(){return depth;}
}
int API::V1::outside(){return 17;}
namespace Alias=API::V1;
namespace Imported {
using API::state,API::R,Alias::bump;
}
namespace Directed {
using namespace Alias;
int*address(){return &state;}
}
namespace Lifetime {
inline namespace V {
struct Guard {
 int*trace;
 Guard(int*p):trace(p){*trace=*trace*10+1;}
 ~Guard(){*trace=*trace*10+2;}
};
}
}
int boolChoice(){return API::choose(true);}
int intChoice(){return API::choose(1);}
int explicitChoice(){return API::V1::choose(true);}
int outerRead(const API::Outer&r){return read(r);}
int innerRead(const API::Inner&r){return read(r);}
int parentOutside(){return API::outside();}
int versionOutside(){return API::V1::outside();}
int importedBump(){return Imported::bump();}
int parentDeep(){return API::deep();}
int*defaultAddress(){return &API::state;}
int*explicitAddress(){return &API::V1::state;}
int*aliasAddress(){return &Alias::state;}
int*importAddress(){using Imported::state;return &state;}
int*otherAddress(){return &API::V2::state;}
int*deepAddress(){return &API::depth;}
int*explicitDeepAddress(){return &API::V1::Deep::depth;}
const int*constantAddress(){return &API::constant;}
void write(int value){API::state=value;}
API::V1::R&same(Imported::R&r){return r;}
void cleanup(int&trace){Lifetime::Guard g(&trace);trace=trace*10+4;}
int main(){return boolChoice()-20;}
"""
    inline_namespaces = check("v2-inline-namespaces-protocol", inline_source, profile="cpp-core-v2")
    in_functions = {f["name"]: f for f in inline_namespaces["functions"]}
    assert len(in_functions) == len(inline_namespaces["functions"])

    def in_line(prefix):
        lines = [i for i, line in enumerate(inline_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(lines) == 1, (prefix, lines)
        return lines[0]

    def in_function(prefix, result, parameters):
        found = [f for f in inline_namespaces["functions"] if f["loc"]["line"] == in_line(prefix)
                 and f["result"] == result and [p["type"] for p in f["params"]] == parameters]
        assert len(found) == 1, (prefix, result, parameters, found)
        return found[0]

    def in_record(prefix):
        found = [r for r in inline_namespaces["records"] if r["loc"]["line"] == in_line(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0]

    def in_global(prefix, value, mutable=True):
        found = [g for g in inline_namespaces["globals"] if g["loc"]["line"] == in_line(prefix)]
        assert len(found) == 1, (prefix, found)
        result = found[0]
        assert result["type"] == "int" and result["value"]["kind"] == "literal"
        assert result["value"]["type"] == "int" and result["value"]["value"] == str(value)
        assert result.get("mutable", False) == mutable
        if not mutable:
            assert "mutable" not in result
        return result

    assert len(inline_namespaces["globals"]) == 4
    state = in_global("int state=3;", 3)
    other = in_global("int state=19;", 19)
    depth = in_global("int depth=5;", 5)
    constant = in_global("const int constant=7;", 7, False)
    assert len({g["name"] for g in inline_namespaces["globals"]}) == 4
    assert len(inline_namespaces["records"]) == 5
    record = in_record("struct R { int n;")
    other_record = in_record("struct R { bool flag;")
    outer = in_record("struct Outer {")
    inner = in_record("struct Inner {")
    guard = in_record("struct Guard {")
    assert record["id"] != other_record["id"]
    assert [f["type"] for f in record["fields"]] == ["int"]
    assert [f["type"] for f in other_record["fields"]] == ["bool", "int"]
    assert record["layout"] == {"size_bits": 32, "abi_align_bits": 32, "field_offsets_bits": [0]}
    assert other_record["layout"] == {"size_bits": 64, "abi_align_bits": 32, "field_offsets_bits": [0, 32]}
    choose_bool = in_function("int choose(bool)", "int", ["bool"])
    choose_int = in_function("int choose(int)", "int", ["int"])
    outside = in_function("int API::V1::outside()", "int", [])
    bump = in_function("int bump()", "int", [])
    deep = in_function("int deep()", "int", [])
    for prefix, callee in (("int boolChoice()", choose_bool), ("int intChoice()", choose_int),
                           ("int explicitChoice()", choose_bool), ("int parentOutside()", outside),
                           ("int versionOutside()", outside), ("int importedBump()", bump),
                           ("int parentDeep()", deep)):
        function = in_function(prefix, "int", [])
        calls = gc_calls(function)
        assert len(calls) == 1 and calls[0]["callee"] == callee["name"]
        assert [a["type"] for a in calls[0]["args"]] == [p["type"] for p in callee["params"]]
    for prefix, callee_prefix, parameter_record in (("int outerRead(", "int read(const Outer&", outer),
                                                    ("int innerRead(", "int read(const Inner&", inner)):
        parameter_type = "cptr:"+parameter_record["id"]
        function = in_function(prefix, "int", [parameter_type])
        callee = in_function(callee_prefix, "int", [parameter_type])
        calls = gc_calls(function)
        assert len(calls) == 1 and calls[0]["callee"] == callee["name"]
        assert np_pointer(function, calls[0]["args"][0]) == ("parameter", function["params"][0]["name"])
    for prefix, global_value, result in (("int*defaultAddress()", state, "ptr:int"),
                                         ("int*explicitAddress()", state, "ptr:int"),
                                         ("int*aliasAddress()", state, "ptr:int"),
                                         ("int*importAddress()", state, "ptr:int"),
                                         ("int*address()", state, "ptr:int"),
                                         ("int*otherAddress()", other, "ptr:int"),
                                         ("int*deepAddress()", depth, "ptr:int"),
                                         ("int*explicitDeepAddress()", depth, "ptr:int"),
                                         ("const int*constantAddress()", constant, "cptr:int")):
        function = in_function(prefix, result, [])
        assert not gc_calls(function)
        assert [np_pointer(function, n["value"]) for n in function["body"] if n["op"] == "return"] == [("object", global_value["name"])]
    write = in_function("void write(", "void", ["int"])
    writes = [n for n in write["body"] if n["op"] == "assign" and n["target"].get("name") == state["name"]]
    assert len(writes) == 1
    assert gc_identity(write, writes[0]["value"]) == ("parameter", write["params"][0]["name"])
    bump_writes = [n for n in bump["body"] if n["op"] == "assign" and n["target"].get("name") == state["name"]]
    assert len(bump_writes) == 1 and bump_writes[0]["value"]["kind"] == "binary"
    assert bump_writes[0]["value"]["operator"] == "+"
    same = in_function("API::V1::R&same(", "ptr:"+record["id"], ["ptr:"+record["id"]])
    assert [np_pointer(same, n["value"]) for n in same["body"] if n["op"] == "return"] == [("parameter", same["params"][0]["name"])]
    cleanup = in_function("void cleanup(", "void", ["ptr:int"])
    constructor = in_function(" Guard(int*", "void", ["ptr:"+guard["id"], "ptr:int"])
    calls = gc_calls(cleanup)
    assert [call["callee"] for call in calls] == [constructor["name"], guard["id"]+"_destroy"]
    assert np_pointer(cleanup, calls[0]["args"][0]) == np_pointer(cleanup, calls[1]["args"][0])
    declaration_lines = {i for i, line in enumerate(inline_source.splitlines(), 1)
                         if line.lstrip().startswith(("namespace ", "inline namespace ", "using "))}
    for collection in (inline_namespaces["globals"], inline_namespaces["records"], inline_namespaces["functions"]):
        assert not any(item["loc"]["line"] in declaration_lines for item in collection), "namespace visibility duplicated an entity"
    for function in inline_namespaces["functions"]:
        assert not any(n["loc"]["line"] in declaration_lines for n in function["body"]), "namespace declaration emitted runtime work"
        for call in gc_calls(function):
            assert [a["type"] for a in call["args"]] == [p["type"] for p in in_functions[call["callee"]]["params"]]
    with tempfile.TemporaryDirectory(prefix="neverc-inline-namespaces-relocated-") as temp:
        relocated = check("v2-inline-namespaces-relocated", inline_source,
                          root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == inline_namespaces, "inline namespace identities depend on the absolute root"

    inline_namespaces_positive = {
        'named': 'namespace N{inline namespace V{int n=3;}}int main(){return N::n-3;}',
        'anonymous': 'namespace N{inline namespace{int n=3;}}int main(){return N::n-3;}',
        'transitive': 'namespace N{inline namespace V{inline namespace Deep{int n=3;}}}int main(){return &N::n!=&N::V::Deep::n;}',
        'reopen': 'namespace N{inline namespace V{int f();}namespace V{int f(){return 3;}}}int main(){return N::f()-3;}',
        'nested-reopen': 'namespace N{inline namespace V{int f();}}namespace N::V{int f(){return 3;}}int main(){return N::f()-3;}',
        'transitive-nested-reopen': 'namespace N{inline namespace V{inline namespace Deep{int f();}}}namespace N::V::Deep{int f(){return 3;}}int main(){return N::f()-3;}',
        'outside-definition': 'namespace N{inline namespace V{int f();}}int N::V::f(){return 3;}int main(){return N::f()-3;}',
        'alias': 'namespace N{inline namespace V{int n=3;}}namespace A=N::V;int main(){return &A::n!=&N::n;}',
        'directive': 'namespace N{inline namespace V{int n=3;}}using namespace N::V;int main(){return &n!=&N::n;}',
        'parent-directive': 'namespace N{inline namespace V{int n=3;}}using namespace N;int main(){return n-3;}',
        'using': 'namespace N{inline namespace V{int n=3;}}using N::n;int f(){using N::V::n;return n;}',
        'import-promoted': 'namespace N{inline namespace V{int n=3;}}using N::n;',
        'versions': 'namespace N{inline namespace V1{int n=3;}namespace V2{int n=4;}}int main(){return &N::n==&N::V2::n;}',
        'record': 'namespace N{inline namespace V{struct R{int n;};}}int main(){N::R r{3};N::V::R&s=r;return &r!=&s;}',
        'qualified-overloads': 'namespace N{int f(int){return 1;}inline namespace V{int f(bool){return 2;}}}int main(){return N::f(true)-2;}',
        'adl-parent-type': 'namespace N{struct R{int n;};inline namespace V{int f(const R&r){return r.n;}}}int main(){N::R r{3};return f(r)-3;}',
        'adl-inline-type': 'namespace N{inline namespace V{struct R{int n;};}int f(const R&r){return r.n;}}int main(){N::R r{3};return f(r)-3;}',
        'hidden-friend': 'namespace N{inline namespace V{struct R{int n;friend int f(const R&r){return r.n;}};}}int main(){N::R r{3};return f(r)-3;}',
        'lifetime': 'namespace N{inline namespace V{struct R{int*p;R(int*q):p(q){++*p;}~R(){++*p;}};}}int main(){int n=0;{N::R r(&n);}return n-2;}',
        'macro-inline': '#define INLINE inline\nnamespace N{INLINE namespace V{int n=3;}}int main(){return N::n-3;}',
        'macro-separator': '#define SCOPE ::\nnamespace N{inline namespace V{int f();}}namespace N SCOPE V{int f(){return 3;}}int main(){return N::f()-3;}',
        'macro-argument-separator': '#define ID(x) x\nnamespace N{inline namespace V{int f();}}namespace N ID(::) V{int f(){return 3;}}',
    }
    for name, source in inline_namespaces_positive.items():
        check("v2-inline-namespaces-positive-" + name, source, profile="cpp-core-v2")
    inline_namespaces_reject = {
        'nested-inline': 'namespace N::inline V{int n=3;}',
        'nested-inline-inner': 'namespace N::inline V::Inner{int n=3;}',
        'nested-inline-reopen': 'namespace N{inline namespace V{int n=3;}}namespace N::inline V{int f(){return n;}}',
        'macro-nested-inline': '#define INLINE inline\nnamespace N::INLINE V{int n=3;}',
        'macro-argument-inline': '#define ID(x) x\nnamespace N::ID(inline) V{int n=3;}',
        'attribute': 'namespace N{inline namespace [[deprecated]] V{int n=3;}}',
        'unused-type': 'namespace N{inline namespace V{using T=double;}}',
        'unused-body': 'namespace N{inline namespace V{int f(){double n=3;return static_cast<int>(n);}}}',
        'skipped-body': 'namespace N{inline namespace V{int f(){if(false){double n=3;}return 0;}}}',
        'folded-initializer': 'namespace N{inline namespace V{int n=static_cast<int>(3.0);}}',
        'active-include': '#include <vector>\nnamespace N{inline namespace V{int n=3;}}',
        'inactive-include': '#if 0\n#include "missing.h"\n#endif\nnamespace N{inline namespace V{int n=3;}}',
    }
    for name, source in inline_namespaces_reject.items():
        check("v2-inline-namespaces-reject-" + name, source, 'TR0201', profile="cpp-core-v2")
    inline_namespaces_invalid = {
        'inline-mismatch': 'namespace N{}inline namespace N{}',
        'block': 'void f(){inline namespace N{}}',
        'class': 'struct R{inline namespace N{}};',
        'alias-reopen': 'namespace N{}namespace A=N;inline namespace A{}',
        'duplicate': 'namespace N{inline namespace V{int n=3;}namespace V{int n=4;}}',
        'ambiguous': 'namespace N{int n=3;inline namespace V{int n=4;}}int f(){return N::n;}',
        'ambiguous-versions': 'namespace N{inline namespace V1{int n=3;}inline namespace V2{int n=4;}}int f(){return N::n;}',
        'inline-alias': 'namespace N{}inline namespace A=N;',
        'leading-inline-nested': 'inline namespace N::V{int n=3;}',
        'const-write': 'namespace N{inline namespace V{const int n=3;}}void f(){N::n=4;}',
    }
    for name, source in inline_namespaces_invalid.items():
        check("v2-inline-namespaces-invalid-" + name, source, 'TR0202', profile="cpp-core-v2")
    inline_namespaces_missing = {
        'function': 'namespace N{inline namespace V{int f();}}int main(){return N::f();}',
        'unused-function': 'namespace N{inline namespace V{int f();}}',
        'global': 'namespace N{inline namespace V{extern int n;}}int f(){return N::n;}',
        'unused-global': 'namespace N{inline namespace V{extern int n;}}',
    }
    for name, source in inline_namespaces_missing.items():
        check("v2-inline-namespaces-missing-" + name, source, 'TR0203', profile="cpp-core-v2")
    check("v1-inline-namespaces-top", "inline namespace N{int n=3;}", "TR0201")
    check("v1-inline-namespaces-inner", "namespace N{inline namespace V{int n=3;}}", "TR0201")

    constexpr_if_source = """int calls=0;
int tick(){return ++calls;}
int left(){return 3;}
int right(){return 4;}
constexpr bool choose(){return true;}
struct Choice {
 bool value;
 constexpr Choice(bool b):value(b){}
 constexpr explicit operator bool()const{return value;}
};
struct Unused { int data[2]; };
struct Result { int n; };
struct Trace {
 int*trace;int id;
 Trace(int*p,int n):trace(p),id(n){*trace=*trace*10+id;}
 ~Trace(){*trace=*trace*10+id+4;}
};
int selected(){if constexpr(true)return left();else return right();}
int opposite(){if constexpr(false)return left();else return right();}
int folded(){if constexpr(choose())return left();else return right();}
int initializer(){if constexpr(int n=tick();false)return right();else return n;}
int conditionVariable(){if constexpr(const int n=3)return n;else return 0;}
int recordVariable(){if constexpr(constexpr Choice c{true})return c.value;else return 0;}
int temporaryCondition(){if constexpr(Choice{true})return left();else return right();}
auto deduced(){if constexpr(true)return 7;else return Result{9};}
auto recordResult(){if constexpr(false)return 7;else return Result{9};}
void discarded(){if constexpr(false){Unused u{};int a[2]={1,2};}}
int switchDiscard(int n){switch(n){case 0:if constexpr(false){Unused u{};int a[2]={1,2};return a[0];}else return 2;default:return 3;}}
void normal(int&t){if constexpr(Trace one(&t,1);true){Trace two(&t,2);t=t*10+3;}}
void noBody(int&t){if constexpr(Trace one(&t,1);false){Trace unused(&t,2);}}
int captured(int&t){if constexpr(Trace one(&t,1);true){Trace two(&t,2);return t;}else return 0;}
void breaking(int&t){for(int i=0;i<3;++i){if constexpr(Trace one(&t,1);true){Trace two(&t,2);break;}}}
void continuing(int&t){for(int i=0;i<2;++i){if constexpr(Trace one(&t,1);true){Trace two(&t,2);continue;}}}
int main(){return selected()-3;}
"""
    constexpr_if = check("v2-constexpr-if-protocol", constexpr_if_source, profile="cpp-core-v2")
    ci_functions = {f["name"]: f for f in constexpr_if["functions"]}

    def ci_line(prefix):
        lines = [i for i, line in enumerate(constexpr_if_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(lines) == 1, (prefix, lines)
        return lines[0]

    def ci_function(prefix, result, parameters):
        found = [f for f in constexpr_if["functions"] if f["loc"]["line"] == ci_line(prefix)
                 and f["result"] == result and [p["type"] for p in f["params"]] == parameters]
        assert len(found) == 1, (prefix, result, parameters, found)
        return found[0]

    def ci_record(prefix):
        found = [r for r in constexpr_if["records"] if r["loc"]["line"] == ci_line(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0]

    choice = ci_record("struct Choice {")
    unused = ci_record("struct Unused {")
    result = ci_record("struct Result {")
    trace = ci_record("struct Trace {")
    left = ci_function("int left()", "int", [])
    right = ci_function("int right()", "int", [])
    tick = ci_function("int tick()", "int", [])
    chooser = ci_function("constexpr bool choose()", "bool", [])
    constructor = ci_function(" constexpr Choice(", "void", ["ptr:"+choice["id"], "bool"])
    conversion = ci_function(" constexpr explicit operator bool()", "bool", ["cptr:"+choice["id"]])
    for prefix, callee in (("int selected()", left), ("int opposite()", right),
                           ("int folded()", left), ("int temporaryCondition()", left)):
        function = ci_function(prefix, "int", [])
        calls = gc_calls(function)
        assert len(calls) == 1 and calls[0]["callee"] == callee["name"]
        assert not any(n["op"] == "branch" for n in function["body"]), "constexpr condition became a runtime branch"
        assert not any(v["type"] in (choice["id"], unused["id"]) for v in function["locals"])
    initializer = ci_function("int initializer()", "int", [])
    calls = gc_calls(initializer)
    assert len(calls) == 1 and calls[0]["callee"] == tick["name"]
    assert not any(n["op"] == "branch" for n in initializer["body"])
    # Follow value captures back to the call result; the initializer still runs.
    returned = next(n["value"] for n in initializer["body"] if n["op"] == "return")
    while returned["kind"] == "var" and returned["name"] != calls[0]["target"]["name"]:
        values = [n["value"] for n in initializer["body"] if n["op"] == "assign"
                  and n["target"].get("name") == returned["name"]]
        assert len(values) == 1, returned
        returned = values[0]
    assert returned.get("name") == calls[0]["target"]["name"]
    variable = ci_function("int conditionVariable()", "int", [])
    assert [gc_identity(variable, n["value"]) for n in variable["body"] if n["op"] == "return"] == [3]
    assert not gc_calls(variable) and not any(n["op"] == "branch" for n in variable["body"])
    record_variable = ci_function("int recordVariable()", "int", [])
    calls = gc_calls(record_variable)
    assert len(calls) == 1 and calls[0]["callee"] == constructor["name"]
    assert gc_identity(record_variable, calls[0]["args"][1]) == 1
    place = np_pointer(record_variable, calls[0]["args"][0])
    reads = [n for n in walk(record_variable["body"]) if n.get("kind") == "member"]
    assert reads and all(np_place(record_variable, n["args"][0]) == place for n in reads)
    assert all(n["callee"] not in (chooser["name"], conversion["name"]) for n in gc_calls(record_variable))
    deduced = ci_function("auto deduced()", "int", [])
    assert [gc_identity(deduced, n["value"]) for n in deduced["body"] if n["op"] == "return"] == [7]
    assert not any(v["type"] == result["id"] for v in deduced["locals"])
    record_result = ci_function("auto recordResult()", "void", ["ptr:"+result["id"]])
    stores = [n for n in record_result["body"] if n["op"] == "assign" and n["target"].get("kind") == "member"]
    assert len(stores) == 1 and stores[0]["target"]["name"] == result["fields"][0]["name"]
    assert gc_identity(record_result, stores[0]["value"]) == 9
    assert np_place(record_result, stores[0]["target"]["args"][0]) == ("parameter", record_result["params"][0]["name"])
    discarded = ci_function("void discarded()", "void", [])
    assert discarded["locals"] == [] and not gc_calls(discarded)
    assert all(n["op"] in ("label", "return") for n in discarded["body"])
    switched = ci_function("int switchDiscard(", "int", ["int"])
    assert not any(v["type"] == unused["id"] or v["type"].startswith("arr:") for v in switched["locals"]), "switch pre-registration allocated discarded storage"
    assert not gc_calls(switched)
    ctor = ci_function(" Trace(int*", "void", ["ptr:"+trace["id"], "ptr:int", "int"])
    destructor_name = trace["id"]+"_destroy"
    for prefix, result_type, count in (("void normal(", "void", 2), ("void noBody(", "void", 1),
                                        ("int captured(", "int", 2), ("void breaking(", "void", 2),
                                        ("void continuing(", "void", 2)):
        function = ci_function(prefix, result_type, ["ptr:int"])
        calls = gc_calls(function)
        assert [c["callee"] for c in calls] == [ctor["name"]]*count + [destructor_name]*count
        constructed = [np_pointer(function, c["args"][0]) for c in calls[:count]]
        destroyed = [np_pointer(function, c["args"][0]) for c in calls[count:]]
        assert len(set(constructed)) == count and destroyed == list(reversed(constructed))
        assert sum(v["type"] == trace["id"] for v in function["locals"]) == count
        assert [gc_identity(function, c["args"][2]) for c in calls[:count]] == list(range(1, count+1))
        if result_type == "int":
            returned = next(n["value"] for n in function["body"] if n["op"] == "return")
            captures = [i for i, n in enumerate(function["body"]) if n["op"] == "assign" and n["target"].get("name") == returned.get("name")]
            cleanups = [i for i, n in enumerate(function["body"]) if n["op"] == "call" and n["callee"] == destructor_name]
            assert len(captures) == 1 and captures[0] < min(cleanups)
    for function in constexpr_if["functions"]:
        for call in gc_calls(function):
            assert [a["type"] for a in call["args"]] == [p["type"] for p in ci_functions[call["callee"]]["params"]]
    with tempfile.TemporaryDirectory(prefix="neverc-constexpr-if-relocated-") as temp:
        relocated = check("v2-constexpr-if-relocated", constexpr_if_source,
                          root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == constexpr_if, "constexpr-if identities depend on the absolute root"

    constexpr_if_positive = {
        'true': 'int f(){if constexpr(true)return 3;else return 4;}',
        'false': 'int f(){if constexpr(false)return 3;else return 4;}',
        'empty': 'int f(){if constexpr(false)return 3;return 4;}',
        'nested': 'int f(){if constexpr(true){if constexpr(false)return 3;else return 4;}return 0;}',
        'else-if': 'int f(){if constexpr(false)return 1;else if constexpr(true)return 3;else return 4;}',
        'integer': 'int f(){if constexpr(3)return 3;else return 4;}',
        'enum': 'enum E{n=3};int f(){if constexpr(n==3)return 3;else return 4;}',
        'query': 'int f(){if constexpr(sizeof(int)==4&&alignof(int)==4)return 3;else return 4;}',
        'noexcept': 'int f(){if constexpr(noexcept(1+1))return 3;else return 4;}',
        'constexpr-function': 'constexpr bool choose(){int n=2;return ++n==3;}int f(){if constexpr(choose())return 3;else return 4;}',
        'constexpr-member': 'struct R{constexpr bool yes()const{return true;}};int f(){if constexpr(R{}.yes())return 3;else return 4;}',
        'constexpr-conversion': 'struct R{constexpr explicit operator bool()const{return true;}};int f(){if constexpr(R{})return 3;else return 4;}',
        'init-call': 'int count=0;int next(){return ++count;}int f(){if constexpr(int n=next();true)return n;else return 0;}int main(){return f()!=1||f()!=2;}',
        'init-empty': 'int count=0;void f(){if constexpr(++count;false){++count;}}int main(){f();return count-1;}',
        'condition-variable': 'int f(){if constexpr(const int n=3)return n;else return 4;}',
        'condition-record': 'struct R{bool value;constexpr explicit operator bool()const{return value;}};int f(){if constexpr(constexpr R r{true})return r.value;else return 0;}',
        'auto-return': 'struct R{int n;};auto f(){if constexpr(true)return 3;else return R{4};}int main(){return f()-3;}',
        'auto-return-record': 'struct R{int n;};auto f(){if constexpr(false)return 3;else return R{4};}int main(){return f().n-4;}',
        'record-init-lifetime': 'struct R{int*p;R(int*q):p(q){++*p;}~R(){++*p;}};int f(){int n=0;if constexpr(R r(&n);false)++n;return n;}',
        'discarded-record': 'struct R{int*p;R(int*q):p(q){++*p;}~R(){++*p;}};int f(){int n=0;if constexpr(false){R r(&n);}return n;}',
        'loop-break': 'int f(){int n=0;while(true){if constexpr(true){++n;break;}}return n;}',
        'loop-continue': 'int f(){int n=0;for(int i=0;i<2;++i){if constexpr(true){++n;continue;}n=99;}return n;}',
        'outer-switch': 'struct R{int n;};int f(int n){switch(n){case 0:if constexpr(false){R unused{3};int a[2]={1,2};return a[0];}else return 4;default:return 0;}}',
        'inner-switch': 'int f(int n){if constexpr(true){switch(n){case 0:return 3;default:return 4;}}return 0;}',
        'aliases': 'namespace N{int n=3;}int f(){if constexpr(true){namespace A=N;using A::n;return n;}else return 0;}',
        'static-local': 'int f(){if constexpr(true){static int n=3;return ++n;}else return 0;}',
        'macro': '#define YES true\nint f(){if constexpr(YES)return 3;else return 0;}',
    }
    for name, source in constexpr_if_positive.items():
        check("v2-constexpr-if-positive-" + name, source, profile="cpp-core-v2")
    constexpr_if_reject = {
        'discarded-floating': 'int f(){if constexpr(false){double n=3;return static_cast<int>(n);}return 0;}',
        'discarded-new': 'int f(){if constexpr(false){int*p=new int(3);}return 0;}',
        'discarded-throw': 'int f(){if constexpr(false)throw 3;return 0;}',
        'discarded-type': 'int f(){if constexpr(false){using T=double;}return 0;}',
        'discarded-attribute': 'int f(){if constexpr(false){[[maybe_unused]] int n=3;}return 0;}',
        'folded-condition': 'int f(){if constexpr(static_cast<int>(3.0)==3)return 1;else return 0;}',
        'folded-function': 'constexpr bool f(){return static_cast<int>(3.0)==3;}int g(){if constexpr(f())return 1;else return 0;}',
        'inactive-include': '#if 0\n#include "missing.h"\n#endif\nint f(){if constexpr(true)return 3;else return 0;}',
    }
    for name, source in constexpr_if_reject.items():
        check("v2-constexpr-if-reject-" + name, source, 'TR0201', profile="cpp-core-v2")
    constexpr_if_invalid = {
        'if-consteval': 'int f(){if consteval{return 1;}else{return 0;}}',
        'if-not-consteval': 'int f(){if !consteval{return 1;}else{return 0;}}',
        'if-not-keyword': 'int f(){if not consteval{return 1;}else{return 0;}}',
        'nonconstant': 'int f(bool b){if constexpr(b)return 1;else return 0;}',
        'nonconstexpr-call': 'bool f(){return true;}int g(){if constexpr(f())return 1;else return 0;}',
        'nonconstant-variable': 'int f(){if constexpr(int n=3)return n;else return 0;}',
        'discarded-assert': 'int f(){if constexpr(false){static_assert(false,"invalid");}return 0;}',
        'discarded-source-error': 'int f(){if constexpr(false){int n="invalid";}return 0;}',
        'crossing-case': 'void f(int n){switch(n){if constexpr(true){case 0:break;}}}',
        'condition-scope': 'int f(){if constexpr(const int n=3){}return n;}',
        'break-without-loop': 'void f(){if constexpr(true)break;}',
    }
    for name, source in constexpr_if_invalid.items():
        check("v2-constexpr-if-invalid-" + name, source, 'TR0202', profile="cpp-core-v2")
    constexpr_if_missing = {
        'discarded-function': 'int missing();int f(){if constexpr(false)return missing();return 0;}',
        'discarded-global': 'extern int missing;int f(){if constexpr(false)return missing;return 0;}',
    }
    for name, source in constexpr_if_missing.items():
        check("v2-constexpr-if-missing-" + name, source, 'TR0203', profile="cpp-core-v2")
    check("v1-constexpr-if", "int f(){if constexpr(true)return 3;else return 4;}", "TR0201")
    check("v1-consteval-if", "int f(){if consteval{return 3;}else{return 4;}}", "TR0202")

    function_templates_source = """struct R{int n;};
struct Trace{
 int*p;
 Trace(int*q):p(q){++*p;}
 ~Trace(){++*p;}
};
int ticks=0;
int tick(){return ++ticks;}
template<class T>T id(T v){return v;}
int idInt(int n){return id(n);}
int idExplicit(int n){return id<int>(n);}
bool idBool(bool b){return id(b);}
R idRecord(R r){return id(r);}
template<class T>T& alias(T&v){return v;}
int& aliasInt(int&n){return alias(n);}
template<class T>int counter(){static int n=0;return ++n;}
int countInt(){return counter<int>();}
int countIntAgain(){return counter<int>();}
int countBool(){return counter<bool>();}
template<class T>int defaulted(T v=T{},int extra=tick()){return v+extra;}
int defaultZero(){return defaulted<int>();}
int defaultValue(){return defaulted(3);}
template<class T>int lazy(T v=T::missing){return v;}
int lazyUsed(){return lazy(3);}
template<class T>int recursive(T n){return n?1+recursive<T>(n-1):0;}
int recurse(){return recursive(2);}
template<class T>auto selected(int*p){if constexpr(sizeof(T)==sizeof(int)){Trace guard(p);return *p;}else{return R{9};}}
int selectedInt(int*p){return selected<int>(p);}
R selectedBool(int*p){return selected<bool>(p);}
template<class T>int overloaded(T v){return 1;}
int early(){return overloaded(1);}
template<class T>int overloaded(int v){return 2;}
int late(){return overloaded<int>(1);}
namespace Collision{
 template<class T>int count(T v){struct Local{int n;};Local x{1};static int n=0;return ++n+x.n;}
 int earlyCount(){return count(1);}
 template<class T>int count(int v){struct Local{int n;};Local x{2};static int n=10;return ++n+x.n;}
 int lateCount(){return count<int>(1);}
}
template<class T>auto first(T);
template<class T>auto second(T n){struct Local{T n;};return Local{n};}
template<class T>auto first(T n){return second(n);}
int fromLocal(){return first(3).n;}
#define PRIMARY(M) template<class T>auto macro(T v)->decltype(v.M){return v.M;}
#define PAIR PRIMARY(a) PRIMARY(b)
struct MacroTypes{int a;bool b;};
PAIR
template int macro<MacroTypes>(MacroTypes);
template bool macro<MacroTypes>(MacroTypes);
template<class T>double unused(T n){return n+1.0;}
template<class T>int specialized(T n){return 1;}
template<>int specialized<int>(int n){return 7;}
int specialization(){return specialized(3);}
namespace Import{template<class T>T take(T n){return n;}}
using Import::take;
int imported(){return take(3);}
template<class T>int candidate(T n){double x=1.0;return 4;}
int candidate(int n){return 5;}
int chooseOrdinary(){return candidate(3);}
"""
    function_templates = check("v2-function-templates-protocol", function_templates_source, profile="cpp-core-v2")
    ft_functions = {f["name"]: f for f in function_templates["functions"]}
    ft_records = {r["id"]: r for r in function_templates["records"]}
    ft_globals = {g["name"]: g for g in function_templates["globals"]}
    assert len(ft_functions) == len(function_templates["functions"]), "template function identity collision"
    assert len(ft_records) == len(function_templates["records"]), "template record identity collision"
    assert len(ft_globals) == len(function_templates["globals"]), "template static identity collision"

    def ft_line(prefix):
        lines = [i for i, line in enumerate(function_templates_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(lines) == 1, (prefix, lines)
        return lines[0]

    def ft_function(prefix):
        matches = [f for f in ft_functions.values() if f["loc"]["line"] == ft_line(prefix)]
        assert len(matches) == 1, (prefix, matches)
        return matches[0]

    def ft_selected(prefix):
        function = ft_function(prefix)
        calls = gc_calls(function)
        assert len(calls) == 1, (prefix, calls)
        return ft_functions[calls[0]["callee"]]

    def ft_signature(function, result, parameters):
        assert function["result"] == result and [p["type"] for p in function["params"]] == parameters, function

    def ft_return_literal(function):
        return [gc_identity(function, n["value"]) for n in function["body"] if n["op"] == "return"]

    record = next(r for r in ft_records.values() if r["loc"]["line"] == ft_line("struct R{"))
    trace = next(r for r in ft_records.values() if r["loc"]["line"] == ft_line("struct Trace{"))
    id_int, id_bool, id_record = (ft_selected(p) for p in ("int idInt(", "bool idBool(", "R idRecord("))
    assert id_int == ft_selected("int idExplicit(")
    assert len({f["name"] for f in (id_int, id_bool, id_record)}) == 3
    ft_signature(id_int, "int", ["int"])
    ft_signature(id_bool, "bool", ["bool"])
    ft_signature(id_record, "void", ["ptr:"+record["id"], "ptr:"+record["id"]])
    assert len([f for f in ft_functions.values() if f["loc"]["line"] == ft_line("template<class T>T id(")]) == 3
    alias = ft_selected("int& aliasInt(")
    ft_signature(alias, "ptr:int", ["ptr:int"])
    returned = next(n["value"] for n in alias["body"] if n["op"] == "return")
    assert np_pointer(alias, returned) == ("parameter", alias["params"][0]["name"])

    counter_int, counter_bool = ft_selected("int countInt("), ft_selected("int countBool(")
    assert counter_int == ft_selected("int countIntAgain(") and counter_int["name"] != counter_bool["name"]
    def ft_static(function):
        targets = {n["target"]["name"] for n in function["body"] if n["op"] == "assign"
                   and n["target"].get("kind") == "var" and n["target"]["name"] in ft_globals}
        assert len(targets) == 1, (function, targets)
        value = ft_globals[next(iter(targets))]
        assert value["mutable"] and value["type"] == "int"
        return value
    count_globals = [ft_static(f) for f in (counter_int, counter_bool)]
    assert count_globals[0]["name"] != count_globals[1]["name"]
    assert all(g["value"]["value"] == "0" for g in count_globals)
    tick = ft_function("int tick(")
    default_targets = []
    for prefix, argument in (("int defaultZero(", 0), ("int defaultValue(", 3)):
        function = ft_function(prefix)
        calls = gc_calls(function)
        assert len(calls) == 2 and calls[0]["callee"] == tick["name"]
        default_targets.append(calls[1]["callee"])
        assert gc_identity(function, calls[1]["args"][0]) == argument
        assert calls[1]["args"][1]["kind"] == "var" and calls[1]["args"][1]["name"] == calls[0]["target"]["name"]
    assert len(set(default_targets)) == 1
    ft_signature(ft_functions[default_targets[0]], "int", ["int", "int"])
    ft_signature(ft_selected("int lazyUsed("), "int", ["int"])
    recursive = ft_selected("int recurse(")
    assert [c["callee"] for c in gc_calls(recursive)] == [recursive["name"]]
    chosen_int, chosen_bool = ft_selected("int selectedInt("), ft_selected("R selectedBool(")
    ft_signature(chosen_int, "int", ["ptr:int"])
    ft_signature(chosen_bool, "void", ["ptr:"+record["id"], "ptr:int"])
    constructor = ft_function(" Trace(int*")
    calls = gc_calls(chosen_int)
    assert [c["callee"] for c in calls] == [constructor["name"], trace["id"]+"_destroy"]
    assert np_pointer(chosen_int, calls[0]["args"][0]) == np_pointer(chosen_int, calls[1]["args"][0])
    returned = next(n["value"] for n in chosen_int["body"] if n["op"] == "return")
    captures = [i for i, n in enumerate(chosen_int["body"]) if n["op"] == "assign" and n["target"].get("name") == returned.get("name")]
    cleanup = next(i for i, n in enumerate(chosen_int["body"]) if n["op"] == "call" and n["callee"] == trace["id"]+"_destroy")
    assert len(captures) == 1 and captures[0] < cleanup
    assert not gc_calls(chosen_bool) and not any(v["type"] == trace["id"] for v in chosen_bool["locals"])
    stores = [n for n in chosen_bool["body"] if n["op"] == "assign" and n["target"].get("kind") == "member"]
    assert len(stores) == 1 and gc_identity(chosen_bool, stores[0]["value"]) == 9

    early, late = ft_selected("int early("), ft_selected("int late(")
    assert early["name"] != late["name"]
    for function, value in ((early, 1), (late, 2)):
        ft_signature(function, "int", ["int"])
        assert ft_return_literal(function) == [value]
    first_count, second_count = ft_selected(" int earlyCount("), ft_selected(" int lateCount(")
    assert first_count["name"] != second_count["name"]
    first_static, second_static = ft_static(first_count), ft_static(second_count)
    assert first_static["name"] != second_static["name"]
    assert [g["value"]["value"] for g in (first_static, second_static)] == ["0", "10"]
    local_types = [{v["type"] for v in f["locals"] if v["type"] in ft_records} for f in (first_count, second_count)]
    assert all(len(types) == 1 for types in local_types) and local_types[0].isdisjoint(local_types[1])
    local_records = [ft_records[next(iter(types))] for types in local_types]
    assert local_records[0]["fields"][0]["name"] != local_records[1]["fields"][0]["name"]
    first = ft_selected("int fromLocal(")
    second_calls = gc_calls(first)
    assert len(second_calls) == 1
    second = ft_functions[second_calls[0]["callee"]]
    assert first["name"] != second["name"]
    assert first["result"] == second["result"] == "void"
    assert [p["type"] for p in first["params"]] == [p["type"] for p in second["params"]]
    assert first["params"][0]["type"].startswith("ptr:") and first["params"][0]["type"][4:] in ft_records
    macro = [f for f in ft_functions.values() if f["loc"]["line"] == ft_line("PAIR")]
    assert len(macro) == 2 and {f["result"] for f in macro} == {"int", "bool"}
    macro_record = next(r for r in ft_records.values() if r["loc"]["line"] == ft_line("struct MacroTypes{"))
    assert all([p["type"] for p in f["params"]] == ["ptr:"+macro_record["id"]] for f in macro)
    assert not any(f["loc"]["line"] == ft_line("template<class T>double unused(") for f in ft_functions.values())
    assert ft_return_literal(ft_selected("int specialization(")) == [7]
    assert ft_selected("int imported(")["loc"]["line"] == ft_line("namespace Import{")
    ordinary = ft_selected("int chooseOrdinary(")
    assert ordinary["loc"]["line"] == ft_line("int candidate(") and ft_return_literal(ordinary) == [5]
    assert not any(f["loc"]["line"] == ft_line("template<class T>int candidate(") for f in ft_functions.values())
    for function in ft_functions.values():
        for call in gc_calls(function):
            assert call["callee"] in ft_functions
            assert [a["type"] for a in call["args"]] == [p["type"] for p in ft_functions[call["callee"]]["params"]]
    with tempfile.TemporaryDirectory(prefix="neverc-function-templates-relocated-") as temp:
        relocated = check("v2-function-templates-relocated", function_templates_source,
                          root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == function_templates, "template identity depends on absolute paths or address order"

    imported_defaults_source = """int ticks=0;
int live=0;
int tick(){return ++ticks;}
struct Guard{int n;Guard(int v):n(v){++live;}~Guard(){--live;}};
namespace Source{int plain(int);int multiple(int,int);int effect(int);int lifetime(const Guard&);int choose(int,int);int choose(bool b){return 9;}}
namespace First{using Source::plain;using Source::multiple;using Source::effect;using Source::lifetime;using Source::choose;}
namespace Second{using First::plain;using First::multiple;using First::effect;using First::lifetime;using First::choose;}
int before(){return Second::plain(7);}
namespace Source{int multiple(int,int b=4);}
int Source::plain(int n=3){return n;}
namespace Source{
int multiple(int a=3,int b){return a*10+b;}
int effect(int n=tick()){return n;}
int lifetime(const Guard&g=Guard(5)){return g.n+live;}
int choose(int n,int extra=2){return n+extra;}
}
namespace Closed{int select(int n){return n+10;}}
namespace Captured{using Closed::select;}
namespace Closed{int select(bool b){return 99;}}
int defaultValue(){return Second::plain();}
int blockValue(){using Second::plain;return plain();}
int twoDefaults(){return Second::multiple();}
int oneDefault(){return Second::multiple(2);}
int omittedEffect(){return Second::effect();}
int explicitEffect(){return Second::effect(7);}
int temporaryDefault(){return Second::lifetime();}
int chooseInt(){return Second::choose(3);}
int chooseBool(){return Second::choose(true);}
int closedOverloads(){return Captured::select(true);}
"""
    imported_defaults = check("v2-imported-defaults-protocol", imported_defaults_source, profile="cpp-core-v2")
    di_functions = {f["name"]: f for f in imported_defaults["functions"]}
    assert len(di_functions) == len(imported_defaults["functions"])

    def di_function(prefix):
        lines = [i for i, line in enumerate(imported_defaults_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(lines) == 1, (prefix, lines)
        found = [f for f in imported_defaults["functions"] if f["loc"]["line"] == lines[0]]
        assert len(found) == 1, (prefix, found)
        return found[0]

    plain = di_function("int Source::plain(")
    multiple = di_function("int multiple(")
    effect = di_function("int effect(")
    tick = di_function("int tick(")
    for prefix, target, values in (("int before(", plain, [7]),
                                   ("int defaultValue(", plain, [3]),
                                   ("int blockValue(", plain, [3]),
                                   ("int twoDefaults(", multiple, [3, 4]),
                                   ("int oneDefault(", multiple, [2, 4]),
                                   ("int explicitEffect(", effect, [7])):
        caller = di_function(prefix)
        calls = gc_calls(caller)
        assert len(calls) == 1 and calls[0]["callee"] == target["name"]
        assert [gc_identity(caller, a) for a in calls[0]["args"]] == values
    omitted = gc_calls(di_function("int omittedEffect("))
    assert [call["callee"] for call in omitted] == [tick["name"], effect["name"]]
    assert len(omitted[1]["args"]) == 1 and omitted[1]["args"][0]["type"] == "int"
    def di_call_result(function, expr):
        if expr["kind"] == "cast":
            return di_call_result(function, expr["args"][0])
        assert expr["kind"] == "var", expr
        name = expr["name"]
        if any(call.get("target", {}).get("name") == name for call in gc_calls(function)):
            return name
        values = [n["value"] for n in function["body"] if n["op"] == "assign"
                  and n["target"].get("kind") == "var" and n["target"]["name"] == name]
        assert len(values) == 1, (name, values)
        return di_call_result(function, values[0])

    assert di_call_result(di_function("int omittedEffect("), omitted[1]["args"][0]) == omitted[0]["target"]["name"]
    lifetime = di_function("int lifetime(")
    lifetime_calls = gc_calls(di_function("int temporaryDefault("))
    assert len(lifetime_calls) == 3 and lifetime_calls[1]["callee"] == lifetime["name"]
    constructor, destructor = [di_functions[lifetime_calls[i]["callee"]] for i in (0, 2)]
    receiver = lifetime["params"][0]["type"]
    assert receiver.startswith("ptr:")
    assert [p["type"] for p in constructor["params"]] == [receiver, "int"]
    assert [p["type"] for p in destructor["params"]] == [receiver]
    caller = di_function("int temporaryDefault(")
    assert gc_identity(caller, lifetime_calls[0]["args"][1]) == 5
    assert np_pointer(caller, lifetime_calls[0]["args"][0]) == np_pointer(caller, lifetime_calls[1]["args"][0])
    assert np_pointer(caller, lifetime_calls[0]["args"][0]) == np_pointer(caller, lifetime_calls[2]["args"][0])
    integer = gc_calls(di_function("int chooseInt("))
    boolean = gc_calls(di_function("int chooseBool("))
    closed = gc_calls(di_function("int closedOverloads("))
    assert len(integer) == len(boolean) == len(closed) == 1
    assert [p["type"] for p in di_functions[integer[0]["callee"]]["params"]] == ["int", "int"]
    assert [p["type"] for p in di_functions[boolean[0]["callee"]]["params"]] == ["bool"]
    assert [p["type"] for p in di_functions[closed[0]["callee"]]["params"]] == ["int"]
    for function in imported_defaults["functions"]:
        for call in gc_calls(function):
            assert call["callee"] in di_functions
            assert [a["type"] for a in call["args"]] == [p["type"] for p in di_functions[call["callee"]]["params"]]
    with tempfile.TemporaryDirectory(prefix="neverc-imported-defaults-relocated-") as temp:
        relocated = check("v2-imported-defaults-relocated", imported_defaults_source,
                          root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == imported_defaults

    imported_defaults_positive = {
        'late-default': 'namespace N{int f(int);}using N::f;namespace N{int f(int n=3){return n;}}int main(){return f()-3;}',
        'qualified-definition': 'namespace N{int f(int);}using N::f;int N::f(int n=3){return n;}int main(){return f()-3;}',
        'namespace-import': 'namespace N{int f(int);}namespace A{using N::f;}namespace N{int f(int n=3){return n;}}int main(){return A::f()-3;}',
        'block-import': 'namespace N{int f(int);}namespace A{using N::f;}namespace N{int f(int n=3){return n;}}int main(){using A::f;return f()-3;}',
        'reexport': 'namespace N{int f(int);}namespace A{using N::f;}namespace B{using A::f;}namespace N{int f(int n=3){return n;}}int main(){return B::f()-3;}',
        'inline-namespace': 'namespace N{inline namespace V{int f(int);}}using N::f;namespace N{namespace V{int f(int n=3){return n;}}}int main(){return f()-3;}',
        'multiple-additions': 'namespace N{int f(int,int);}using N::f;namespace N{int f(int,int b=4);}namespace N{int f(int a=3,int b){return a*10+b;}}int main(){return f()-34+f(2)-24;}',
        'overload-viability': 'namespace N{int f(int,int);int f(bool){return 9;}}using N::f;namespace N{int f(int n,int extra=2){return n+extra;}}int main(){return f(3)-5+f(true)-9;}',
        'no-new-overload': 'namespace N{int f(int n){return n+10;}}using N::f;namespace N{int f(bool){return 99;}}int main(){return f(true)-11;}',
        'namespace-binding': 'int value=99;namespace N{int value=3;int f(int);}using N::f;namespace N{int f(int n=value){return n;}}int main(){return f()-3;}',
        'side-effects': 'int ticks=0;int tick(){return ++ticks;}namespace N{int f(int);}using N::f;namespace N{int f(int n=tick()){return n;}}int main(){int a=f();int b=f(7);int d=f();return a-1+b-7+d-2+ticks-2;}',
        'const-reference-default': 'int live=0;struct R{int n;R(int v):n(v){++live;}~R(){--live;}};namespace N{int f(const R&);}using N::f;namespace N{int f(const R&r=R(3)){return r.n+live;}}int main(){int n=f();return n-4+live;}',
        'array-reference-default': 'namespace N{struct A{int a[2];};int f(const int(&)[2]);}using N::f;namespace N{int f(const int(&v)[2]=A{{3,4}}.a){return v[0]+v[1];}}int main(){return f()-7;}',
        'prior-explicit-call': 'namespace N{int f(int);}using N::f;int before(){return f(4);}namespace N{int f(int n=3){return n;}}int main(){return before()-4+f()-3;}',
        'cross-namespace-c-defaults': 'namespace A{extern "C" int f(int);}using A::f;namespace B{extern "C" int f(int n=8);}namespace A{extern "C" int f(int n=3){return n;}}int main(){return f()-3;}',
    }
    for name, source in imported_defaults_positive.items():
        check("v2-imported-defaults-positive-" + name, source, profile="cpp-core-v2")
    imported_defaults_invalid = {
        'before-default': 'namespace N{int f(int);}using N::f;int before(){return f();}namespace N{int f(int n=3){return n;}}',
        'new-zero-argument-overload': 'namespace N{int f(int n){return n;}}using N::f;namespace N{int f(){return 3;}}int main(){return f();}',
        'block-default-escape': 'namespace N{int f(int);void local(){int f(int n=3);f();}}using N::f;namespace N{int f(int n){return n;}}int main(){return f();}',
        'block-default-after-namespace': 'namespace N{int f(int);}using N::f;namespace N{void local(){int f(int n=3);f();}int f(int n){return n;}}int main(){return f();}',
        'cross-namespace-c-escape': 'namespace A{extern "C" int f(int);}using A::f;namespace B{extern "C" int f(int n=8);}namespace A{extern "C" int f(int n){return n;}}int main(){return f();}',
        'duplicate-default': 'namespace N{int f(int n=3);}using N::f;namespace N{int f(int n=3){return n;}}int main(){return f();}',
    }
    for name, source in imported_defaults_invalid.items():
        check("v2-imported-defaults-invalid-" + name, source, "TR0202", profile="cpp-core-v2")

    friend_declarations_source = """int ticks=0;
int live=0;
struct Guard{int n;Guard(int v):n(v){++live;}~Guard(){--live;}};
class Outer{
 using Hidden=int;
 static const int secret=9;
public:
 class Inner{
  using Value=Hidden;
  static int next(){return ++ticks;}
 public:
  int n;
  friend Value read(Inner v,Value add){return v.n+add;}
  friend int effect(Inner,int value=next()){return value;}
  friend int guard(Inner,const Guard&g=Guard(5)){return g.n+live;}
 };
 struct Member{int get(int value=secret)const{return value;}};
 static const int visible=7;
 struct Public{friend int publicDefault(Public,int value=Outer::visible){return value;}};
};
int granted(int);
class Grant{using Value=int;friend int granted(int);public:struct Inner{friend Value granted(int n){return n;}};};
int readCall(int n){return read(Outer::Inner{n},2);}
int omittedCall(){return effect(Outer::Inner{3});}
int explicitCall(){return effect(Outer::Inner{3},7);}
int guardCall(){return guard(Outer::Inner{3});}
int memberCall(){Outer::Member m;return m.get();}
int publicCall(){return publicDefault(Outer::Public{});}
int grantedCall(){return granted(6);}
"""
    friend_declarations = check("v2-friend-declarations-protocol", friend_declarations_source, profile="cpp-core-v2")
    fd_functions = {f["name"]: f for f in friend_declarations["functions"]}
    assert len(fd_functions) == len(friend_declarations["functions"])

    def fd_function(prefix):
        lines = [i for i, line in enumerate(friend_declarations_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(lines) == 1, (prefix, lines)
        found = [f for f in friend_declarations["functions"] if f["loc"]["line"] == lines[0]]
        assert len(found) == 1, (prefix, found)
        return found[0]

    read = fd_function("  friend Value read(")
    effect = fd_function("  friend int effect(")
    next_value = fd_function("  static int next(")
    guard = fd_function("  friend int guard(")
    assert read["result"] == effect["result"] == guard["result"] == "int"
    assert len(read["params"]) == len(effect["params"]) == len(guard["params"]) == 2
    receiver = read["params"][0]["type"]
    assert receiver.startswith("ptr:") and effect["params"][0]["type"] == guard["params"][0]["type"] == receiver
    assert read["params"][1]["type"] == effect["params"][1]["type"] == "int"
    assert not next_value["params"], "static default helper has no receiver"
    omitted = gc_calls(fd_function("int omittedCall("))
    explicit = gc_calls(fd_function("int explicitCall("))
    assert [call["callee"] for call in omitted] == [next_value["name"], effect["name"]]
    assert di_call_result(fd_function("int omittedCall("), omitted[1]["args"][1]) == omitted[0]["target"]["name"]
    assert len(explicit) == 1 and explicit[0]["callee"] == effect["name"]
    assert gc_identity(fd_function("int explicitCall("), explicit[0]["args"][1]) == 7
    caller = fd_function("int guardCall(")
    calls = gc_calls(caller)
    assert len(calls) == 3 and calls[1]["callee"] == guard["name"]
    constructor, destructor = [fd_functions[calls[i]["callee"]] for i in (0, 2)]
    guard_pointer = guard["params"][1]["type"]
    assert [p["type"] for p in constructor["params"]] == [guard_pointer, "int"]
    assert [p["type"] for p in destructor["params"]] == [guard_pointer]
    assert gc_identity(caller, calls[0]["args"][1]) == 5
    assert np_pointer(caller, calls[0]["args"][0]) == np_pointer(caller, calls[1]["args"][1])
    assert np_pointer(caller, calls[0]["args"][0]) == np_pointer(caller, calls[2]["args"][0])
    for function in friend_declarations["functions"]:
        for call in gc_calls(function):
            assert call["callee"] in fd_functions
            assert [a["type"] for a in call["args"]] == [p["type"] for p in fd_functions[call["callee"]]["params"]]
    with tempfile.TemporaryDirectory(prefix="neverc-friend-declarations-relocated-") as temp:
        relocated = check("v2-friend-declarations-relocated", friend_declarations_source,
                          root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == friend_declarations

    friend_declarations_positive = {
        'nonfunction-type-friend': 'class R{class Hidden{};public:struct I{friend class R::Hidden;};};',
        'own-return-alias': 'class R{public:class I{using Value=int;public:friend Value get(I,int n){return n;}};};int main(){return get(R::I{},3)-3;}',
        'own-parameter-alias': 'class R{public:class I{using Value=int;public:friend int get(I,Value n){return n;}};};int main(){return get(R::I{},3)-3;}',
        'inner-alias-to-outer': 'class R{using Hidden=int;public:class I{using Value=Hidden;public:friend Value get(I,Value n){return n;}};};int main(){return get(R::I{},3)-3;}',
        'own-default-constant': 'class R{public:class I{static const int n=3;public:friend int get(I,int value=n){return value;}};};int main(){return get(R::I{})-3;}',
        'own-default-call': 'int ticks=0;class R{public:class I{static int next(){return ++ticks;}public:friend int get(I,int n=next()){return n;}};};int main(){int a=get(R::I{});int b=get(R::I{},7);return a-1+b-7+ticks-1;}',
        'own-array-bound': 'class R{public:class I{static const int n=2;public:friend int get(I,int(&v)[n]){return v[0]+v[1];}};};int main(){int a[2]={3,4};return get(R::I{},a)-7;}',
        'public-return-alias': 'class R{public:using Value=int;struct I{friend Value get(I,int n){return n;}};};int main(){return get(R::I{},3)-3;}',
        'public-default': 'class R{public:static const int n=3;struct I{friend int get(I,int value=R::n){return value;}};};int main(){return get(R::I{})-3;}',
        'explicit-outer-grant': 'int get(int);class R{using Value=int;friend int get(int);public:struct I{friend Value get(int n){return n;}};};int main(){return get(3)-3;}',
        'ordinary-nested-member': 'class R{using Value=int;static const int n=3;public:struct I{Value get(int value=n)const{return value;}};};int main(){R::I i;return i.get()-3;}',
        'own-trailing-return': 'class R{public:class I{using Value=int;public:friend auto get(I,int n)->Value{return n;}};};int main(){return get(R::I{},3)-3;}',
        'deep-own-alias': 'class Outer{public:class R{public:class I{using Value=int;public:friend Value get(I,int n){return n;}};};};int main(){return get(Outer::R::I{},3)-3;}',
    }
    for name, source in friend_declarations_positive.items():
        check("v2-friend-declarations-positive-" + name, source, profile="cpp-core-v2")
    friend_declarations_invalid = {
        'outer-parameter-alias': 'class R{using Value=int;public:struct I{friend int get(I,Value n){return n;}};};',
        'outer-trailing-return': 'class R{using Value=int;public:struct I{friend auto get(I)->Value{return 1;}};};',
        'outer-qualified-return': 'class R{using Value=int;public:struct I{friend R::Value get(I){return 1;}};};',
        'outer-return-decltype': 'class R{static int make(){return 3;}public:struct I{friend auto get(I)->decltype(R::make()){return 1;}};};',
        'outer-array-bound': 'class R{static const int n=2;public:struct I{friend int get(I,int(&v)[R::n]){return v[0];}};};',
        'outer-protected-alias': 'class R{protected:using Value=int;public:struct I{friend Value get(I){return 1;}};};',
        'outer-private-parameter-type': 'class R{struct Hidden{};public:struct I{friend int get(I,Hidden){return 1;}};};',
        'outer-default-type': 'class R{struct Hidden{};public:struct I{friend int get(I,int n=sizeof(R::Hidden)){return n;}};};',
        'outer-default-call': 'class R{static int next(){return 3;}public:struct I{friend int get(I,int n=R::next()){return n;}};};',
        'outer-default-constructor': 'class R{R(int){}public:struct I{friend int get(I,const R&v=R(3)){return 1;}};};',
        'outer-default-destructor': 'class R{~R(){}public:R(int){}struct I{friend int get(I,const R&v=R(3)){return 1;}};};',
        'unrelated-overload-grant': 'int get(bool);class R{using Value=int;friend int get(bool);public:struct I{friend Value get(int n){return n;}};};int get(bool){return 0;}',
        'deep-outer-alias': 'class Outer{using Value=int;public:struct R{struct I{friend Value get(I){return 1;}};};};',
        'immediate-nomination': 'class D{class E{class F{};friend void use(D::E::F&);};friend void use(D::E::F&);};',
        'deep-immediate-nomination': 'class Outer{class D{class E{class F{};friend void use(D::E::F&);};friend void use(D::E::F&);};};',
    }
    for name, source in friend_declarations_invalid.items():
        check("v2-friend-declarations-invalid-" + name, source, "TR0202", profile="cpp-core-v2")
    friend_declarations_reject = {
        'nonfunction-template-friend': 'class R{template<class T>struct Hidden{};public:struct I{template<class T>friend struct R::Hidden;};};',
    }
    for name, source in friend_declarations_reject.items():
        check("v2-friend-declarations-reject-" + name, source, "TR0201", profile="cpp-core-v2")

    frontend_repairs_source = """namespace Original{int count=3;using Value=int;int read(int v){return v;}}
namespace First{using Original::count;using Original::Value;using Original::read;}
namespace Second{using First::count;using First::Value;using First::read;}
template<class T>int query(T v)noexcept(sizeof(T)==sizeof(int)){return v;}
template<int N>int valueQuery()noexcept(N>0){return N;}
template<class T>int (parenthesized)(T v)noexcept(sizeof(T)==sizeof(int)){return v;}
template<class T,int N>struct Box{T n;Box(T v)noexcept(N>0):n(v){}T get()const noexcept(sizeof(T)==sizeof(int)){return n;}};
template<class T>struct Later{T n;Later(T v)noexcept(sizeof(T)==sizeof(int));T get()const noexcept(sizeof(T)==sizeof(int));};
template<class T>Later<T>::Later(T v)noexcept(sizeof(T)==sizeof(int)):n(v){}
template<class T>T Later<T>::get()const noexcept(sizeof(T)==sizeof(int)){return n;}
int callInt(int v){return query(v);}
int callBool(bool v){return query(v);}
int callThree(){return valueQuery<3>();}
int callZero(){return valueQuery<0>();}
int callParens(int v){return parenthesized(v);}
int callBox(int v){Box<int,3>b(v);return b.get();}
int callFalseBox(int v){Box<int,0>b(v);return b.get();}
int callLater(int v){Later<int>b(v);return b.get();}
bool intFlag(){return noexcept(query(1));}
bool boolFlag(){return noexcept(query(true));}
bool threeFlag(){return noexcept(valueQuery<3>());}
bool zeroFlag(){return noexcept(valueQuery<0>());}
bool ctorFlag(){return noexcept(Box<int,3>(1));}
bool falseCtorFlag(){return noexcept(Box<int,0>(1));}
int imports(){using Second::Value;using Second::read;Value n=Second::count;return read(n);}
"""
    frontend_repairs = check("v2-frontend-repairs-protocol", frontend_repairs_source, profile="cpp-core-v2")
    fr_functions = {f["name"]: f for f in frontend_repairs["functions"]}
    assert len(fr_functions) == len(frontend_repairs["functions"])

    def fr_function(prefix):
        lines = [i for i, line in enumerate(frontend_repairs_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(lines) == 1, (prefix, lines)
        found = [f for f in frontend_repairs["functions"] if f["loc"]["line"] == lines[0]]
        assert len(found) == 1, (prefix, found)
        return found[0]

    for prefix, value in (("bool intFlag(", True), ("bool boolFlag(", False),
                          ("bool threeFlag(", True), ("bool zeroFlag(", False),
                          ("bool ctorFlag(", True), ("bool falseCtorFlag(", False)):
        function = fr_function(prefix)
        assert function["result"] == "bool" and not function["params"]
        assert not gc_calls(function), "noexcept operand must not execute"
        literals = [n for n in walk(function["body"]) if n.get("kind") == "literal" and n.get("type") == "bool"]
        assert literals and all(n["value"] is value for n in literals), (prefix, literals)
    targets = []
    for prefix, params in (("int callInt(", ["int"]), ("int callBool(", ["bool"]),
                           ("int callThree(", []), ("int callZero(", []),
                           ("int callParens(", ["int"])):
        calls = gc_calls(fr_function(prefix))
        assert len(calls) == 1
        target = fr_functions[calls[0]["callee"]]
        assert target["result"] == "int" and [p["type"] for p in target["params"]] == params
        targets.append(target["name"])
    assert len(targets) == len(set(targets))
    for function in frontend_repairs["functions"]:
        for call in gc_calls(function):
            assert call["callee"] in fr_functions
            assert [a["type"] for a in call["args"]] == [p["type"] for p in fr_functions[call["callee"]]["params"]]
    with tempfile.TemporaryDirectory(prefix="neverc-frontend-repairs-relocated-") as temp:
        relocated = check("v2-frontend-repairs-relocated", frontend_repairs_source,
                          root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == frontend_repairs

    frontend_repairs_positive = {
        'shadow-reexports': 'namespace N{int n=3;using T=int;int f(int v){return v;}}namespace A{using N::n;using N::T;using N::f;}namespace B{using A::n;using A::T;using A::f;}int main(){using B::T;using B::f;T v=B::n;return f(v)-3;}',
        'noexcept-resolved': 'template<class T>int f(T v)noexcept(sizeof(T)==sizeof(int)){return v;}int main(){return f(3)-3;}',
        'noexcept-false': 'template<class T>int f(T v)noexcept(sizeof(T)!=sizeof(int)){return v;}int main(){return f(3)-3+noexcept(f(3));}',
        'noexcept-parens': 'template<class T>int (f)(T v)noexcept(sizeof(T)==sizeof(int)){return v;}int main(){return f(3)-3;}',
        'noexcept-value': 'template<int N>int f()noexcept(N>0){return N;}int main(){return f<3>()-3+f<0>();}',
        'noexcept-method': 'template<class T>struct R{T n;T get()const noexcept(sizeof(T)==sizeof(int)){return n;}};int main(){R<int>r{3};return r.get()-3;}',
        'noexcept-method-value': 'template<int N>struct R{int get()const noexcept(N>0){return N;}};int main(){R<3>a;R<0>b;return a.get()-3+b.get();}',
        'noexcept-constructor': 'template<class T>struct R{T n;R(T v)noexcept(sizeof(T)==sizeof(int)):n(v){}};int main(){R<int>r(3);return r.n-3;}',
        'noexcept-constructor-value': 'template<int N>struct R{int n;R()noexcept(N>0):n(N){}};int main(){R<3>a;R<0>b;return a.n-3+b.n;}',
        'noexcept-out-of-line': 'template<class T>struct R{T n;R(T v)noexcept(sizeof(T)==sizeof(int));T get()const noexcept(sizeof(T)==sizeof(int));};template<class T>R<T>::R(T v)noexcept(sizeof(T)==sizeof(int)):n(v){}template<class T>T R<T>::get()const noexcept(sizeof(T)==sizeof(int)){return n;}int main(){R<int>r(3);return r.get()-3;}',
        'noexcept-explicit-specialization': 'template<class T>int f(T v)noexcept(sizeof(T)==sizeof(int)){return v;}template<>int f<int>(int v)noexcept(true){return v+1;}int main(){return f(3)-4;}',
        'noexcept-local-method': 'template<class T>int f(T v)noexcept(sizeof(T)==sizeof(int)){struct R{int get()const noexcept(sizeof(int)==4){return 3;}};R r;return r.get()+v;}int main(){return f(3)-6;}',
        'noexcept-written-decltype': 'template<class T>auto f(decltype(static_cast<T>(1)) v)noexcept(sizeof(T)==sizeof(int))->decltype(static_cast<T>(1)){return v;}int main(){return f<int>(3)-3;}',
        'consteval-local': 'int main(){int consteval=3;return consteval-3;}',
        'consteval-function': 'int consteval(int n){return n;}int main(){return consteval(3)-3;}',
        'consteval-template': 'template<class T>int f(T consteval){if constexpr(sizeof(T)==sizeof(int))return consteval;else return 0;}int main(){return f(3)-3;}',
    }
    for name, source in frontend_repairs_positive.items():
        check("v2-frontend-repairs-positive-" + name, source, profile="cpp-core-v2")
    frontend_repairs_reject = {
        'noexcept-selected-floating': 'template<class T>int f(T v)noexcept(sizeof(T)==sizeof(int)&&1.0>0.0){return v;}int main(){return f(3);}',
        'noexcept-method-floating': 'template<class T>struct R{int get()const noexcept(sizeof(T)==sizeof(int)&&1.0>0.0){return 3;}};int main(){R<int>r;return r.get();}',
        'noexcept-constructor-floating': 'template<class T>struct R{int n;R()noexcept(sizeof(T)==sizeof(int)&&1.0>0.0):n(3){}};int main(){R<int>r;return r.n;}',
        'noexcept-return-source': 'template<class T>auto f(T v)noexcept(sizeof(T)==sizeof(int))->decltype(static_cast<T>(1.0)){return v;}int main(){return f(3);}',
        'noexcept-parameter-source': 'template<class T>int f(decltype(static_cast<T>(1.0)) v)noexcept(sizeof(T)==sizeof(int)){return v;}int main(){return f<int>(3);}',
        'noexcept-redeclaration-source': 'template<class T>int f(T v)noexcept(true);template<class T>int f(T v)noexcept(1.0>0.0){return v;}int main(){return f(3);}',
        'noexcept-ordinary-source': 'int f(int v)noexcept(1.0>0.0){return v;}int main(){return f(3);}',
        'noexcept-local-method-source': 'template<class T>int f(T v)noexcept(sizeof(T)==sizeof(int)){struct R{int get()const noexcept(1.0>0.0){return 3;}};R r;return r.get()+v;}int main(){return f(3);}',
    }
    for name, source in frontend_repairs_reject.items():
        check("v2-frontend-repairs-reject-" + name, source, "TR0201", profile="cpp-core-v2")

    template_source_source = 'template<class T>T evaluate(T x){\n struct Local{\n  T value;\n  T get()const{return value;}\n  int next(){static int count=0;return ++count;}\n };\n Local l{x};return l.get()+l.next();\n}\nint signedValue(int n){return evaluate(n);}\nunsigned unsignedValue(unsigned n){return evaluate(n);}\ntemplate int evaluate<int>(int);\nextern template unsigned evaluate<unsigned>(unsigned);\ntemplate unsigned evaluate<unsigned>(unsigned);\nextern template unsigned evaluate<unsigned>(unsigned);\ntemplate<class T>T&alias(T&x){\n struct Reference{\n  T*p;\n  T&get(){return *p;}\n };\n Reference r{&x};return r.get();\n}\nint&reference(int&n){return alias(n);}\ntemplate<class T>T lifetime(T x){\n struct Life{\n  T value;\n  Life(T n):value(n){}\n  ~Life(){}\n  T read()const{return value;}\n };\n Life r(x);return r.read();\n}\nint lived(int n){return lifetime(n);}\ntemplate<class T>struct Outer{\n T run(T n){\n  struct Inner{\n   T value;\n   operator T()const{return value;}\n  };\n  Inner r{n};return r;\n }\n};\nint outer(Outer<int>&r,int n){return r.run(n);}\ntemplate<int N>int constant(){return N;}\nextern template int constant<3>();\ntemplate int constant<1+2>();\nextern template int constant<3>();\nint value(){return constant<3>();}\n'
    template_source = check("v2-template-source-protocol", template_source_source, profile="cpp-core-v2")
    ts_functions = {f["name"]: f for f in template_source["functions"]}
    ts_records = {r["id"]: r for r in template_source["records"]}
    ts_globals = {g["name"]: g for g in template_source["globals"]}
    assert len(ts_functions) == len(template_source["functions"])
    assert len(ts_records) == len(template_source["records"])
    assert len(ts_globals) == len(template_source["globals"])

    def ts_line(prefix):
        lines = [i for i, line in enumerate(template_source_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(lines) == 1, (prefix, lines)
        return lines[0]

    def ts_function(prefix):
        found = [f for f in ts_functions.values() if f["loc"]["line"] == ts_line(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0]

    def ts_selected(prefix):
        calls = gc_calls(ts_function(prefix))
        assert len(calls) == 1, (prefix, calls)
        return ts_functions[calls[0]["callee"]]

    def ts_signature(function, result, params):
        assert function["result"] == result and [p["type"] for p in function["params"]] == params, function

    signed = ts_selected("int signedValue(")
    unsigned = ts_selected("unsigned unsignedValue(")
    assert signed["name"] != unsigned["name"]
    assert len([f for f in ts_functions.values() if f["loc"]["line"] == ts_line("template<class T>T evaluate(")]) == 2
    instance_records = []
    instance_globals = []
    for function, scalar in ((signed, "int"), (unsigned, "uint")):
        ts_signature(function, scalar, [scalar])
        calls = gc_calls(function)
        assert len(calls) == 2
        getter, counter = [ts_functions[c["callee"]] for c in calls]
        rid = counter["params"][0]["type"].removeprefix("ptr:")
        record = ts_records[rid]
        instance_records.append(record)
        assert record["loc"]["line"] == ts_line(" struct Local{")
        assert [f["type"] for f in record["fields"]] == [scalar]
        ts_signature(getter, scalar, ["cptr:"+rid])
        ts_signature(counter, "int", ["ptr:"+rid])
        assert getter["loc"]["line"] == ts_line("  T get()const{return value;}")
        assert counter["loc"]["line"] == ts_line("  int next(")
        assert np_pointer(function, calls[0]["args"][0]) == np_pointer(function, calls[1]["args"][0])
        returned = next(n["value"] for n in getter["body"] if n["op"] == "return")
        assert gc_identity(getter, returned) == ("member", ("parameter", getter["params"][0]["name"]), record["fields"][0]["name"])
        names = {n["target"]["name"] for n in counter["body"] if n["op"] == "assign"
                 and n["target"].get("kind") == "var" and n["target"]["name"] in ts_globals}
        assert len(names) == 1
        item = ts_globals[next(iter(names))]
        assert item["mutable"] and item["type"] == "int" and item["value"]["value"] == "0"
        instance_globals.append(item["name"])
    assert len({r["id"] for r in instance_records}) == 2
    assert len({r["fields"][0]["name"] for r in instance_records}) == 2
    assert len(set(instance_globals)) == 2 and len(ts_globals) == 2
    alias = ts_selected("int&reference(")
    ts_signature(alias, "ptr:int", ["ptr:int"])
    alias_calls = gc_calls(alias)
    assert len(alias_calls) == 1
    returned = next(n["value"] for n in alias["body"] if n["op"] == "return")
    assert di_call_result(alias, returned) == alias_calls[0]["target"]["name"]
    alias_getter = ts_functions[alias_calls[0]["callee"]]
    arid = alias_getter["params"][0]["type"].removeprefix("ptr:")
    ts_signature(alias_getter, "ptr:int", ["ptr:"+arid])
    assert [f["type"] for f in ts_records[arid]["fields"]] == ["ptr:int"]
    life = ts_selected("int lived(")
    ts_signature(life, "int", ["int"])
    life_calls = gc_calls(life)
    assert len(life_calls) == 3
    constructor, getter, destructor = [ts_functions[c["callee"]] for c in life_calls]
    lrid = constructor["params"][0]["type"].removeprefix("ptr:")
    ts_signature(constructor, "void", ["ptr:"+lrid, "int"])
    ts_signature(getter, "int", ["cptr:"+lrid])
    ts_signature(destructor, "void", ["ptr:"+lrid])
    assert destructor["name"] == lrid+"_destroy"
    assert [np_pointer(life, c["args"][0]) for c in life_calls] == [np_pointer(life, life_calls[0]["args"][0])]*3
    assert gc_identity(life, life_calls[0]["args"][1]) == ("parameter", life["params"][0]["name"])
    outer = ts_selected("int outer(")
    outer_calls = gc_calls(outer)
    assert len(outer_calls) == 1
    conversion = ts_functions[outer_calls[0]["callee"]]
    irid = conversion["params"][0]["type"].removeprefix("cptr:")
    ts_signature(conversion, "int", ["cptr:"+irid])
    assert ts_records[irid]["loc"]["line"] == ts_line("  struct Inner{")
    constant = ts_selected("int value(")
    ts_signature(constant, "int", [])
    assert [gc_identity(constant, n["value"]) for n in constant["body"] if n["op"] == "return"] == [3]
    assert len([f for f in ts_functions.values() if f["loc"]["line"] == ts_line("template<int N>int constant(")]) == 1
    for function in ts_functions.values():
        assert not any(n["op"] == "mapped_call" for n in function["body"])
        for call_node in gc_calls(function):
            callee = ts_functions[call_node["callee"]]
            assert [a["type"] for a in call_node["args"]] == [p["type"] for p in callee["params"]]
    with tempfile.TemporaryDirectory(prefix="neverc-template-source-relocated-") as temp:
        relocated = check("template-source-relocated", template_source_source, root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == template_source, "instantiation source evidence changed canonical identities"

    template_source_positive = {
        'promoted-template-default': 'template<class T>struct R{T n=1;};',
        'selected-template-default': 'template<class T>struct R{T n=1;};int f(){R<int>r;return r.n;}',
        'lazy-template-default': 'template<class T>struct R{T n=T::missing;};static_assert(sizeof(R<int>)==sizeof(int));',
        'lazy-member-initializer': 'template<class T>struct R{T n;R():n(T::missing){}};static_assert(sizeof(R<int>)==sizeof(int));',
        'ordinary-member-initializer': 'template<class T>struct R{T n;R(T v):n(v){}};int f(){R<int>r(3);return r.n;}',
        'outside-member-initializer': 'template<class T>struct R{T n;R(T);};template<class T>R<T>::R(T v):n(v){}int f(){R<int>r(3);return r.n;}',
        'local-method': 'template<class T>T f(T n){struct R{T n;T get()const{return n;}};R r{n};return r.get();}int g(){return f(3);}',
        'local-static': 'template<class T>int f(){struct R{int get(){static int n=0;return ++n;}};R r;return r.get();}int g(){return f<int>()+f<unsigned>();}',
        'local-construct-destruct': 'int live=0;template<class T>T f(T n){struct R{T n;R(T v):n(v){++live;}~R(){--live;}};R r(n);return r.n;}int g(){return f(3)+live;}',
        'local-copy-move': 'template<class T>T f(T n){struct R{T n;R(T v):n(v){}R(const R&r):n(r.n+1){}R(R&&r):n(r.n+2){r.n=0;}};R a(n);R b(a);R c(static_cast<R&&>(b));return a.n+b.n+c.n;}int g(){return f(3);}',
        'local-operators': 'template<class T>T f(T n){struct R{T n;T&operator*(){return n;}R&operator++(){++n;return *this;}T operator()(){return n;}operator T()const{return n;}R&operator=(const R&r){n=r.n;return *this;}};R a{n},b{n};*a=4;++a;b=a;return b()+T(a);}int g(){return f(3);}',
        'local-defaulted': 'template<class T>T f(){struct R{T n=3;R()=default;R(const R&)=default;R(R&&)=default;R&operator=(const R&)=default;R&operator=(R&&)=default;~R()=default;};R a;R b(a);R c(static_cast<R&&>(b));a=c;b=static_cast<R&&>(c);return a.n+b.n;}int g(){return f<int>();}',
        'local-defaulted-nontrivial': 'struct I{int n;I():n(3){}I(const I&r):n(r.n+1){}I&operator=(const I&r){n=r.n;return *this;}~I(){}};template<class T>int f(){struct R{T n=2;I i;R()=default;R(const R&)=default;R&operator=(const R&)=default;~R()=default;};R a;R b(a);a=b;return a.i.n;}int g(){return f<int>();}',
        'local-nested': 'template<class T>T f(T n){struct R{struct S{T n;T get(){return n;}};T get(T n){S s{n};return s.get();}};R r;return r.get(n);}int g(){return f(3);}',
        'local-in-local-method': 'template<class T>T f(T n){struct R{T get(T n){struct S{T n;T get(){return n;}};S s{n};return s.get();}};R r;return r.get(n);}int g(){return f(3);}',
        'local-under-class-method': 'template<class T>struct R{T get(T n){struct S{T n;T get(){return n;}};S s{n};return s.get();}};int g(){R<int>r;return r.get(3);}',
        'local-default-noexcept': 'template<int N>int f(){struct R{int get(int n=N)const noexcept(sizeof(int)==4){return n;}};R r;return r.get();}int g(){return f<3>();}',
        'local-unused-defined': 'template<class T>int f(){struct R{T get(){return T(3);}T unused(){return T(4);}};R r;return r.get();}int g(){return f<int>();}',
        'local-overloads': 'template<class T>int f(){struct R{T get()&{return 3;}T get()const&{return 4;}T get()&&{return 5;}};R r;const R&c=r;return r.get()+c.get()+static_cast<R&&>(r).get();}int g(){return f<int>();}',
        'directive-scalar': 'template<int N>int f(){return N;}template int f<1+2>();',
        'directive-types': 'template<class T>T f(T n){return n;}template int f<int>(int);template unsigned f<unsigned>(unsigned);',
        'directive-deduced': 'template<class T>T f(T n){return n;}template int f(int);',
        'directive-extern-definition': 'template<int N>int f(){return N;}extern template int f<3>();template int f<1+2>();',
        'directive-after-use': 'template<int N>int f(){return N;}int g(){return f<3>();}template int f<1+2>();',
        'directive-after-specialization': 'template<int N>int f(){return N;}template<>int f<3>(){return 7;}template int f<1+2>();',
        'directive-repeat-extern': 'template<int N>int f(){return N;}extern template int f<3>();extern template int f<1+2>();template int f<3>();',
        'directive-member': 'template<class T>struct R{T f(T n){return n;}};template int R<int>::f(int);',
        'directive-conversion': 'template<class T>struct R{operator T(){return T(3);}};template R<int>::operator int();',
        'directive-aliased-qualifier': 'namespace N{template<class T>struct R{T f(){return T(3);}};}namespace A=N;template int A::R<int>::f();',
        'directive-written-noexcept': 'template<int N>int f()noexcept{return N;}template int f<3>()noexcept(sizeof(int)==4);',
        'protocol-source': 'template<class T>T evaluate(T x){\n struct Local{\n  T value;\n  T get()const{return value;}\n  int next(){static int count=0;return ++count;}\n };\n Local l{x};return l.get()+l.next();\n}\nint signedValue(int n){return evaluate(n);}\nunsigned unsignedValue(unsigned n){return evaluate(n);}\ntemplate int evaluate<int>(int);\nextern template unsigned evaluate<unsigned>(unsigned);\ntemplate unsigned evaluate<unsigned>(unsigned);\nextern template unsigned evaluate<unsigned>(unsigned);\ntemplate<class T>T&alias(T&x){\n struct Reference{\n  T*p;\n  T&get(){return *p;}\n };\n Reference r{&x};return r.get();\n}\nint&reference(int&n){return alias(n);}\ntemplate<class T>T lifetime(T x){\n struct Life{\n  T value;\n  Life(T n):value(n){}\n  ~Life(){}\n  T read()const{return value;}\n };\n Life r(x);return r.read();\n}\nint lived(int n){return lifetime(n);}\ntemplate<class T>struct Outer{\n T run(T n){\n  struct Inner{\n   T value;\n   operator T()const{return value;}\n  };\n  Inner r{n};return r;\n }\n};\nint outer(Outer<int>&r,int n){return r.run(n);}\ntemplate<int N>int constant(){return N;}\nextern template int constant<3>();\ntemplate int constant<1+2>();\nextern template int constant<3>();\nint value(){return constant<3>();}\n',
    }
    for name, source in template_source_positive.items():
        check("v2-template-source-positive-" + name, source, profile="cpp-core-v2")
    template_source_reject = {
        'dependent-delegating': 'template<class T>struct R{T n;R():R(3){}R(T v):n(v){}};',
        'outside-delegating': 'template<class T>struct R{T n;R();R(T v):n(v){}};template<class T>R<T>::R():R(3){}',
        'selected-delegating': 'template<class T>struct R{T n;R():R(3){}R(T v):n(v){}};int f(){R<int>r;return r.n;}',
        'local-selected-body': 'template<class T>int f(){struct R{int get(){return int(1.0);}};R r;return r.get();}int g(){return f<int>();}',
        'local-unused-body': 'template<class T>int f(){struct R{int get(){return 3;}int unused(){return int(1.0);}};R r;return r.get();}int g(){return f<int>();}',
        'local-noexcept-body': 'template<class T>int f(){struct R{int get()noexcept(sizeof(double)>0){return 3;}};R r;return r.get();}int g(){return f<int>();}',
        'local-field-type': 'template<class T>int f(){struct R{double n;int get(){return 3;}};R r{};return r.get();}int g(){return f<int>();}',
        'local-dynamic-static': 'template<class T>int f(int x){struct R{int get(int n){static int value=n;return value;}};R r;return r.get(x);}int g(){return f<int>(3);}',
        'local-virtual': 'template<class T>int f(){struct R{virtual int get(){return 3;}};R r;return r.get();}int g(){return f<int>();}',
        'directive-floating-cast': 'template<int N>int f(){return N;}template int f<static_cast<int>(1.0)>();',
        'directive-floating-size': 'template<int N>int f(){return N;}template int f<sizeof(double)>();',
        'directive-floating-fold': 'constexpr int n(){return int(1.0);}template<int N>int f(){return N;}template int f<n()>();',
        'directive-floating-conditional': 'template<int N>int f(){return N;}template int f<(true?3:int(1.0))>();',
        'directive-extern-floating': 'template<int N>int f(){return N;}extern template int f<int(1.0)>();',
        'directive-after-use-floating': 'template<int N>int f(){return N;}int g(){return f<1>();}template int f<int(1.0)>();',
        'directive-after-specialization-floating': 'template<int N>int f(){return N;}template<>int f<1>(){return 3;}template int f<int(1.0)>();',
        'directive-first-bad-spelling': 'template<int N>int f(){return N;}extern template int f<int(1.0)>();extern template int f<1>();template int f<1>();',
        'directive-last-bad-spelling': 'template<int N>int f(){return N;}extern template int f<1>();extern template int f<int(1.0)>();template int f<1>();',
        'directive-return-type-source': 'template<int N>int f(){return N;}template decltype(int(1.0)) f<3>();',
        'directive-parameter-type-source': 'template<class T>int f(T){return 3;}template int f<int>(decltype(int(1.0)));',
        'directive-noexcept-source': 'template<int N>int f()noexcept{return N;}template int f<3>()noexcept(sizeof(double)>0);',
        'directive-member-qualifier-source': 'template<int N>struct R{int f(){return N;}};template int R<int(1.0)>::f();',
        'directive-conversion-name-source': 'template<class T>struct R{operator T(){return T(3);}};template R<int>::operator decltype(int(1.0))();',
        'directive-attribute-after-specialization': 'template<int N>int f(){return N;}template<>int f<3>(){return 7;}template __attribute__((used)) int f<3>();',
        'directive-attribute-after-use': 'template<int N>int f(){return N;}int g(){return f<3>();}extern template __attribute__((used)) int f<3>();',
        'directive-declarator-attribute': 'template<int N>int f(){return N;}template int f<3>() __attribute__((used));',
        'directive-type-chunk-attribute': 'template<int N>int*f(int*p){return p;}extern template int* __attribute__((aligned(8))) f<3>(int*);',
    }
    for name, source in template_source_reject.items():
        check("v2-template-source-reject-" + name, source, 'TR0201', profile="cpp-core-v2")
    template_source_invalid = {
        'local-private': 'template<class T>int f(){class R{int get(){return 3;}};R r;return r.get();}int g(){return f<int>();}',
        'local-undefined-dependent': 'template<class T>int f(){struct R{int get(){return T::missing;}};R r;return r.get();}int g(){return f<int>();}',
        'directive-type-mismatch': 'template<class T>T f(T n){return n;}template bool f<int>(int);',
        'directive-duplicate-definition': 'template<int N>int f(){return N;}template int f<3>();template int f<1+2>();',
    }
    for name, source in template_source_invalid.items():
        check("v2-template-source-invalid-" + name, source, 'TR0202', profile="cpp-core-v2")
    check("v1-template-source-directive", 'template<int N>int f(){return N;}template int f<1+2>();', "TR0201")

    class_operators_source = 'int next(){return 2;}\ntemplate<class T,int N>struct Cursor{\n T*p;\n T&operator*()const{return *p;}\n T&operator[](int n){return p[n];}\n Cursor&operator++(){++p;return *this;}\n Cursor operator++(int){T*old=p;++p;return Cursor{old};}\n Cursor operator+(int n)const{return Cursor{p+n};}\n Cursor&operator=(int n){*p=n;return *this;}\n T operator()(int n=next()){return p[n]+N;}\n bool operator&&(const Cursor&r)const{return *p&&*r.p;}\n bool operator||(const Cursor&r)const{return *p||*r.p;}\n Cursor&operator,(Cursor&r){return r;}\n explicit operator bool()const{return *p!=0;}\n operator T&(){return *p;}\n int counter(){static int n=0;return ++n;}\n};\nCursor<int,3>&left(Cursor<int,3>&r){return r;}\nint right(){return 7;}\nint&dereference(const Cursor<int,3>&r){return *r;}\nint&subscript(Cursor<int,3>&r){return left(r)[next()];}\nCursor<int,3>&prefix(Cursor<int,3>&r){return ++r;}\nCursor<int,3>postfix(Cursor<int,3>&r){return r++;}\nCursor<int,3>result(const Cursor<int,3>&r){return r+1;}\nCursor<int,3>&assign(Cursor<int,3>&r){return left(r)=right();}\nCursor<int,3>&explicitAssign(Cursor<int,3>&r){return left(r).operator=(right());}\nint callDefault(Cursor<int,3>&r){return left(r)();}\nint callExplicit(Cursor<int,3>&r){return left(r)(right());}\nbool bothAnd(Cursor<int,3>&a,Cursor<int,3>&b){return left(a)&&left(b);}\nbool bothOr(Cursor<int,3>&a,Cursor<int,3>&b){return left(a)||left(b);}\nCursor<int,3>&comma(Cursor<int,3>&a,Cursor<int,3>&b){return (left(a),left(b));}\nbool boolean(const Cursor<int,3>&r){return static_cast<bool>(r);}\nint&reference(Cursor<int,3>&r){return r;}\nint counterA(Cursor<int,3>&r){return r.counter();}\nusing Alias=Cursor<int,1+2>;\nint counterAlias(Alias&r){return r.counter();}\nint counterB(Cursor<int,4>&r){return r.counter();}\nint callDifferent(Cursor<int,4>&r){return r(1);}\ntemplate<class T>struct Qualified{\n T n;\n T operator()()&{return n;}\n T operator()()const&{return n+1;}\n T operator()()&&{return n+2;}\n};\nint mutableCall(Qualified<int>&r){return r();}\nint constCall(const Qualified<int>&r){return r();}\nint rvalueCall(Qualified<int>&r){return static_cast<Qualified<int>&&>(r)();}\nstruct Value{\n int n;\n ~Value(){}\n};\ntemplate<class T>struct Convert{\n T n;\n operator Value()const{return Value{n};}\n};\nValue converted(const Convert<int>&r){return r;}\nint extended(const Convert<int>&r){const Value&v=r;return v.n;}\ntemplate<auto N>struct Count{\n int operator()(){struct Local{int n;};static int count=0;Local l{++count};return l.n+N;}\n};\nint signedCount(){Count<3>c;return c();}\nint unsignedCount(){Count<3u>c;return c();}\nint aliasCount(){Count<1+2>c;return c();}\n'
    class_operators = check("v2-class-operators-protocol", class_operators_source, profile="cpp-core-v2")
    co_functions = {f["name"]: f for f in class_operators["functions"]}
    co_records = {r["id"]: r for r in class_operators["records"]}
    co_globals = {g["name"]: g for g in class_operators["globals"]}
    assert len(co_functions) == len(class_operators["functions"])
    assert len(co_records) == len(class_operators["records"])
    assert len(co_globals) == len(class_operators["globals"])

    def co_line(prefix):
        lines = [i for i, line in enumerate(class_operators_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(lines) == 1, (prefix, lines)
        return lines[0]

    def co_function(prefix):
        found = [f for f in co_functions.values() if f["loc"]["line"] == co_line(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0]

    def co_selected(prefix):
        calls = gc_calls(co_function(prefix))
        assert len(calls) == 1, (prefix, calls)
        return co_functions[calls[0]["callee"]]

    def co_signature(function, result, params):
        assert function["result"] == result and [p["type"] for p in function["params"]] == params, function

    rid = co_function("int&dereference(")["params"][0]["type"].removeprefix("cptr:")
    assert [f["type"] for f in co_records[rid]["fields"]] == ["ptr:int"]
    dereference = co_selected("int&dereference(")
    prefix = co_selected("Cursor<int,3>&prefix(")
    postfix = co_selected("Cursor<int,3>postfix(")
    result = co_selected("Cursor<int,3>result(")
    boolean = co_selected("bool boolean(")
    reference = co_selected("int&reference(")
    for function, output, params in (
        (dereference, "ptr:int", ["cptr:"+rid]),
        (prefix, "ptr:"+rid, ["ptr:"+rid]),
        (postfix, "void", ["ptr:"+rid, "ptr:"+rid, "int"]),
        (result, "void", ["ptr:"+rid, "cptr:"+rid, "int"]),
        (boolean, "bool", ["cptr:"+rid]),
        (reference, "ptr:int", ["ptr:"+rid]),
    ):
        co_signature(function, output, params)
    assert len({f["name"] for f in (dereference, prefix, postfix, result, boolean, reference)}) == 6
    for name in ("int&dereference(", "Cursor<int,3>&prefix(", "int&reference("):
        caller = co_function(name)
        call = gc_calls(caller)[0]
        assert np_pointer(caller, call["args"][0]) == ("parameter", caller["params"][0]["name"])
        returned = next(n["value"] for n in caller["body"] if n["op"] == "return")
        assert di_call_result(caller, returned) == call["target"]["name"]
    returned = next(n["value"] for n in prefix["body"] if n["op"] == "return")
    assert np_pointer(prefix, returned) == ("parameter", prefix["params"][0]["name"])
    for name, literal in (("Cursor<int,3>postfix(", 0), ("Cursor<int,3>result(", 1)):
        caller = co_function(name)
        call = gc_calls(caller)[0]
        assert "target" not in call
        assert [np_pointer(caller, arg) for arg in call["args"][:2]] == [
            ("parameter", p["name"]) for p in caller["params"]]
        assert gc_identity(caller, call["args"][2]) == literal
        assert not any(n["op"] == "assign" and n["target"]["type"] == rid for n in caller["body"])
    left = co_function("Cursor<int,3>&left(")
    right = co_function("int right(")
    next_value = co_function("int next(")
    for name, effects in (("Cursor<int,3>&assign(", [right, left]),
                          ("Cursor<int,3>&explicitAssign(", [left, right]),
                          ("int&subscript(", [left, next_value]),
                          ("int callDefault(", [left, next_value]),
                          ("int callExplicit(", [left, right])):
        caller = co_function(name)
        calls = gc_calls(caller)
        assert len(calls) == 3 and [c["callee"] for c in calls[:2]] == [f["name"] for f in effects]
        receiver = next(c for c in calls[:2] if c["callee"] == left["name"])
        argument = next(c for c in calls[:2] if c["callee"] != left["name"])
        assert di_call_result(caller, calls[2]["args"][0]) == receiver["target"]["name"]
        assert di_call_result(caller, calls[2]["args"][1]) == argument["target"]["name"]
    assignment = gc_calls(co_function("Cursor<int,3>&assign("))[-1]
    assert assignment["callee"] == gc_calls(co_function("Cursor<int,3>&explicitAssign("))[-1]["callee"]
    co_signature(co_functions[assignment["callee"]], "ptr:"+rid, ["ptr:"+rid, "int"])
    call = gc_calls(co_function("int callDefault("))[-1]
    assert call["callee"] == gc_calls(co_function("int callExplicit("))[-1]["callee"]
    co_signature(co_functions[call["callee"]], "int", ["ptr:"+rid, "int"])
    for name, output in (("bool bothAnd(", "bool"), ("bool bothOr(", "bool"), ("Cursor<int,3>&comma(", "ptr:"+rid)):
        caller = co_function(name)
        calls = gc_calls(caller)
        assert len(calls) == 3 and [c["callee"] for c in calls[:2]] == [left["name"]]*2
        assert [np_pointer(caller, c["args"][0]) for c in calls[:2]] == [
            ("parameter", p["name"]) for p in caller["params"]]
        assert [di_call_result(caller, a) for a in calls[2]["args"]] == [c["target"]["name"] for c in calls[:2]]
        qualifier = "ptr:" if output.startswith("ptr:") else "cptr:"
        co_signature(co_functions[calls[2]["callee"]], output, [qualifier+rid]*2)
        assert not any(n["op"] == "branch" for n in caller["body"]), "overloaded logical/comma call was short-circuited"
    qualified = [co_selected(p) for p in ("int mutableCall(", "int constCall(", "int rvalueCall(")]
    qid = co_function("int mutableCall(")["params"][0]["type"].removeprefix("ptr:")
    assert len({f["name"] for f in qualified}) == 3
    for function, qualifier in zip(qualified, ("ptr:", "cptr:", "ptr:")):
        co_signature(function, "int", [qualifier+qid])
    counter_a = co_selected("int counterA(")
    counter_b = co_selected("int counterB(")
    assert counter_a == co_selected("int counterAlias(") and counter_a["name"] != counter_b["name"]
    other_rid = counter_b["params"][0]["type"].removeprefix("ptr:")
    assert other_rid != rid and co_records[rid]["fields"][0]["name"] != co_records[other_rid]["fields"][0]["name"]
    different_call = co_selected("int callDifferent(")
    assert different_call["name"] != call["callee"]
    co_signature(different_call, "int", ["ptr:"+other_rid, "int"])
    signed_count = co_selected("int signedCount(")
    unsigned_count = co_selected("int unsignedCount(")
    assert signed_count == co_selected("int aliasCount(") and signed_count["name"] != unsigned_count["name"]
    count_record_ids = {f["params"][0]["type"] for f in (signed_count, unsigned_count)}
    assert len(count_record_ids) == 2 and all(len(f["params"]) == 1 for f in (signed_count, unsigned_count))
    local_records = [r for r in co_records.values() if r["loc"]["line"] == co_line(" int operator()(){struct Local")]
    assert len(local_records) == 2 and len({r["fields"][0]["name"] for r in local_records}) == 2
    globals_used = []
    for function in (counter_a, counter_b, signed_count, unsigned_count):
        names = {n["target"]["name"] for n in function["body"] if n["op"] == "assign"
                 and n["target"].get("kind") == "var" and n["target"]["name"] in co_globals}
        assert len(names) == 1
        item = co_globals[next(iter(names))]
        assert item["mutable"] and item["type"] == "int" and item["value"]["value"] == "0"
        globals_used.append(item["name"])
    assert len(set(globals_used)) == 4 and len(co_globals) == 4
    converted = co_function("Value converted(")
    conversion = co_selected("Value converted(")
    vid = converted["params"][0]["type"].removeprefix("ptr:")
    co_signature(conversion, "void", [p["type"] for p in converted["params"]])
    converted_call = gc_calls(converted)[0]
    assert [np_pointer(converted, a) for a in converted_call["args"]] == [
        ("parameter", p["name"]) for p in converted["params"]]
    assert not any(n["op"] == "assign" and n["target"]["type"] == vid for n in converted["body"])
    extended = co_function("int extended(")
    extended_calls = gc_calls(extended)
    assert [c["callee"] for c in extended_calls] == [conversion["name"], vid+"_destroy"]
    assert np_pointer(extended, extended_calls[0]["args"][0]) == np_pointer(extended, extended_calls[1]["args"][0])
    for function in co_functions.values():
        assert not any(n["op"] == "mapped_call" for n in function["body"])
        for call_node in gc_calls(function):
            callee = co_functions[call_node["callee"]]
            assert [a["type"] for a in call_node["args"]] == [p["type"] for p in callee["params"]]
    with tempfile.TemporaryDirectory(prefix="neverc-class-operators-relocated-") as temp:
        relocated = check("class-operators-relocated", class_operators_source, root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == class_operators, "class operator identities depend on the absolute root"

    class_operators_positive = {
        'promoted-0': 'template<class T>struct R{T n;R&operator=(const R&r){n=r.n;return *this;}};',
        'promoted-1': 'template<class T>struct R{R()=default;int operator()(){return 3;}};',
        'promoted-2': 'template<class T>struct R{T n;R(T v):n(v){}T operator()(){return n;}};',
        'promoted-3': 'template<class T>struct R{T n;R(T v):n(v){}operator T(){return n;}};',
        'promoted-4': 'template<class T>struct R{T n;T operator()(){return n;}};',
        'promoted-5': 'template<class T>struct R{T n;operator int(){return 1;}};',
        'iterator': 'template<class T>struct R{T*p;T&operator*()const{return *p;}R&operator++(){++p;return *this;}R operator++(int){R old{p};++p;return old;}T&operator[](int n)const{return p[n];}T*operator->()const{return p;}};struct I{int n;};int f(){int a[3]={1,2,3};R<int>r{a};(*r)++;r[1]=7;int n=*r++;++r;I i{4};R<I>p{&i};return n+*r+p->n;}',
        'call-default': 'int count=0;int next(){return ++count;}template<class T>struct R{T n;T operator()(T x=next()){return n+x;}};int f(){R<int>r{3};return r()+r(7);}',
        'subscript-ref': 'template<class T>struct R{T*p;T&operator[](int n){return p[n];}};int&f(R<int>&r,int n){return r[n];}',
        'arrow-chain': 'struct I{int n;};template<class T>struct R{T*p;T*operator->(){return p;}};template<class T>struct S{T n;T&operator->(){return n;}};int f(){I i{3};S<R<I>>s{{&i}};return s->n;}',
        'arrow-star': 'template<class T>struct R{T n;T&operator->*(int){return n;}};int&f(R<int>&r){return r->*0;}',
        'logical-comma': 'template<class T>struct R{T n;bool operator&&(const R&r){return n&&r.n;}bool operator||(const R&r){return n||r.n;}R&operator,(R&r){return r;}};bool f(R<int>&a,R<int>&b){(a,b).n=2;return (a&&b)||(a||b);}',
        'copy-move-assignment': 'template<class T>struct R{T n;R&operator=(const R&r){n=r.n;return *this;}R&operator=(R&&r){n=r.n;r.n=0;return *this;}};int f(){R<int>a{1},b{2};a=b;b=static_cast<R<int>&&>(a);return b.n;}',
        'general-assignment': 'template<class T>struct R{T n;T operator=(T x)&&{n=x;return n;}};int f(){return R<int>{1}=7;}',
        'record-result': 'template<class T>struct R{T n;R operator+(const R&r)const{return R{n+r.n};}};R<int>f(const R<int>&a,const R<int>&b){return a+b;}',
        'qualified-overloads': 'template<class T>struct R{T n;T&operator()()&{return n;}const T&operator()()const&{return n;}T operator()()&&{return n+1;}};int f(R<int>&r,const R<int>&c){r()=3;return c()+static_cast<R<int>&&>(r)();}',
        'explicit-bool': 'template<class T>struct R{T n;explicit operator bool()const noexcept{return n!=0;}};bool f(R<int>&r){if(r)return !r;return static_cast<bool>(r);}',
        'implicit-scalar': 'template<class T>struct R{T n;operator T()const{return n;}};int f(){R<int>r{3};int n=r;return n+r.operator int();}',
        'reference-conversion': 'template<class T>struct R{T n;operator T&(){return n;}operator const T&()const{return n;}};int f(R<int>&r,const R<int>&s){int&n=r;const int&m=s;n=5;return m;}',
        'pointer-conversion': 'template<class T>struct R{T*p;operator T*()const{return p;}};int f(){int n=3;R<int>r{&n};int*p=r;*p=7;return n;}',
        'record-conversion': 'struct I{int n;};template<class T>struct R{T n;operator I()const{return I{n};}};I f(const R<int>&r){return r;}',
        'rvalue-conversion': 'struct I{int n;I(I&&r):n(r.n){r.n=0;}};template<class T>struct R{T n;operator T&&()&&{return static_cast<T&&>(n);}};I f(R<I>&r){return static_cast<R<I>&&>(r);}',
        'deduced-conversion': 'template<class T>struct R{T n;operator auto()const{return n;}};int f(R<int>&r){return r;}',
        'decltype-auto-conversion': 'template<class T>struct R{T n;operator decltype(auto)(){return (n);}};int&f(R<int>&r){return r;}',
        'value-arguments': 'template<class T,T N>struct R{T n;T operator()(){return n+N;}};template<auto N>struct S{int operator()(){static int count=0;return ++count+N;}};int f(){R<int,3>r{2};S<3>a;S<3u>b;S<1+2>c;return r()+a()+b()+c();}',
        'local-types': 'template<class T>struct R{int operator()(){struct Local{T n;};Local l{3};static int count=0;return l.n+ ++count;}};int f(){R<int>a;R<unsigned>b;return a()+b();}',
        'outside-operator': 'template<class T>struct R{T n;T operator()(T x);};template<class T>T R<T>::operator()(T x){return n+x;}int f(){R<int>r{3};return r(4);}',
        'outside-conversion': 'template<class T>struct R{T n;operator T()const;};template<class T>R<T>::operator T()const{return n;}int f(R<int>&r){return r;}',
        'member-specialization': 'template<class T>struct R{T n;T operator()(){return n;}operator T(){return n;}};template<>int R<int>::operator()(){return n+1;}template<>R<int>::operator int(){return n+2;}int f(R<int>&r){return r()+int(r);}',
        'class-specialization': 'template<class T>struct R{T n;T operator()(){return n;}};template<>struct R<int>{int n;int operator()(){return n+1;}operator int(){return n+2;}};int f(R<int>&r){return r()+int(r);}',
        'class-instantiation': 'template<class T>struct R{T n;T operator()(){return n;}operator T(){return n;}};template struct R<int>;',
        'operator-instantiation': 'template<class T>struct R{T n;T operator()(){return n;}};template int R<int>::operator()();',
        'conversion-instantiation': 'template<class T>struct R{T n;operator T(){return n;}};template R<int>::operator int();',
        'late-definition': 'template<class T>struct R{T n;T operator()();};template struct R<int>;template<class T>T R<T>::operator()(){return n;}int f(R<int>&r){return r();}',
        'lazy-bodies': 'template<class T>struct R{int n;int operator()(){return T::missing;}operator auto(){return T::missing;}};static_assert(sizeof(R<int>)==sizeof(int));',
        'lazy-unsupported': 'template<class T>struct R{int n;int operator()(){return int(1.0);}operator auto(){return 1.0;}};static_assert(sizeof(R<int>)==sizeof(int));',
        'lazy-default': 'template<class T>struct R{T n;T operator()(T x=T::missing){return n+x;}};int f(){R<int>r{3};return r(4);}',
        'folded-source': 'template<class T>struct R{T n;constexpr T operator()()const{return n;}constexpr operator T()const{return n;}};static_assert(R<int>{3}()==3);static_assert(int(R<int>{4})==4);',
        'noexcept-source': 'template<class T>struct R{T n;T operator()()noexcept(sizeof(this->n)>0){return n;}operator T()const noexcept(sizeof(T)>0){return n;}};bool f(R<int>&r){return noexcept(r())&&noexcept(r.operator int());}',
        'conversion-default': 'template<class T>struct R{T n;constexpr operator T()const{return n;}};template<class T>struct S{T n=R<T>{3};T operator()(T x=R<T>{4}){return n+x;}};int f(){S<int>s;return s();}',
        'arithmetic': 'template<class T>struct R{T n;T operator+(T x)const{return n+x;}T operator-(T x)const{return n-x;}T operator*(T x)const{return n*x;}T operator/(T x)const{return n/x;}T operator%(T x)const{return n%x;}T operator^(T x)const{return n^x;}T operator&(T x)const{return n&x;}T operator|(T x)const{return n|x;}T operator<<(T x)const{return n<<x;}T operator>>(T x)const{return n>>x;}};int f(){R<int>r{6};return (r+2)+(r-2)+(r*2)+(r/2)+(r%2)+(r^2)+(r&2)+(r|2)+(r<<2)+(r>>2);}',
        'comparison': 'template<class T>struct R{T n;bool operator==(T x)const{return n==x;}bool operator!=(T x)const{return n!=x;}bool operator<(T x)const{return n<x;}bool operator>(T x)const{return n>x;}bool operator<=(T x)const{return n<=x;}bool operator>=(T x)const{return n>=x;}};int f(){R<int>r{6};return (r==2)+(r!=2)+(r<2)+(r>2)+(r<=2)+(r>=2);}',
        'compound': 'template<class T>struct R{T n;R&operator+=(T x){n+=x;return *this;}R&operator-=(T x){n-=x;return *this;}R&operator*=(T x){n*=x;return *this;}R&operator/=(T x){n/=x;return *this;}R&operator%=(T x){n%=x;return *this;}R&operator^=(T x){n^=x;return *this;}R&operator&=(T x){n&=x;return *this;}R&operator|=(T x){n|=x;return *this;}R&operator<<=(T x){n<<=x;return *this;}R&operator>>=(T x){n>>=x;return *this;}};int f(){R<int>r{6};r+=2;r-=1;r*=2;r/=2;r%=5;r^=3;r|=2;r&=3;r<<=1;r>>=1;return r.n;}',
        'unary': 'template<class T>struct R{T n;T operator+()const{return +n;}T operator-()const{return -n;}T operator~()const{return ~n;}bool operator!()const{return !n;}R&operator--(){--n;return *this;}R operator--(int){R old{n};--n;return old;}};int f(){R<int>r{3};--r;r--;return +r+ -r+ ~r+ !r;}',
        'template-iterator-range': 'template<class T>struct Iterator{T*p;T&operator*()const{return *p;}Iterator&operator++(){++p;return *this;}bool operator!=(const Iterator&r)const{return p!=r.p;}};template<class T,int N>struct Range{T data[N];Iterator<T>begin(){return Iterator<T>{data};}Iterator<T>end(){return Iterator<T>{data+N};}};int f(){Range<int,3>r{{1,2,3}};int sum=0;for(int&n:r){++n;sum+=n;}return sum;}',
        'protocol-source': 'int next(){return 2;}\ntemplate<class T,int N>struct Cursor{\n T*p;\n T&operator*()const{return *p;}\n T&operator[](int n){return p[n];}\n Cursor&operator++(){++p;return *this;}\n Cursor operator++(int){T*old=p;++p;return Cursor{old};}\n Cursor operator+(int n)const{return Cursor{p+n};}\n Cursor&operator=(int n){*p=n;return *this;}\n T operator()(int n=next()){return p[n]+N;}\n bool operator&&(const Cursor&r)const{return *p&&*r.p;}\n bool operator||(const Cursor&r)const{return *p||*r.p;}\n Cursor&operator,(Cursor&r){return r;}\n explicit operator bool()const{return *p!=0;}\n operator T&(){return *p;}\n int counter(){static int n=0;return ++n;}\n};\nCursor<int,3>&left(Cursor<int,3>&r){return r;}\nint right(){return 7;}\nint&dereference(const Cursor<int,3>&r){return *r;}\nint&subscript(Cursor<int,3>&r){return left(r)[next()];}\nCursor<int,3>&prefix(Cursor<int,3>&r){return ++r;}\nCursor<int,3>postfix(Cursor<int,3>&r){return r++;}\nCursor<int,3>result(const Cursor<int,3>&r){return r+1;}\nCursor<int,3>&assign(Cursor<int,3>&r){return left(r)=right();}\nCursor<int,3>&explicitAssign(Cursor<int,3>&r){return left(r).operator=(right());}\nint callDefault(Cursor<int,3>&r){return left(r)();}\nint callExplicit(Cursor<int,3>&r){return left(r)(right());}\nbool bothAnd(Cursor<int,3>&a,Cursor<int,3>&b){return left(a)&&left(b);}\nbool bothOr(Cursor<int,3>&a,Cursor<int,3>&b){return left(a)||left(b);}\nCursor<int,3>&comma(Cursor<int,3>&a,Cursor<int,3>&b){return (left(a),left(b));}\nbool boolean(const Cursor<int,3>&r){return static_cast<bool>(r);}\nint&reference(Cursor<int,3>&r){return r;}\nint counterA(Cursor<int,3>&r){return r.counter();}\nusing Alias=Cursor<int,1+2>;\nint counterAlias(Alias&r){return r.counter();}\nint counterB(Cursor<int,4>&r){return r.counter();}\nint callDifferent(Cursor<int,4>&r){return r(1);}\ntemplate<class T>struct Qualified{\n T n;\n T operator()()&{return n;}\n T operator()()const&{return n+1;}\n T operator()()&&{return n+2;}\n};\nint mutableCall(Qualified<int>&r){return r();}\nint constCall(const Qualified<int>&r){return r();}\nint rvalueCall(Qualified<int>&r){return static_cast<Qualified<int>&&>(r)();}\nstruct Value{\n int n;\n ~Value(){}\n};\ntemplate<class T>struct Convert{\n T n;\n operator Value()const{return Value{n};}\n};\nValue converted(const Convert<int>&r){return r;}\nint extended(const Convert<int>&r){const Value&v=r;return v.n;}\ntemplate<auto N>struct Count{\n int operator()(){struct Local{int n;};static int count=0;Local l{++count};return l.n+N;}\n};\nint signedCount(){Count<3>c;return c();}\nint unsignedCount(){Count<3u>c;return c();}\nint aliasCount(){Count<1+2>c;return c();}\n',
    }
    for name, source in class_operators_positive.items():
        check("v2-class-operators-positive-" + name, source, profile="cpp-core-v2")
    class_operators_reject = {
        'selected-operator-body': 'template<class T>struct R{int operator()(){return int(1.0);}};int f(){R<int>r;return r();}',
        'selected-conversion-body': 'template<class T>struct R{operator int(){return int(1.0);}};int f(){R<int>r;return r;}',
        'forced-operator-body': 'template<class T>struct R{int operator()(){return int(1.0);}};template int R<int>::operator()();',
        'forced-conversion-body': 'template<class T>struct R{operator int(){return int(1.0);}};template R<int>::operator int();',
        'forced-class-body': 'template<class T>struct R{int operator()(){return int(1.0);}};template struct R<int>;',
        'selected-default': 'template<class T>struct R{int operator()(int x=int(1.0)){return x;}};int f(){R<int>r;return r();}',
        'selected-dmi': 'template<class T>struct R{T n=T(1.0);operator T(){return n;}};int f(){R<int>r;return r;}',
        'folded-operator': 'template<class T>struct R{constexpr int operator()()const{return int(1.0);}};static_assert(R<int>{}()==1);',
        'folded-conversion': 'template<class T>struct R{constexpr operator int()const{return int(1.0);}};static_assert(int(R<int>{})==1);',
        'operator-noexcept': 'template<class T>struct R{int operator()()noexcept(sizeof(double)>0){return 1;}};bool f(R<int>&r){return noexcept(r());}',
        'conversion-noexcept': 'template<class T>struct R{operator int()noexcept(sizeof(double)>0){return 1;}};bool f(R<int>&r){return noexcept(r.operator int());}',
        'selected-floating-type': 'template<class T>struct R{operator T(){return T();}};double f(R<double>&r){return r;}',
        'outside-header': 'template<decltype(sizeof(double)) N>struct R{int operator()();};template<decltype(sizeof(double)) N>int R<N>::operator()(){return N;}int f(){R<3>r;return r();}',
        'own-operator-template': 'template<class T>struct R{template<class U>int operator()(U){return 1;}};',
        'own-conversion-template': 'template<class T>struct R{template<class U>operator U(){return U();}};',
        'free-operator-template': 'struct R{int n;};template<class T>int operator+(const R&r,T v){return r.n+v;}',
        'virtual-conversion': 'template<class T>struct R{virtual operator int(){return 1;}};',
        'volatile-operator': 'template<class T>struct R{int operator()()volatile{return 1;}};',
        'allocation': 'template<class T>struct R{static void*operator new(decltype(sizeof(0))){return nullptr;}};',
        'conditional-explicit-conversion': 'template<class T>struct R{explicit(true) operator bool(){return true;}};',
        'conditional-explicit-constructor': 'template<class T>struct R{explicit(sizeof(double)>0) R(){}};',
    }
    for name, source in class_operators_reject.items():
        check("v2-class-operators-reject-" + name, source, 'TR0201', profile="cpp-core-v2")
    class_operators_invalid = {
        'dependent-operator-use': 'template<class T>struct R{int operator()(){return T::missing;}};int f(){R<int>r;return r();}',
        'dependent-conversion-use': 'template<class T>struct R{operator int(){return T::missing;}};int f(){R<int>r;return r;}',
        'explicit-implicit-use': 'template<class T>struct R{explicit operator int(){return 1;}};int f(){R<int>r;return r;}',
        'private-conversion': 'template<class T>class R{operator int(){return 1;}};int f(R<int>&r){return r;}',
        'wrong-receiver': 'template<class T>struct R{int operator()()&&{return 1;}};int f(R<int>&r){return r();}',
        'ambiguous-conversion': 'template<class T>struct R{operator int(){return 1;}operator unsigned(){return 2;}};bool f(R<int>&r){return r;}',
        'defaulted-ordinary-operator': 'template<class T>struct R{int operator()()=default;};',
        'wrong-arity': 'template<class T>struct R{int operator+(int,int){return 1;}};',
    }
    for name, source in class_operators_invalid.items():
        check("v2-class-operators-invalid-" + name, source, 'TR0202', profile="cpp-core-v2")
    class_operators_missing = {
        'selected-operator': 'template<class T>struct R{int operator()();};int f(R<int>&r){return r();}',
        'selected-conversion': 'template<class T>struct R{operator int();};int f(R<int>&r){return r;}',
        'query-operator': 'template<class T>struct R{int operator()()noexcept;};bool f(R<int>&r){return noexcept(r());}',
        'query-conversion': 'template<class T>struct R{operator int()noexcept;};bool f(R<int>&r){return noexcept(r.operator int());}',
        'specialized-operator': 'template<class T>struct R{int operator()(){return 1;}};template<>int R<int>::operator()();',
        'specialized-conversion': 'template<class T>struct R{operator int(){return 1;}};template<>R<int>::operator int();',
        'selected-copy-assignment': 'template<class T>struct R{T n;R&operator=(const R&);};void f(R<int>&a,const R<int>&b){a=b;}',
    }
    for name, source in class_operators_missing.items():
        check("v2-class-operators-missing-" + name, source, 'TR0203', profile="cpp-core-v2")
    check("v1-class-operator", 'template<class T>struct R{T n;T operator()(){return n;}};', "TR0201")
    check("v1-class-conversion", 'template<class T>struct R{T n;operator int(){return 1;}};', "TR0201")

    class_defaulted_source = 'int trace=0;\nvoid mark(int n){trace=n;}\nstruct Leaf{\n int n;Leaf*self;\n Leaf():n(1),self(this){mark(1);}\n Leaf(const Leaf&s):n(s.n+10),self(this){mark(2);}\n Leaf(Leaf&&s):n(s.n+20),self(this){s.n=-1;mark(3);}\n Leaf&operator=(const Leaf&s){n=s.n+30;mark(4);return *this;}\n Leaf&operator=(Leaf&&s){n=s.n+40;s.n=-1;mark(5);return *this;}\n ~Leaf(){mark(6);}\n};\ntemplate<class T,int N>struct Box{\n T plain;T value=N;Leaf first;Leaf items[2];\n Box()=default;Box(const Box&)=default;Box(Box&&)=default;\n Box&operator=(const Box&)& =default;Box&operator=(Box&&)& =default;~Box()=default;\n};\ntemplate<class T>struct Outside{T plain;Leaf leaf;Outside();~Outside();};\ntemplate<class T>Outside<T>::Outside()=default;\ntemplate<class T>Outside<T>::~Outside()=default;\ntemplate<class T>struct Trivial{T n;T*p;Trivial(const Trivial&)=default;Trivial&operator=(const Trivial&)=default;};\ntemplate<class T>struct Forced{T n=3;Leaf leaf;Forced();};\ntemplate<class T>Forced<T>::Forced()=default;\ntemplate struct Forced<int>;\nusing Alias=Box<int,3>;\nvoid defaultInit(){Box<int,3>r;Outside<int>o;}\nvoid valueInit(){Box<int,3>r=Box<int,3>();Outside<int>o=Outside<int>();}\nBox<int,3>copy(const Box<int,3>&r){return r;}\nBox<int,3>move(Box<int,3>&r){return static_cast<Box<int,3>&&>(r);}\nBox<int,3>&copyAssign(Box<int,3>&a,const Box<int,3>&b){return a=b;}\nBox<int,3>&moveAssign(Box<int,3>&a,Box<int,3>&b){return a=static_cast<Box<int,3>&&>(b);}\nvoid alias(){Alias r;}\nvoid different(){Box<unsigned int,4>r;}\nTrivial<int>trivialCopy(const Trivial<int>&r){return r;}\nTrivial<int>&trivialAssign(Trivial<int>&a,const Trivial<int>&b){return a=b;}\n'
    class_defaulted = check("v2-class-defaulted-protocol", class_defaulted_source, profile="cpp-core-v2")
    cf_functions = {f["name"]: f for f in class_defaulted["functions"]}
    cf_records = {r["id"]: r for r in class_defaulted["records"]}
    assert len(cf_functions) == len(class_defaulted["functions"])
    assert len(cf_records) == len(class_defaulted["records"])

    def cf_line(prefix):
        lines = [i for i, line in enumerate(class_defaulted_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(lines) == 1, (prefix, lines)
        return lines[0]

    def cf_function(prefix):
        found = [f for f in class_defaulted["functions"] if f["loc"]["line"] == cf_line(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0]

    def cf_record(prefix):
        found = [r for r in class_defaulted["records"] if r["loc"]["line"] == cf_line(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0]

    def cf_member(rid, result, source=None):
        params = ["ptr:"+rid] + ([source+rid] if source else [])
        found = [f for f in class_defaulted["functions"]
                 if f["loc"]["line"] in (cf_line(" Box()"), cf_line(" Box&operator="))
                 and f["result"] == result and [p["type"] for p in f["params"]] == params
                 and not f["name"].endswith("_destroy")]
        assert len(found) == 1, (rid, result, params, found)
        return found[0]

    copied = cf_function("Box<int,3>copy(")
    bid = copied["params"][0]["type"].removeprefix("ptr:")
    box = cf_records[bid]
    leaf = cf_record("struct Leaf{")
    lid = leaf["id"]
    assert [f["type"] for f in box["fields"]] == ["int", "int", lid, "arr:2:"+lid]
    constructor = cf_member(bid, "void")
    copy = cf_member(bid, "void", "cptr:")
    move = cf_member(bid, "void", "ptr:")
    copy_assignment = cf_member(bid, "ptr:"+bid, "cptr:")
    move_assignment = cf_member(bid, "ptr:"+bid, "ptr:")
    destruction = cf_functions[bid+"_destroy"]
    assert len({f["name"] for f in (constructor, copy, move, copy_assignment, move_assignment, destruction)}) == 6
    leaf_default = cf_function(" Leaf():")
    leaf_copy = cf_function(" Leaf(const Leaf&")
    leaf_move = cf_function(" Leaf(Leaf&&")
    leaf_copy_assignment = cf_function(" Leaf&operator=(const Leaf&")
    leaf_move_assignment = cf_function(" Leaf&operator=(Leaf&&")
    for function, target, count in ((constructor, leaf_default, 3), (copy, leaf_copy, 3),
                                     (move, leaf_move, 3), (copy_assignment, leaf_copy_assignment, 2),
                                     (move_assignment, leaf_move_assignment, 2)):
        calls = gc_calls(function)
        assert [c["callee"] for c in calls] == [target["name"]]*count
        assert not any(n["op"] == "assign" and n["target"]["type"] == bid for n in function["body"])
        for arg, parameter in zip(calls[0]["args"], function["params"]):
            assert np_pointer(function, arg) == ("field", ("parameter", parameter["name"]), box["fields"][2]["name"])
        if function in (constructor, copy, move):
            for index, call in enumerate(calls[1:]):
                for arg, parameter in zip(call["args"], function["params"]):
                    expected = ("element", ("field", ("parameter", parameter["name"]), box["fields"][3]["name"]), index)
                    assert np_pointer(function, arg) == expected
        else:
            # Sema represents nontrivial array assignment as a counted loop.
            assert any(n["op"] == "branch" for n in function["body"])
            returned = next(n["value"] for n in function["body"] if n["op"] == "return")
            assert np_pointer(function, returned) == ("parameter", function["params"][0]["name"])
    stores = [n for n in constructor["body"] if n["op"] == "assign" and n["target"]["kind"] == "member"]
    assert len(stores) == 1 and stores[0]["target"]["name"] == box["fields"][1]["name"]
    assert gc_identity(constructor, stores[0]["value"]) == 3
    for function in (copy, move, copy_assignment, move_assignment):
        stores = [n for n in function["body"] if n["op"] == "assign" and n["target"]["kind"] == "member" and n["target"]["type"] == "int"]
        assert [n["target"]["name"] for n in stores] == [f["name"] for f in box["fields"][:2]]
        for node, field in zip(stores, box["fields"]):
            for expr, parameter in zip((node["target"], node["value"]), function["params"]):
                assert gc_identity(function, expr) == ("member", ("parameter", parameter["name"]), field["name"])
    assert [c["callee"] for c in gc_calls(destruction)] == [lid+"_destroy"]*3
    receiver = ("parameter", destruction["params"][0]["name"])
    array = ("field", receiver, box["fields"][3]["name"])
    assert [np_pointer(destruction, c["args"][0]) for c in gc_calls(destruction)] == [
        ("element", array, 1), ("element", array, 0), ("field", receiver, box["fields"][2]["name"])]
    for prefix, target in (("Box<int,3>copy(", copy), ("Box<int,3>move(", move),
                            ("Box<int,3>&copyAssign(", copy_assignment), ("Box<int,3>&moveAssign(", move_assignment)):
        caller = cf_function(prefix)
        calls = gc_calls(caller)
        assert len(calls) == 1 and calls[0]["callee"] == target["name"]
        for arg, parameter in zip(calls[0]["args"], caller["params"]):
            assert np_pointer(caller, arg) == ("parameter", parameter["name"])
        if target["result"] != "void":
            returned = next(n["value"] for n in caller["body"] if n["op"] == "return")
            assert di_call_result(caller, returned) == calls[0]["target"]["name"]
    outside = cf_record("template<class T>struct Outside{")
    oid = outside["id"]
    for prefix, zeroed in (("void defaultInit(", []), ("void valueInit(", [bid])):
        function = cf_function(prefix)
        zeros = [n["target"]["type"] for n in function["body"] if n["op"] == "assign" and n["target"]["type"] in (bid, oid)]
        assert zeros == zeroed, (prefix, zeros)
        calls = gc_calls(function)
        assert [c["callee"] for c in calls] == [constructor["name"], cf_function("template<class T>Outside<T>::Outside(")["name"], oid+"_destroy", bid+"_destroy"]
        assert np_pointer(function, calls[0]["args"][0]) == np_pointer(function, calls[3]["args"][0])
        assert np_pointer(function, calls[1]["args"][0]) == np_pointer(function, calls[2]["args"][0])
    assert gc_calls(cf_function("void alias("))[0]["callee"] == constructor["name"]
    different = gc_calls(cf_function("void different("))[0]
    uid = different["args"][0]["type"].removeprefix("ptr:")
    assert uid != bid and different["callee"] == cf_member(uid, "void")["name"]
    assert cf_records[uid]["fields"][1]["type"] == "uint"
    forced = cf_record("template<class T>struct Forced{")
    forced_constructor = cf_function("template<class T>Forced<T>::Forced(")
    assert [p["type"] for p in forced_constructor["params"]] == ["ptr:"+forced["id"]]
    assert [c["callee"] for c in gc_calls(forced_constructor)] == [leaf_default["name"]]
    assert all(c["callee"] != forced_constructor["name"] for f in class_defaulted["functions"] for c in gc_calls(f))
    trivial = cf_record("template<class T>struct Trivial{")
    tid = trivial["id"]
    for prefix in ("Trivial<int>trivialCopy(", "Trivial<int>&trivialAssign("):
        function = cf_function(prefix)
        assert not gc_calls(function)
        stores = [n for n in function["body"] if n["op"] == "assign" and n["target"]["type"] == tid]
        assert len(stores) == 1
        for expr, parameter in zip((stores[0]["target"], stores[0]["value"]), function["params"]):
            assert gc_identity(function, expr) == ("parameter", parameter["name"])
    assert not any(f["loc"]["line"] == cf_line("template<class T>struct Trivial{") for f in class_defaulted["functions"])
    for function in class_defaulted["functions"]:
        assert not any(n["op"] == "mapped_call" for n in function["body"])
        for call in gc_calls(function):
            callee = cf_functions[call["callee"]]
            assert [a["type"] for a in call["args"]] == [p["type"] for p in callee["params"]]
    with tempfile.TemporaryDirectory(prefix="neverc-class-defaulted-relocated-") as temp:
        relocated = check("class-defaulted-relocated", class_defaulted_source, root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == class_defaulted, "defaulted template identities depend on the absolute root"

    class_defaulted_lazy_source = 'template<class T>struct Lazy{\n T n=T::missing;\n Lazy()noexcept;Lazy(const Lazy&)noexcept;Lazy(Lazy&&)noexcept;\n Lazy&operator=(const Lazy&)noexcept;Lazy&operator=(Lazy&&)noexcept;~Lazy()noexcept;\n};\ntemplate<class T>Lazy<T>::Lazy()noexcept=default;\ntemplate<class T>Lazy<T>::Lazy(const Lazy&)noexcept=default;\ntemplate<class T>Lazy<T>::Lazy(Lazy&&)noexcept=default;\ntemplate<class T>Lazy<T>&Lazy<T>::operator=(const Lazy&)noexcept=default;\ntemplate<class T>Lazy<T>&Lazy<T>::operator=(Lazy&&)noexcept=default;\ntemplate<class T>Lazy<T>::~Lazy()noexcept=default;\nbool query(Lazy<int>&a,const Lazy<int>&b){return noexcept(Lazy<int>())&&noexcept(Lazy<int>(b))&&noexcept(Lazy<int>(static_cast<Lazy<int>&&>(a)))&&noexcept(a=b)&&noexcept(a=static_cast<Lazy<int>&&>(a));}\n'
    class_defaulted_lazy = check("v2-class-defaulted-lazy-protocol", class_defaulted_lazy_source, profile="cpp-core-v2")
    assert len(class_defaulted_lazy["records"]) == 1
    assert len(class_defaulted_lazy["functions"]) == 1
    assert class_defaulted_lazy["functions"][0]["result"] == "bool"
    assert not gc_calls(class_defaulted_lazy["functions"][0])

    class_defaulted_positive = {
        'promoted-constructor': 'template<class T>struct R{T n;R()=default;};',
        'promoted-destructor': 'template<class T>struct R{~R()=default;};',
        'all-inline': 'template<class T>struct R{T n=3;R()=default;R(const R&)=default;R(R&&)=default;R&operator=(const R&)=default;R&operator=(R&&)=default;~R()=default;};int f(){R<int>a;R<int>b(a);R<int>c(static_cast<R<int>&&>(b));a=c;b=static_cast<R<int>&&>(c);return a.n+b.n;}',
        'nonconst-copy': 'struct I{int n;I(I&s):n(++s.n){}};template<class T>struct R{T n;I i;R(R&)=default;};R<int>f(R<int>&r){return r;}',
        'nonconst-assignment': 'struct I{int n;I&operator=(I&s){n=++s.n;return *this;}};template<class T>struct R{T n;I i;R&operator=(R&)=default;};R<int>&f(R<int>&a,R<int>&b){return a=b;}',
        'copy-ref-qualifier': 'template<class T>struct R{T n;R&operator=(const R&)& =default;};R<int>&f(R<int>&a,const R<int>&b){return a=b;}',
        'move-ref-qualifier': 'template<class T>struct R{T n;R&operator=(R&&)&&=default;};R<int>&f(R<int>&a,R<int>&b){return static_cast<R<int>&&>(a)=static_cast<R<int>&&>(b);}',
        'empty': 'template<class T>struct R{R()=default;R(const R&)=default;R&operator=(const R&)=default;~R()=default;};void f(){R<int>a;R<int>b(a);a=b;}',
        'auto-value': 'template<auto N>struct R{int n=N;R()=default;R(const R&)=default;R&operator=(const R&)=default;};int f(){R<3>a;R<3u>b;R<1+2>c(a);c=a;return a.n+b.n+c.n;}',
        'class-instantiation': 'struct I{int n;I():n(3){}};template<class T>struct R{T n=4;I i;R()=default;};template struct R<int>;',
        'member-instantiation': 'struct I{int n;I():n(3){}};template<class T>struct R{T n=4;I i;R()=default;};template R<int>::R();',
        'forced-copy': 'struct I{int n;I(const I&s):n(s.n+1){}};template<class T>struct R{T n;I i;R(const R&)=default;};template R<int>::R(const R<int>&);',
        'forced-assignment': 'struct I{int n;I&operator=(const I&s){n=s.n+1;return *this;}};template<class T>struct R{T n;I i;R&operator=(const R&)=default;};template R<int>&R<int>::operator=(const R<int>&);',
        'class-specialization': 'template<class T>struct R{T n;R()=default;};template<>struct R<int>{int n=7;R()=default;R(const R&)=default;R&operator=(const R&)=default;};int f(){R<int>a;R<int>b(a);a=b;return a.n;}',
        'member-specialization': 'template<class T>struct R{T n=3;R();};template<class T>R<T>::R()=default;template<>R<int>::R()=default;int f(){R<int>a;return a.n;}',
        'visible-later': 'template<class T>struct R{T n=3;R();};template struct R<int>;template<class T>R<T>::R()=default;int f(){R<int>a;return a.n;}',
        'lazy-dependent-dmi': 'template<class T>struct R{T n=T::missing;R()=default;};static_assert(sizeof(R<int>)==sizeof(int));',
        'lazy-unsupported-dmi': 'template<class T>struct R{T n=static_cast<T>(1.0);R()=default;};static_assert(sizeof(R<int>)==sizeof(int));',
        'lazy-deleted-copy': 'struct I{int n;I(I&&s):n(s.n){}};template<class T>struct R{T n;I i;R(const R&)=default;};static_assert(sizeof(R<int>)==sizeof(int)*2);',
        'lazy-defaulted-wrapper': 'template<class T>struct I{T n;~I(){T::missing();}};template<class T>struct R{I<T>i;~R()=default;};static_assert(sizeof(R<int>)==sizeof(int));',
        'lazy-missing-wrapper': 'template<class T>struct I{T n;~I();};template<class T>struct R{I<T>i;~R()=default;};static_assert(sizeof(R<int>)==sizeof(int));',
        'query-dependent-dmi': 'template<class T>struct R{T n=T::missing;explicit R()noexcept=default;};int f(){return sizeof(R<int>{});}',
        'query-defaulted-this': 'template<class T>struct R{T n;~R()noexcept(sizeof(this->n)>0)=default;};bool f(){return noexcept(R<int>{1});}struct S{int n;int get(){return this->n;}};int g(){S s{3};return s.get();}',
        'query-defaulted-constructor-this': 'template<class T>struct R{T n=3;R()noexcept(sizeof(this->n)>0)=default;};bool f(){return noexcept(R<int>());}',
        'nontrivial-array': 'struct I{int n;I():n(1){}I(const I&s):n(s.n+1){}I&operator=(const I&s){n=s.n+2;return *this;}};template<class T>struct R{T n;I a[2][2];R()=default;R(const R&)=default;R&operator=(const R&)=default;};void f(){R<int>a=R<int>();R<int>b(a);a=b;}',
        'implicit-members': 'struct I{int n;I():n(3){}};template<class T>struct R{T n;I i;};R<int>f(){return R<int>();}',
        'query-outside-default': 'template<class T>struct R{T n;R()noexcept;};template<class T>R<T>::R()noexcept=default;bool f(){return noexcept(R<int>());}',
        'query-outside-copy': 'template<class T>struct R{T n;R(const R&)noexcept;};template<class T>R<T>::R(const R&)noexcept=default;bool f(const R<int>&r){return noexcept(R<int>(r));}',
        'query-outside-move': 'template<class T>struct R{T n;R(R&&)noexcept;};template<class T>R<T>::R(R&&)noexcept=default;bool f(R<int>&r){return noexcept(R<int>(static_cast<R<int>&&>(r)));}',
        'query-outside-copy-assignment': 'template<class T>struct R{T n;R&operator=(const R&)noexcept;};template<class T>R<T>&R<T>::operator=(const R&)noexcept=default;bool f(R<int>&a,const R<int>&b){return noexcept(a=b);}',
        'query-outside-move-assignment': 'template<class T>struct R{T n;R&operator=(R&&)noexcept;};template<class T>R<T>&R<T>::operator=(R&&)noexcept=default;bool f(R<int>&a,R<int>&b){return noexcept(a=static_cast<R<int>&&>(b));}',
        'query-outside-destructor': 'template<class T>struct R{T n;~R()noexcept;};template<class T>R<T>::~R()noexcept=default;bool f(){return noexcept(R<int>{1});}',
        'lazy-explicit-defaulting': 'template<class T>struct R{T n=T::missing;R()noexcept=default;};template struct R<int>;static_assert(sizeof(R<int>)==sizeof(int));',
        'all-outside': 'template<class T>struct R{T n=3;R();R(const R&);R(R&&);R&operator=(const R&);R&operator=(R&&);~R();};template<class T>R<T>::R()=default;template<class T>R<T>::R(const R&)=default;template<class T>R<T>::R(R&&)=default;template<class T>R<T>&R<T>::operator=(const R&)=default;template<class T>R<T>&R<T>::operator=(R&&)=default;template<class T>R<T>::~R()=default;int f(){R<int>a;R<int>b(a);R<int>c(static_cast<R<int>&&>(b));a=c;b=static_cast<R<int>&&>(c);return a.n+b.n;}',
        'extern-unused': 'template<class T>struct R{T n=3;R();};template<class T>R<T>::R()=default;extern template R<int>::R();static_assert(sizeof(R<int>)==sizeof(int));',
        'protocol-storage-source': 'int trace=0;\nvoid mark(int n){trace=n;}\nstruct Leaf{\n int n;Leaf*self;\n Leaf():n(1),self(this){mark(1);}\n Leaf(const Leaf&s):n(s.n+10),self(this){mark(2);}\n Leaf(Leaf&&s):n(s.n+20),self(this){s.n=-1;mark(3);}\n Leaf&operator=(const Leaf&s){n=s.n+30;mark(4);return *this;}\n Leaf&operator=(Leaf&&s){n=s.n+40;s.n=-1;mark(5);return *this;}\n ~Leaf(){mark(6);}\n};\ntemplate<class T,int N>struct Box{\n T plain;T value=N;Leaf first;Leaf items[2];\n Box()=default;Box(const Box&)=default;Box(Box&&)=default;\n Box&operator=(const Box&)& =default;Box&operator=(Box&&)& =default;~Box()=default;\n};\ntemplate<class T>struct Outside{T plain;Leaf leaf;Outside();~Outside();};\ntemplate<class T>Outside<T>::Outside()=default;\ntemplate<class T>Outside<T>::~Outside()=default;\ntemplate<class T>struct Trivial{T n;T*p;Trivial(const Trivial&)=default;Trivial&operator=(const Trivial&)=default;};\ntemplate<class T>struct Forced{T n=3;Leaf leaf;Forced();};\ntemplate<class T>Forced<T>::Forced()=default;\ntemplate struct Forced<int>;\nusing Alias=Box<int,3>;\nvoid defaultInit(){Box<int,3>r;Outside<int>o;}\nvoid valueInit(){Box<int,3>r=Box<int,3>();Outside<int>o=Outside<int>();}\nBox<int,3>copy(const Box<int,3>&r){return r;}\nBox<int,3>move(Box<int,3>&r){return static_cast<Box<int,3>&&>(r);}\nBox<int,3>&copyAssign(Box<int,3>&a,const Box<int,3>&b){return a=b;}\nBox<int,3>&moveAssign(Box<int,3>&a,Box<int,3>&b){return a=static_cast<Box<int,3>&&>(b);}\nvoid alias(){Alias r;}\nvoid different(){Box<unsigned int,4>r;}\nTrivial<int>trivialCopy(const Trivial<int>&r){return r;}\nTrivial<int>&trivialAssign(Trivial<int>&a,const Trivial<int>&b){return a=b;}\n',
        'protocol-lazy-source': 'template<class T>struct Lazy{\n T n=T::missing;\n Lazy()noexcept;Lazy(const Lazy&)noexcept;Lazy(Lazy&&)noexcept;\n Lazy&operator=(const Lazy&)noexcept;Lazy&operator=(Lazy&&)noexcept;~Lazy()noexcept;\n};\ntemplate<class T>Lazy<T>::Lazy()noexcept=default;\ntemplate<class T>Lazy<T>::Lazy(const Lazy&)noexcept=default;\ntemplate<class T>Lazy<T>::Lazy(Lazy&&)noexcept=default;\ntemplate<class T>Lazy<T>&Lazy<T>::operator=(const Lazy&)noexcept=default;\ntemplate<class T>Lazy<T>&Lazy<T>::operator=(Lazy&&)noexcept=default;\ntemplate<class T>Lazy<T>::~Lazy()noexcept=default;\nbool query(Lazy<int>&a,const Lazy<int>&b){return noexcept(Lazy<int>())&&noexcept(Lazy<int>(b))&&noexcept(Lazy<int>(static_cast<Lazy<int>&&>(a)))&&noexcept(a=b)&&noexcept(a=static_cast<Lazy<int>&&>(a));}\n',
    }
    for name, source in class_defaulted_positive.items():
        check("v2-class-defaulted-positive-" + name, source, profile="cpp-core-v2")
    class_defaulted_reject = {
        'selected-dmi': 'template<class T>struct R{T n=static_cast<T>(1.0);R()=default;};void f(){R<int>r;}',
        'forced-dmi': 'template<class T>struct R{T n=static_cast<T>(1.0);R();};template<class T>R<T>::R()=default;template struct R<int>;',
        'forced-member-dmi': 'template<class T>struct R{T n=static_cast<T>(1.0);R();};template<class T>R<T>::R()=default;template R<int>::R();',
        'forced-member-operation': 'template<class T>struct I{T n;I(const I&s):n(s.n){double v=1.0;}};template<class T>struct R{I<T>i;R(const R&);};template<class T>R<T>::R(const R&)=default;template struct R<int>;',
        'selected-member-destruction': 'template<class T>struct I{T n;~I(){double v=1.0;}};template<class T>struct R{I<T>i;~R()=default;};void f(){R<int>r{{1}};}',
        'query-default-spec': 'template<class T>struct R{T n;R()noexcept(sizeof(T)==sizeof(double))=default;};bool f(){return noexcept(R<int>());}',
        'query-copy-spec': 'template<class T>struct R{T n;R(const R&)noexcept(sizeof(T)==sizeof(double))=default;};bool f(const R<int>&r){return noexcept(R<int>(r));}',
        'query-assignment-spec': 'template<class T>struct R{T n;R&operator=(const R&)noexcept(sizeof(T)==sizeof(double))=default;};bool f(R<int>&a,const R<int>&b){return noexcept(a=b);}',
        'query-destructor-spec': 'template<class T>struct R{T n;~R()noexcept(sizeof(this->n)==sizeof(double))=default;};bool f(){return noexcept(R<int>{1});}',
        'attribute': 'template<class T>struct R{[[deprecated]]R()=default;};',
        'deleted-written': 'template<class T>struct R{R()=delete;};',
        'virtual': 'template<class T>struct R{virtual ~R()=default;};',
        'explicit-destruction': 'template<class T>struct R{~R()=default;};void f(R<int>&r){r.~R();}',
        'outer-type': 'template<int N>struct R{R();};template<decltype(static_cast<int>(1.0)) N>R<N>::R()=default;',
    }
    for name, source in class_defaulted_reject.items():
        check("v2-class-defaulted-reject-" + name, source, 'TR0201', profile="cpp-core-v2")
    class_defaulted_invalid = {
        'defaulted-ordinary': 'template<class T>struct R{int f()=default;};',
        'own-constructor-template': 'template<class T>struct R{template<class U>R(U)=default;};',
        'copy-extra': 'template<class T>struct R{R(const R&,int n=0)=default;};void f(R<int>&r){R<int>x(r);}',
        'deleted-used': 'struct I{int n;I(I&&s):n(s.n){}};template<class T>struct R{T n;I i;R(const R&)=default;};R<int>f(const R<int>&r){return r;}',
        'dependent-used': 'template<class T>struct R{T n=T::missing;R()=default;};void f(){R<int>r;}',
        'wrong-ref-qualifier': 'template<class T>struct R{T n;R&operator=(R&&)&&=default;};void f(R<int>&a,R<int>&b){a=static_cast<R<int>&&>(b);}',
        'private-copy': 'template<class T>class R{R(const R&)=default;public:T n;};R<int>f(const R<int>&r){return r;}',
    }
    for name, source in class_defaulted_invalid.items():
        check("v2-class-defaulted-invalid-" + name, source, 'TR0202', profile="cpp-core-v2")
    class_defaulted_missing = {
        'member-constructor': 'template<class T>struct I{T n;I();};template<class T>struct R{I<T>i;R()=default;};void f(){R<int>r;}',
        'member-copy': 'template<class T>struct I{T n;I(const I&);};template<class T>struct R{I<T>i;R(const R&)=default;};R<int>f(const R<int>&r){return r;}',
        'member-destructor': 'template<class T>struct I{T n;~I();};template<class T>struct R{I<T>i;~R()=default;};void f(){R<int>r{{1}};}',
        'direct-result-destructor': 'template<class T>struct I{T n;~I();};template<class T>struct R{I<T>i;~R()=default;};R<int>f(){return R<int>{{1}};}',
        'specialization-declaration': 'template<class T>struct R{T n;R();};template<class T>R<T>::R()=default;template<>R<int>::R();',
        'extern-default-construction': 'template<class T>struct R{T n=3;R();};template<class T>R<T>::R()=default;extern template R<int>::R();void f(){R<int>r;}',
        'extern-copy-assignment': 'template<class T>struct R{T n;R&operator=(const R&);};template<class T>R<T>&R<T>::operator=(const R&)=default;extern template R<int>&R<int>::operator=(const R<int>&);void f(R<int>&a,const R<int>&b){a=b;}',
    }
    for name, source in class_defaulted_missing.items():
        check("v2-class-defaulted-missing-" + name, source, 'TR0203', profile="cpp-core-v2")
    check("v1-class-defaulted-member", 'template<class T>struct R{T n;R()=default;};', "TR0201")

    class_destructors_source = 'int trace=0;\nvoid mark(int n){trace=trace*10+n;}\ntemplate<class T,int N>struct Leaf{T n;Leaf(T v):n(v){}~Leaf(){mark(N);}};\ntemplate<class T>struct Box{Leaf<T,1>first;Leaf<T,2>items[2];Box():first(7),items{8,9}{}~Box(){mark(3);}};\nstruct Wrapper{Box<int>box;};\nstruct OnlyBody{Leaf<int,1>leaf;~OnlyBody(){mark(4);}};\nstruct UnusedDefaulted{Leaf<int,1>leaf;~UnusedDefaulted()=default;};\ntemplate<class T>struct Forced{T n;~Forced(){mark(5);}};\ntemplate struct Forced<int>;\ntemplate<class T>struct Value{T n;~Value(){mark(n);n=99;}};\ntemplate<auto N>struct State{~State(){static int count=N;mark(++count);}};\ntemplate<class T>struct Local{\n T n;\n ~Local(){struct Inside{T n;~Inside(){mark(n);}};Inside v{n};}\n};\nusing Alias=Leaf<int,1>;\nvoid leafInt(){Leaf<int,1>v(4);}\nvoid leafSame(){Alias v(5);}\nvoid leafUnsigned(){Leaf<unsigned int,1>v(6u);}\nvoid leafOther(){Leaf<int,2>v(7);}\nvoid box(){Box<int>b;}\nvoid wrapped(){Wrapper w;}\nint captured(){Value<int>v{7};return v.n;}\nint observe(const Value<int>&v){return v.n;}\nint full(){return observe(Value<int>{3});}\nint consume(Value<int>v){return v.n;}\nValue<int>makeResult(){return Value<int>{4};}\nvoid result(){Value<int>v=makeResult();}\nvoid array(){Value<int>v[2]={{1},{2}};}\nvoid stateInt(){State<3>s;}\nvoid stateSame(){State<1+2>s;}\nvoid stateUnsigned(){State<3u>s;}\nvoid localInt(){Local<int>v{2};}\nvoid localUnsigned(){Local<unsigned int>v{3u};}\n'
    class_destructors = check("v2-class-destructors-protocol", class_destructors_source, profile="cpp-core-v2")
    cd_functions = {f["name"]: f for f in class_destructors["functions"]}
    cd_records = {r["id"]: r for r in class_destructors["records"]}
    cd_globals = {g["name"] for g in class_destructors["globals"]}
    assert len(cd_functions) == len(class_destructors["functions"])
    assert len(cd_records) == len(class_destructors["records"])

    def cd_line(prefix):
        found = [i for i, line in enumerate(class_destructors_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0]

    def cd_function(prefix):
        found = [f for f in class_destructors["functions"] if f["loc"]["line"] == cd_line(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0]

    def cd_record(prefix):
        found = [r for r in class_destructors["records"] if r["loc"]["line"] == cd_line(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0]

    def cd_destroyed(prefix):
        calls = [c for c in gc_calls(cd_function(prefix)) if c["callee"].endswith("_destroy")]
        assert len(calls) == 1, (prefix, calls)
        return calls[0]["callee"][:-len("_destroy")]

    cd_mark = cd_function("void mark(")["name"]
    leaf_ids = []
    for prefix, scalar, value, tag in (("void leafInt(", "int", 4, 1),
                                       ("void leafUnsigned(", "uint", 6, 1),
                                       ("void leafOther(", "int", 7, 2)):
        function = cd_function(prefix)
        rid = cd_destroyed(prefix)
        leaf_ids.append(rid)
        calls = gc_calls(function)
        assert len(calls) == 2 and calls[1]["callee"] == rid+"_destroy"
        constructor = cd_functions[calls[0]["callee"]]
        assert constructor["result"] == "void"
        assert [p["type"] for p in constructor["params"]] == ["ptr:"+rid, scalar]
        assert gc_identity(function, calls[0]["args"][1]) == value
        assert np_pointer(function, calls[0]["args"][0]) == np_pointer(function, calls[1]["args"][0])
        body = cd_functions[rid+"_destroy"]
        assert [c["callee"] for c in gc_calls(body)] == [cd_mark]
        assert gc_identity(body, gc_calls(body)[0]["args"][0]) == tag
    assert len(set(leaf_ids)) == 3
    assert cd_destroyed("void leafSame(") == leaf_ids[0]
    assert gc_calls(cd_function("void leafSame("))[0]["callee"] == gc_calls(cd_function("void leafInt("))[0]["callee"]

    bid = cd_destroyed("void box(")
    box = cd_records[bid]
    assert [f["type"] for f in box["fields"]] == [leaf_ids[0], "arr:2:"+leaf_ids[2]]
    body = cd_functions[bid+"_destroy"]
    calls = gc_calls(body)
    assert [c["callee"] for c in calls] == [cd_mark, leaf_ids[2]+"_destroy", leaf_ids[2]+"_destroy", leaf_ids[0]+"_destroy"]
    assert gc_identity(body, calls[0]["args"][0]) == 3
    root = ("parameter", body["params"][0]["name"])
    member = ("field", root, box["fields"][0]["name"])
    items = ("field", root, box["fields"][1]["name"])
    assert [np_pointer(body, c["args"][0]) for c in calls[1:]] == [("element", items, 1), ("element", items, 0), member]
    wid = cd_destroyed("void wrapped(")
    wrapper = cd_functions[wid+"_destroy"]
    assert [c["callee"] for c in gc_calls(wrapper)] == [bid+"_destroy"]
    expected = ("field", ("parameter", wrapper["params"][0]["name"]), cd_records[wid]["fields"][0]["name"])
    assert np_pointer(wrapper, gc_calls(wrapper)[0]["args"][0]) == expected
    forced = cd_record("template<class T>struct Forced{")
    assert [c["callee"] for c in gc_calls(cd_functions[forced["id"]+"_destroy"])] == [cd_mark]
    uncalled = cd_record("struct OnlyBody{")
    assert [c["callee"] for c in gc_calls(cd_functions[uncalled["id"]+"_destroy"])] == [cd_mark, leaf_ids[0]+"_destroy"]
    assert cd_record("struct UnusedDefaulted{")["id"]+"_destroy" not in cd_functions

    value = cd_record("template<class T>struct Value{")
    vid = value["id"]
    captured = cd_function("int captured(")
    assert [c["callee"] for c in gc_calls(captured)] == [vid+"_destroy"]
    returned = next(n["value"] for n in captured["body"] if n["op"] == "return")
    captures = [i for i, n in enumerate(captured["body"]) if n["op"] == "assign" and n["target"].get("name") == returned.get("name")]
    cleanups = [i for i, n in enumerate(captured["body"]) if n["op"] == "call"]
    assert len(captures) == 1 and captures[0] < min(cleanups)
    full = cd_function("int full(")
    calls = gc_calls(full)
    assert [c["callee"] for c in calls] == [cd_function("int observe(")["name"], vid+"_destroy"]
    assert np_pointer(full, calls[0]["args"][0]) == np_pointer(full, calls[1]["args"][0])
    consume = cd_function("int consume(")
    assert [p["type"] for p in consume["params"]] == ["ptr:"+vid]
    assert [c["callee"] for c in gc_calls(consume)] == [vid+"_destroy"]
    assert np_pointer(consume, gc_calls(consume)[0]["args"][0]) == ("parameter", consume["params"][0]["name"])
    make = cd_function("Value<int>makeResult(")
    assert make["result"] == "void" and [p["type"] for p in make["params"]] == ["ptr:"+vid]
    assert not gc_calls(make), "returned storage must be cleaned by its eventual owner"
    result = cd_function("void result(")
    calls = gc_calls(result)
    assert [c["callee"] for c in calls] == [make["name"], vid+"_destroy"]
    assert np_pointer(result, calls[0]["args"][0]) == np_pointer(result, calls[1]["args"][0])
    array = cd_function("void array(")
    calls = gc_calls(array)
    assert [c["callee"] for c in calls] == [vid+"_destroy"]*2
    places = [np_pointer(array, c["args"][0]) for c in calls]
    assert places[0][0] == places[1][0] == "element" and places[0][1] == places[1][1]
    assert [p[2] for p in places] == [1, 0]

    sid = cd_destroyed("void stateInt(")
    uid = cd_destroyed("void stateUnsigned(")
    assert sid == cd_destroyed("void stateSame(") and sid != uid
    state_globals = []
    for rid in (sid, uid):
        names = {n["name"] for n in walk(cd_functions[rid+"_destroy"]) if n.get("kind") == "var" and n.get("name") in cd_globals}
        assert len(names) == 1, names
        state_globals.append(names)
    assert not state_globals[0] & state_globals[1]
    local_ids = [cd_destroyed(p) for p in ("void localInt(", "void localUnsigned(")]
    assert len(set(local_ids)) == 2
    inside_ids = []
    for rid, scalar in zip(local_ids, ("int", "uint")):
        calls = gc_calls(cd_functions[rid+"_destroy"])
        assert len(calls) == 1 and calls[0]["callee"].endswith("_destroy")
        inside = calls[0]["callee"][:-len("_destroy")]
        assert [f["type"] for f in cd_records[inside]["fields"]] == [scalar]
        inside_ids.append(inside)
    assert len(set(inside_ids)) == 2
    for function in class_destructors["functions"]:
        if function["name"].endswith("_destroy"):
            rid = function["name"][:-len("_destroy")]
            assert rid in cd_records and function["result"] == "void" and function["internal"]
            assert [p["type"] for p in function["params"]] == ["ptr:"+rid]
        for call in gc_calls(function):
            assert call["callee"] in cd_functions
            assert [a["type"] for a in call["args"]] == [p["type"] for p in cd_functions[call["callee"]]["params"]]
    with tempfile.TemporaryDirectory(prefix="neverc-class-destructors-relocated-") as temp:
        relocated = check("v2-class-destructors-relocated", class_destructors_source,
                          root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == class_destructors, "destructor identities depend on absolute paths"
    lazy_destructors_source = """template<class T>struct Lazy{T n;~Lazy()noexcept(sizeof(this->n)>0){T::missing();}};
struct Wrapper{Lazy<int>member;};
template<class T>struct Missing{T n;~Missing();};
static_assert(sizeof(Wrapper)==sizeof(int));
static_assert(sizeof(Missing<int>)==sizeof(int));
bool query(){return noexcept(Lazy<int>{1});}
"""
    lazy_destructors = check("v2-class-destructors-lazy", lazy_destructors_source, profile="cpp-core-v2")
    assert len(lazy_destructors["records"]) == 3
    assert len(lazy_destructors["functions"]) == 1
    query = lazy_destructors["functions"][0]
    assert query["result"] == "bool" and not query["params"] and not gc_calls(query)
    assert not any(f["name"].endswith("_destroy") for f in lazy_destructors["functions"])

    class_destructors_positive = {
        'promoted-aggregate-method': 'template<class T>struct R{T n;~R(){}};',
        'promoted-constructor': 'template<class T>struct R{T n;R(T v):n(v){}~R(){}};',
        'inline': 'int n=0;template<class T>struct R{T v;~R(){n=v;}};int main(){{R<int>r{3};}return n-3;}',
        'out-of-line': 'int n=0;template<class T>struct R{T v;~R();};template<class T>R<T>::~R(){n=v;}int main(){{R<int>r{3};}return n-3;}',
        'constructor': 'int n=0;template<class T>struct R{T v;R(T x):v(x){}~R(){n=v;}};int main(){{R<int>r(3);}return n-3;}',
        'scalar-argument': 'int n=0;template<int N>struct R{~R(){n=N;}};int main(){{R<3>r;}return n-3;}',
        'auto-argument': 'int n=0;template<auto N>struct R{~R(){n=static_cast<int>(N);}};int main(){{R<3u>r;}return n-3;}',
        'boolean-branch': 'int n=0;template<class T,bool B>struct R{~R(){if constexpr(B)n=3;else n=T::missing;}};int main(){{R<int,true>r;}return n-3;}',
        'dependent-noexcept': 'template<class T>struct R{~R()noexcept(sizeof(T)==sizeof(int)) {}};void f(){R<int>r;}static_assert(noexcept(R<int>{}));',
        'throw-empty': 'template<class T>struct R{~R()throw(){}};void f(){R<int>r;}',
        'noexcept-false': 'template<class T>struct R{~R()noexcept(false){}};void f(){R<int>r;}static_assert(!noexcept(R<int>{}));',
        'type-only-missing': 'template<class T>struct R{T n;~R();};static_assert(sizeof(R<int>)==sizeof(int));',
        'query-missing-body': 'template<class T>struct R{T n;~R()noexcept(sizeof(T)==sizeof(int));};static_assert(noexcept(R<int>{1}));static_assert(sizeof(R<int>{1})==sizeof(int));',
        'query-this-specification': 'template<class T>struct R{T n;~R()noexcept(sizeof(this->n)>0);};bool query(){return noexcept(R<int>{1});}struct S{int n;int get(){return this->n;}};int f(){S s{3};return s.get();}',
        'query-uninstantiated-body': 'template<class T>struct R{T n;~R()noexcept(sizeof(T)==sizeof(int)){T::missing();}};static_assert(noexcept(R<int>{1}));',
        'direct-result-definition': 'template<class T>struct R{T n;~R(){n=3;}};R<int>make(){return R<int>{1};}',
        'type-only-unsupported': 'template<class T>struct R{T n;~R(){double v=1.0;}};static_assert(sizeof(R<int>)==sizeof(int));',
        'type-only-dependent': 'template<class T>struct R{T n;~R(){T::missing();}};static_assert(sizeof(R<int>)==sizeof(int));',
        'unused-ordinary-wrapper': 'template<class T>struct R{T n;~R(){T::missing();}};struct W{R<int>r;};static_assert(sizeof(W)==sizeof(int));',
        'unused-template-wrapper': 'template<class T>struct R{T n;~R(){T::missing();}};template<class T>struct W{R<T>r;};static_assert(sizeof(W<int>)==sizeof(int));',
        'class-instantiation': 'template<class T>struct R{T n;~R(){n=3;}};template struct R<int>;',
        'member-instantiation': 'template<class T>struct R{T n;~R(){n=3;}};template R<int>::~R();',
        'class-declaration-only-member': 'template<class T>struct R{T n;~R();};template struct R<int>;int f(){return sizeof(R<int>);}',
        'member-specialization': 'int n=0;template<class T>struct R{T v;~R(){n=1;}};template<>R<int>::~R(){n=3;}int main(){{R<int>r{};}return n-3;}',
        'class-specialization': 'int n=0;template<class T>struct R{T v;~R(){n=1;}};template<>struct R<int>{int v;~R(){n=3;}};int main(){{R<int>r{};}return n-3;}',
        'alias': 'int n=0;template<class T>struct R{~R(){++n;}};using A=R<int>;int main(){{A a;R<int>b;}return n-2;}',
        'local-record': 'int n=0;template<class T>struct R{T v;~R(){struct I{T v;~I(){n=v;}};I i{v};}};int main(){{R<int>r{3};}return n-3;}',
        'static-local': 'int n=0;template<auto N>struct R{~R(){static int count=N;n=++count;}};int main(){{R<3>r;}return n-4;}',
        'implicit-wrapper': 'int n=0;template<class T>struct R{T v;~R(){n=n*10+v;}};struct W{R<int>r[2];};int main(){{W w{{{1},{2}}};}return n-21;}',
        'ordinary-user-wrapper': 'int n=0;template<class T>struct R{T v;~R(){n=n*10+v;}};struct W{R<int>r;~W(){n=2;}};int main(){{W w{{1}};}return n-21;}',
        'ordinary-defaulted-wrapper': 'int n=0;template<class T>struct R{T v;~R(){n=v;}};struct W{R<int>r;~W()=default;};int main(){{W w{{3}};}return n-3;}',
        'template-implicit-wrapper': 'int n=0;template<class T>struct R{T v;~R(){n=v;}};template<class T>struct W{R<T>r;};int main(){{W<int>w{{3}};}return n-3;}',
        'ordinary-unused-body': 'int n=0;template<class T>struct R{T v;~R(){n=v;}};struct W{R<int>r;~W(){n=2;}};',
        'private-static-factory': 'int n=0;template<class T>class R{T v;~R(){n=v;}public:R(T x):v(x){}static void run(){R r(3);}};int main(){R<int>::run();return n-3;}',
        'default-argument-temporary': 'int n=0;template<class T>struct R{T v;~R(){n=v;}};int read(const R<int>&r=R<int>{3}){return r.v;}int main(){int v=read();return v-3+n-3;}',
        'array-range': 'int n=0;template<class T>struct R{T v;~R(){n+=v;}};int main(){{R<int>a[2]={{1},{2}};for(auto&r:a)++r.v;}return n-5;}',
    }
    for name, source in class_destructors_positive.items():
        check("v2-class-destructors-positive-" + name, source, profile="cpp-core-v2")
    class_destructors_reject = {
        'selected-floating': 'template<class T>struct R{~R(){double n=1.0;}};void f(){R<int>r;}',
        'dead-floating': 'template<class T>struct R{~R(){if(false){double n=1.0;}}};void f(){R<int>r;}',
        'folded-floating': 'template<class T>struct R{~R(){int n=static_cast<int>(1.0);}};void f(){R<int>r;}',
        'forced-floating': 'template<class T>struct R{~R(){double n=1.0;}};template struct R<int>;',
        'forced-member-floating': 'template<class T>struct R{~R(){double n=1.0;}};template R<int>::~R();',
        'selected-noexcept': 'template<class T>struct R{~R()noexcept(1.0>0.0){}};void f(){R<int>r;}',
        'queried-noexcept-floating': 'template<class T>struct R{T n;~R()noexcept(1.0>0.0);};bool query(){return noexcept(R<int>{1});}',
        'queried-noexcept-type': 'template<class T>struct R{T n;~R()noexcept(sizeof(double)>0);};bool query(){return noexcept(R<int>{1});}',
        'queried-dependent-noexcept-type': 'template<class T>struct R{T n;~R()noexcept(sizeof(T)==sizeof(double));};bool query(){return noexcept(R<int>{1});}',
        'outer-parameter-type': 'template<int N>struct R{~R();};template<decltype(static_cast<int>(1.0)) N>R<N>::~R(){}',
        'attribute': 'template<class T>struct R{[[deprecated]]~R(){}};',
        'deleted-shape': 'template<class T>struct R{~R()=delete;};',
        'virtual': 'template<class T>struct R{virtual ~R(){}};',
        'explicit-call': 'template<class T>struct R{~R(){}};void f(R<int>&r){r.~R();}',
        'dead-explicit-call': 'template<class T>struct R{~R(){}};void f(R<int>&r){if(false)r.~R();}',
        'global-lifetime': 'template<class T>struct R{~R(){}};R<int>r;',
        'static-lifetime': 'template<class T>struct R{~R(){}};void f(){static R<int>r;}',
        'ordinary-unused-unsupported-member': 'template<class T>struct R{~R(){double n=1.0;}};struct W{R<int>r;~W(){}};',
    }
    for name, source in class_destructors_reject.items():
        check("v2-class-destructors-reject-" + name, source, 'TR0201', profile="cpp-core-v2")
    class_destructors_invalid = {
        'selected-dependent': 'template<class T>struct R{~R(){T::missing();}};void f(){R<int>r;}',
        'forced-dependent': 'template<class T>struct R{~R(){T::missing();}};template struct R<int>;',
        'private': 'template<class T>class R{~R(){}};void f(){R<int>r;}',
        'deleted-used': 'template<class T>struct R{~R()=delete;};void f(){R<int>r;}',
        'missing-explicit-instantiation': 'template<class T>struct R{~R();};template R<int>::~R();',
        'late-specialization': 'template<class T>struct R{~R(){}};void f(){R<int>r;}template<>R<int>::~R(){}',
        'duplicate': 'template<class T>struct R{~R(){}~R(){}};',
        'selected-noexcept-dependent': 'template<class T>struct R{~R()noexcept(T::missing){}};void f(){R<int>r;}',
    }
    for name, source in class_destructors_invalid.items():
        check("v2-class-destructors-invalid-" + name, source, 'TR0202', profile="cpp-core-v2")
    class_destructors_missing = {
        'local': 'template<class T>struct R{~R();};void f(){R<int>r;}',
        'temporary': 'template<class T>struct R{~R();};void f(){R<int>{};}',
        'reference-temporary': 'template<class T>struct R{~R();};void f(){const R<int>&r=R<int>{};}',
        'array': 'template<class T>struct R{~R();};void f(){R<int>r[2];}',
        'member': 'template<class T>struct R{~R();};struct W{R<int>r;};void f(){W w;}',
        'by-value': 'template<class T>struct R{~R();};void f(R<int>r){}',
        'direct-result': 'template<class T>struct R{T n;~R();};R<int>make(){return R<int>{1};}',
        'specialization-declaration': 'template<class T>struct R{~R(){}};template<>R<int>::~R();',
        'ordinary': 'struct R{~R();};',
        'extern-member': 'template<class T>struct R{~R(){}};extern template R<int>::~R();',
    }
    for name, source in class_destructors_missing.items():
        check("v2-class-destructors-missing-" + name, source, 'TR0203', profile="cpp-core-v2")
    check("v1-class-destructor", "template<class T>struct R{~R(){}};void f(){R<int>r;}", "TR0201")

    class_constructors_source = """struct Guard{int n;Guard(int v):n(v){}~Guard(){n=99;}};
template<class T,int N>struct Box{
 T n;
 Box(T v=N):n(v){}
 Box(const Box&s,int extra=N):n(s.n+extra){}
 Box(Box&&s):n(s.n){s.n=-1;}
};
template<class T,int N>struct Order{T first,second;Order(T v):second(first+N),first(v){}};
template<class T>struct Local{T n;Local(T v){struct Inside{T n;};Inside local{v};n=local.n;}};
template<auto N>struct State{int*p;State(){static int n=N;p=&n;}};
template<auto N>struct OtherState{int*p;OtherState(){static int n=N;p=&n;}};
template<class T>struct WithGuard{Guard member;Guard items[2];WithGuard(T v):member(v),items{Guard(v+1),Guard(v+2)}{}};
Box<int,3> makeInt(int v){return Box<int,3>(v);}
Box<int,1+2> makeSame(int v){return Box<int,1+2>(v);}
Box<unsigned int,3> makeUnsigned(unsigned int v){return Box<unsigned int,3>(v);}
Box<int,4> makeFour(int v){return Box<int,4>(v);}
Box<int,3> makeDefault(){return Box<int,3>();}
Box<int,3> copyDefault(const Box<int,3>&v){return Box<int,3>(v);}
Box<int,3> copyExplicit(const Box<int,3>&v){return Box<int,3>(v,5);}
Box<int,3> moveValue(Box<int,3>&v){return Box<int,3>(static_cast<Box<int,3>&&>(v));}
Order<int,2> ordered(int v){return Order<int,2>(v);}
Local<int> localInt(int v){return Local<int>(v);}
Local<unsigned int> localUnsigned(unsigned int v){return Local<unsigned int>(v);}
State<3> firstState(){return State<3>();}
State<1+2> sameState(){return State<1+2>();}
State<4> nextState(){return State<4>();}
State<3u> unsignedState(){return State<3u>();}
OtherState<3> otherState(){return OtherState<3>();}
int cleanup(){WithGuard<int>v(3);return v.member.n;}
template<class T>class Private{T n;public:Private(T v):n(v){}T get()const{return n;}};
Private<int> privateValue(int v){return Private<int>(v);}
int privateRead(const Private<int>&v){return v.get();}
"""
    class_constructors = check("v2-class-constructors-protocol", class_constructors_source, profile="cpp-core-v2")
    cc_records = {r["id"]: r for r in class_constructors["records"]}
    cc_functions = {f["name"]: f for f in class_constructors["functions"]}
    cc_globals = {g["name"]: g for g in class_constructors["globals"]}
    assert len(cc_records) == len(class_constructors["records"])
    assert len(cc_functions) == len(class_constructors["functions"])
    assert len(cc_globals) == len(class_constructors["globals"])

    def cc_line(prefix):
        lines = [i for i, line in enumerate(class_constructors_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(lines) == 1, (prefix, lines)
        return lines[0]

    def cc_function(prefix):
        found = [f for f in class_constructors["functions"] if f["loc"]["line"] == cc_line(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0]

    def cc_selected(prefix):
        calls = gc_calls(cc_function(prefix))
        assert len(calls) == 1, (prefix, calls)
        return cc_functions[calls[0]["callee"]]

    def cc_signature(function, result, params):
        assert function["result"] == result and [p["type"] for p in function["params"]] == params, function

    def cc_members(function, expr):
        pending = [expr]
        expanded = set()
        result = []
        while pending:
            current = pending.pop()
            if current.get("kind") == "member":
                result.append(current["name"])
            pending.extend(current.get("args", []))
            if current.get("kind") == "var" and current["name"] not in expanded:
                name = current["name"]
                expanded.add(name)
                if not any(p["name"] == name for p in function["params"]):
                    values = [n["value"] for n in function["body"] if n["op"] == "assign"
                              and n["target"].get("kind") == "var" and n["target"]["name"] == name]
                    assert len(values) == 1, (name, values)
                    pending.extend(values)
        return result

    constructors = []
    records = []
    for prefix, scalar in (("Box<int,3> makeInt(", "int"), ("Box<unsigned int,3> makeUnsigned(", "uint"),
                           ("Box<int,4> makeFour(", "int")):
        caller = cc_function(prefix)
        constructor = cc_selected(prefix)
        receiver = caller["params"][0]["type"]
        assert receiver.startswith("ptr:")
        record = cc_records[receiver[4:]]
        constructors.append(constructor)
        records.append(record)
        assert [f["type"] for f in record["fields"]] == [scalar]
        cc_signature(caller, "void", [receiver, scalar])
        cc_signature(constructor, "void", [receiver, scalar])
        assert constructor["loc"]["line"] == cc_line(" Box(T v=")
        call = gc_calls(caller)[0]
        assert np_pointer(caller, call["args"][0]) == ("parameter", caller["params"][0]["name"])
        assert gc_identity(caller, call["args"][1]) == ("parameter", caller["params"][1]["name"])
        writes = [n for n in constructor["body"] if n["op"] == "assign" and n["target"].get("kind") == "member"]
        assert len(writes) == 1
        assert np_place(constructor, writes[0]["target"]) == ("field", ("parameter", constructor["params"][0]["name"]), record["fields"][0]["name"])
        assert gc_identity(constructor, writes[0]["value"]) == ("parameter", constructor["params"][1]["name"])
    assert len({f["name"] for f in constructors}) == 3
    assert len({r["id"] for r in records}) == 3
    assert len({r["fields"][0]["name"] for r in records}) == 3
    assert cc_selected("Box<int,1+2> makeSame(")["name"] == constructors[0]["name"]
    default = cc_function("Box<int,3> makeDefault(")
    assert cc_selected("Box<int,3> makeDefault(")["name"] == constructors[0]["name"]
    assert gc_identity(default, gc_calls(default)[0]["args"][1]) == 3
    rid = records[0]["id"]
    copy = cc_selected("Box<int,3> copyDefault(")
    assert copy["name"] == cc_selected("Box<int,3> copyExplicit(")["name"]
    cc_signature(copy, "void", ["ptr:"+rid, "cptr:"+rid, "int"])
    for prefix, value in (("Box<int,3> copyDefault(", 3), ("Box<int,3> copyExplicit(", 5)):
        caller = cc_function(prefix)
        cc_signature(caller, "void", ["ptr:"+rid, "cptr:"+rid])
        call = gc_calls(caller)[0]
        assert np_pointer(caller, call["args"][0]) == ("parameter", caller["params"][0]["name"])
        assert np_pointer(caller, call["args"][1]) == ("parameter", caller["params"][1]["name"])
        assert gc_identity(caller, call["args"][2]) == value
    move = cc_selected("Box<int,3> moveValue(")
    cc_signature(move, "void", ["ptr:"+rid, "ptr:"+rid])
    assert move["name"] not in (copy["name"], constructors[0]["name"])
    move_writes = [n for n in move["body"] if n["op"] == "assign" and n["target"].get("kind") == "member"]
    assert [np_place(move, n["target"]) for n in move_writes] == [
        ("field", ("parameter", move["params"][0]["name"]), records[0]["fields"][0]["name"]),
        ("field", ("parameter", move["params"][1]["name"]), records[0]["fields"][0]["name"])]
    assert gc_identity(move, move_writes[0]["value"]) == ("member", ("parameter", move["params"][1]["name"]), records[0]["fields"][0]["name"])
    ordered = cc_selected("Order<int,2> ordered(")
    ordered_record = cc_records[ordered["params"][0]["type"][4:]]
    fields = ordered_record["fields"]
    writes = [n for n in ordered["body"] if n["op"] == "assign" and n["target"].get("kind") == "member"]
    assert [n["target"]["name"] for n in writes] == [f["name"] for f in fields]
    accesses = cc_members(ordered, writes[1]["value"])
    assert accesses == [fields[0]["name"]]
    local_records = []
    for prefix, scalar in (("Local<int> localInt(", "int"), ("Local<unsigned int> localUnsigned(", "uint")):
        constructor = cc_selected(prefix)
        types = {v["type"] for v in constructor["locals"] if v["type"] in cc_records}
        assert len(types) == 1, (prefix, types)
        record = cc_records[next(iter(types))]
        assert [f["type"] for f in record["fields"]] == [scalar]
        local_records.append(record)
    assert local_records[0]["id"] != local_records[1]["id"]
    assert local_records[0]["fields"][0]["name"] != local_records[1]["fields"][0]["name"]
    assert cc_selected("State<3> firstState(")["name"] == cc_selected("State<1+2> sameState(")["name"]
    storage = []
    for prefix in ("State<3> firstState(", "State<4> nextState(", "State<3u> unsignedState(", "OtherState<3> otherState("):
        constructor = cc_selected(prefix)
        assert constructor["result"] == "void" and len(constructor["params"]) == 1
        names = {n["name"] for n in walk(constructor["body"]) if n.get("kind") == "var" and n.get("name") in cc_globals}
        assert len(names) == 1
        storage.append(cc_globals[next(iter(names))])
    assert len(cc_globals) == len({g["name"] for g in storage}) == 4
    assert all(g["type"] == "int" and g["mutable"] for g in storage)
    assert [int(g["value"]["value"]) for g in storage] == [3, 4, 3, 3]
    guard = next(r for r in class_constructors["records"] if r["loc"]["line"] == cc_line("struct Guard{"))
    cleanup = cc_function("int cleanup(")
    calls = gc_calls(cleanup)
    assert len(calls) == 2
    owner = cc_records[calls[0]["args"][0]["type"][4:]]
    assert [f["type"] for f in owner["fields"]] == [guard["id"], "arr:2:"+guard["id"]]
    assert calls[1]["callee"] == owner["id"]+"_destroy"
    assert gc_identity(cleanup, calls[0]["args"][1]) == 3
    assert np_pointer(cleanup, calls[0]["args"][0]) == np_pointer(cleanup, calls[1]["args"][0])
    constructor = cc_functions[calls[0]["callee"]]
    initializers = gc_calls(constructor)
    assert len(initializers) == 3
    owner_param = ("parameter", constructor["params"][0]["name"])
    field_base = ("field", owner_param, owner["fields"][1]["name"])
    assert [np_pointer(constructor, n["args"][0]) for n in initializers] == [
        ("field", owner_param, owner["fields"][0]["name"]), ("element", field_base, 0), ("element", field_base, 1)]
    destructor = cc_functions[calls[1]["callee"]]
    destroys = gc_calls(destructor)
    assert len(destroys) == 3 and all(n["callee"] == guard["id"]+"_destroy" for n in destroys)
    destroyed_param = ("parameter", destructor["params"][0]["name"])
    field_base = ("field", destroyed_param, owner["fields"][1]["name"])
    assert [np_pointer(destructor, n["args"][0]) for n in destroys] == [
        ("element", field_base, 1), ("element", field_base, 0), ("field", destroyed_param, owner["fields"][0]["name"])]
    returned = next(n["value"] for n in cleanup["body"] if n["op"] == "return")
    captures = [i for i, n in enumerate(cleanup["body"]) if n["op"] == "assign" and n["target"].get("name") == returned.get("name")]
    assert len(captures) == 1 and captures[0] < cleanup["body"].index(calls[1])
    private_constructor = cc_selected("Private<int> privateValue(")
    private_record = cc_records[private_constructor["params"][0]["type"][4:]]
    assert [f["type"] for f in private_record["fields"]] == ["int"]
    assert private_record["layout"] == records[0]["layout"]
    private_getter = cc_selected("int privateRead(")
    cc_signature(private_getter, "int", ["cptr:"+private_record["id"]])
    assert {n["name"] for n in walk(private_getter["body"]) if n.get("kind") == "member"} == {private_record["fields"][0]["name"]}
    for function in class_constructors["functions"]:
        for call in gc_calls(function):
            assert call["callee"] in cc_functions
            assert [a["type"] for a in call["args"]] == [p["type"] for p in cc_functions[call["callee"]]["params"]]
    with tempfile.TemporaryDirectory(prefix="neverc-class-constructors-relocated-") as temp:
        relocated = check("v2-class-constructors-relocated", class_constructors_source,
                          root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == class_constructors
    class_constructors_positive = {
        'constructor': 'template<class T>struct R{T n;R(T v):n(v){}};',
        'parameter': 'template<class T>struct R{T n;R(T v):n(v){}};int main(){R<int>r(3);return r.n;}',
        'default': 'template<class T,int N>struct R{T n;R(T v=N):n(v){}};int main(){R<int,3>r;return r.n;}',
        'explicit': 'template<class T>struct R{T n;explicit R(T v):n(v){}};int main(){R<int>r(3);return r.n;}',
        'conversion': 'template<class T>struct R{T n;R(T v):n(v){}};int main(){R<int>r=3;return r.n;}',
        'copy': 'template<class T>struct R{T n;R(T v):n(v){}R(const R&r):n(r.n+1){}};int main(){R<int>a(3);R<int>b=a;return b.n;}',
        'move': 'template<class T>struct R{T n;R(T v):n(v){}R(R&&r):n(r.n){r.n=-1;}};int main(){R<int>a(3);R<int>b(static_cast<R<int>&&>(a));return b.n;}',
        'lazy-extra-copy-default': 'template<class T>struct R{T n;R(T v):n(v){}R(const R&r,int v=T::missing):n(r.n+v){}};int main(){R<int>a(3);R<int>b(a,4);return b.n;}',
        'copy-default': 'template<class T,int N>struct R{T n;R(T v):n(v){}R(const R&r,int v=N):n(r.n+v){}};int main(){R<int,2>a(3);R<int,2>b=a;return b.n;}',
        'lazy-body': 'template<class T>struct R{T n;R(){n=T::missing;}};int read(R<int>&r){return r.n;}',
        'lazy-initializer': 'template<class T>struct R{T n;R():n(T::missing){}};int read(R<int>&r){return r.n;}',
        'lazy-default': 'template<class T>struct R{T n;R(T v=T::missing):n(v){}};int main(){R<int>r(3);return r.n;}',
        'lazy-missing': 'template<class T>struct R{T n;R();};int read(R<int>&r){return r.n;}',
        'overload-candidate': 'template<class T>struct R{T n;R(T v):n(v){}R(T*);};int main(){R<int>r(3);return r.n;}',
        'out-of-line': 'template<class T>struct R{T n;R(T);};template<class T>R<T>::R(T v):n(v){}int main(){R<int>r(3);return r.n;}',
        'out-of-line-int-spelling': 'template<int N>struct R{int n;R();};template<decltype(1) N>R<N>::R():n(N){}int main(){R<3>r;return r.n;}',
        'out-of-line-size-spelling': 'template<decltype(sizeof(int)) N>struct R{int n;R();};template<decltype(sizeof(int)) N>R<N>::R():n(static_cast<int>(N)){}int main(){R<3>r;return r.n;}',
        'explicit-class': 'template<class T>struct R{T n;R(T v):n(v){}};template struct R<int>;',
        'explicit-member': 'template<class T>struct R{T n;R(T v):n(v){}};template R<int>::R(int);',
        'explicit-specialization': 'template<class T>struct R{T n;R(T v):n(v){}};template<>R<int>::R(int v):n(v+1){}int main(){R<int>r(3);return r.n;}',
        'full-class-specialization': 'template<class T>struct R{T n;};template<>struct R<int>{int n;R(int v):n(v){}};int main(){R<int>r(3);return r.n;}',
        'constexpr-noexcept': 'template<class T>struct R{T n;constexpr R(T v)noexcept:n(v){}};static_assert(R<int>(3).n==3);int main(){R<int>r(4);return r.n;}',
        'implicit-field-initializer': 'struct I{int n;I():n(3){}};template<class T>struct R{I value;T n=4;R(){}};int main(){R<int>r;return r.value.n+r.n;}',
        'field-array': 'struct I{int n;I():n(3){}};template<class T>struct R{I a[2];T n;R(T v):n(v){}};int main(){R<int>r(4);return r.a[1].n+r.n;}',
        'array-filler': 'template<class T>struct R{T n;R():n(3){}R(T v):n(v){}};int main(){R<int>r[2]={R<int>(4)};return r[1].n;}',
        'early-return': 'template<class T>struct R{T n;R(T v):n(v){if(v)return;n=3;}};int main(){R<int>r(4);return r.n;}',
        'private-inside': 'template<class T>struct R{T n;static R make(T v){return R(v);}private:R(T v):n(v){}};int main(){R<int>r=R<int>::make(3);return r.n;}',
        'namespace-import': 'namespace N{template<class T>struct R{T n;R(T v):n(v){}};}using N::R;int main(){R<int>r(3);return r.n;}',
        'private-field': 'template<class T>class R{T n;};int main(){R<int>r;return sizeof(r);}',
        'private-constructor-field': 'template<class T>class R{T n;public:R(T v):n(v){}T get()const{return n;}};int main(){R<int>r(3);return r.get();}',
        'protected-constructor-field': 'template<class T>class R{protected:T n;public:R(T v):n(v){}T get()const{return n;}};int main(){R<int>r(3);return r.get();}',
    }
    for name, source in class_constructors_positive.items():
        check("v2-class-constructors-positive-" + name, source, profile="cpp-core-v2")
    class_constructors_reject = {
        'floating-initializer': 'template<class T>struct R{T n;R():n(static_cast<int>(1.0)){}};int main(){R<int>r;return r.n;}',
        'floating-body': 'template<class T>struct R{T n;R():n(3){double v=1.0;}};int main(){R<int>r;return r.n;}',
        'floating-default': 'template<class T>struct R{T n;R(T v=static_cast<int>(1.0)):n(v){}};int main(){R<int>r;return r.n;}',
        'floating-noexcept': 'template<class T>struct R{T n;R()noexcept(1.0>0.0):n(3){}};int main(){R<int>r;return r.n;}',
        'forced-initializer': 'template<class T>struct R{T n;R():n(static_cast<int>(1.0)){}};template struct R<int>;',
        'out-of-line-floating-type': 'template<int N>struct R{int n;R();};template<decltype(static_cast<int>(1.0)) N>R<N>::R():n(N){}',
        'out-of-line-floating-size': 'template<decltype(sizeof(int)) N>struct R{int n;R();};template<decltype(sizeof(double)) N>R<N>::R():n(static_cast<int>(N)){}',
        'attribute': 'template<class T>struct R{T n;[[deprecated]]R(T v):n(v){}};',
        'parameter-attribute': 'template<class T>struct R{T n;R([[maybe_unused]]T v):n(v){}};',
        'delegating': 'template<class T>struct R{T n;R():R(3){}R(T v):n(v){}};',
        'own-template': 'template<class T>struct R{T n;template<class U>R(U v):n(v){}};',
        'static-data': 'template<class T>struct R{T n;static int v;R(T x):n(x){}};',
        'base': 'struct I{int n;};template<class T>struct R:I{R(){}};',
        'reference-field': 'template<class T>struct R{T&n;R(T&v):n(v){}};int main(){int n=3;R<int>r(n);return r.n;}',
        'const-field': 'template<class T>struct R{const T n;R(T v):n(v){}};int main(){R<int>r(3);return r.n;}',
        'floating-field': 'template<class T>struct R{double n;R(T v):n(v){}};int main(){R<int>r(3);return 0;}',
        'mixed-access-layout': 'template<class T>class R{T n;public:T m;R(T v):n(v),m(v){}T get(){return n;}};int main(){R<int>r(3);return r.get();}',
    }
    for name, source in class_constructors_reject.items():
        check("v2-class-constructors-reject-" + name, source, 'TR0201', profile="cpp-core-v2")
    class_constructors_invalid = {
        'private': 'template<class T>struct R{T n;private:R(T v):n(v){}};int main(){R<int>r(3);return r.n;}',
        'move-const': 'template<class T>struct R{T n;R(T v):n(v){}R(R&&r):n(r.n){}};int main(){const R<int>a(3);R<int>b(static_cast<const R<int>&&>(a));return b.n;}',
        'copy-nonconst': 'template<class T>struct R{T n;R(T v):n(v){}R(R&r):n(r.n){}};int main(){const R<int>a(3);R<int>b(a);return b.n;}',
        'selected-body': 'template<class T>struct R{T n;R(){n=T::missing;}};int main(){R<int>r;return r.n;}',
        'selected-initializer': 'template<class T>struct R{T n;R():n(T::missing){}};int main(){R<int>r;return r.n;}',
        'selected-default': 'template<class T>struct R{T n;R(T v=T::missing):n(v){}};int main(){R<int>r;return r.n;}',
        'selected-copy-default': 'template<class T>struct R{T n;R(T v):n(v){}R(const R&r,int v=T::missing):n(r.n+v){}};int main(){R<int>a(3);R<int>b=a;return b.n;}',
        'missing-argument': 'template<class T>struct R{T n;R(T v):n(v){}};int main(){R<int>r;return r.n;}',
        'bad-member': 'template<class T>struct R{T n;R():missing(3){}};int main(){R<int>r;return r.n;}',
        'late-specialization': 'template<class T>struct R{T n;R(T v):n(v){}};int main(){R<int>r(3);return r.n;}template<>R<int>::R(int v):n(v+1){}',
        'explicit-missing-definition': 'template<class T>struct R{T n;R(T);};template R<int>::R(int);',
        'private-field-access': 'template<class T>class R{T n;public:R(T v):n(v){}};int main(){R<int>r(3);return r.n;}',
        'protected-field-access': 'template<class T>class R{protected:T n;public:R(T v):n(v){}};int main(){R<int>r(3);return r.n;}',
    }
    for name, source in class_constructors_invalid.items():
        check("v2-class-constructors-invalid-" + name, source, 'TR0202', profile="cpp-core-v2")
    class_constructors_missing = {
        'ordinary': 'template<class T>struct R{T n;R(T);};int main(){R<int>r(3);return r.n;}',
        'copy': 'template<class T>struct R{T n;R(T v):n(v){}R(const R&);};int main(){R<int>a(3);R<int>b=a;return b.n;}',
        'move': 'template<class T>struct R{T n;R(T v):n(v){}R(R&&);};int main(){R<int>a(3);R<int>b(static_cast<R<int>&&>(a));return b.n;}',
        'sizeof': 'template<class T>struct R{T n;R(T v):n(v){}};int main(){return sizeof(R<int>(3));}',
        'noexcept': 'template<class T>struct R{T n;R(T v)noexcept:n(v){}};int main(){return noexcept(R<int>(3));}',
        'explicit-extern': 'template<class T>struct R{T n;R(T);};extern template R<int>::R(int);',
        'explicit-specialization': 'template<class T>struct R{T n;R(T v):n(v){}};template<>R<int>::R(int);',
    }
    for name, source in class_constructors_missing.items():
        check("v2-class-constructors-missing-" + name, source, 'TR0203', profile="cpp-core-v2")
    check("v1-class-constructor", "template<class T>struct R{T n;R(T v):n(v){}};int main(){R<int>r(3);return r.n;}", "TR0201")

    class_methods_source = """struct Guard{int n;Guard(int v):n(v){}~Guard(){n=99;}};
template<class T,int N>struct Box{
 T n;
 T get(T v=N)const{return n+v;}
 void set(T v){n=v;}
 T&ref()&{return n;}
 const T&ref()const&{return n;}
 int category()&{return 1;}
 int category()&&{return 2;}
 static T twice(T v){return v+v;}
 static Box make(T v){return Box{v};}
 auto local()const{struct Local{T n;};return Local{n};}
 int cleanup(){Guard g(N);return g.n;}
};
template<auto N>struct Counter{static int&state(){static int n=N;return n;}};
template<auto N>struct OtherCounter{static int&state(){static int n=N;return n;}};
template<class T,int N>struct Fixed{T a[N];T*begin(){return a;}T*end(){return a+N;}};
int getInt(const Box<int,3>&v){return v.get();}
int getSame(const Box<int,1+2>&v){return v.get();}
unsigned int getUnsigned(const Box<unsigned int,3>&v){return v.get();}
int getFour(const Box<int,4>&v){return v.get();}
void setInt(Box<int,3>&v,int n){v.set(n);}
int&refInt(Box<int,3>&v){return v.ref();}
const int&refConst(const Box<int,3>&v){return v.ref();}
int lvalue(Box<int,3>&v){return v.category();}
int rvalue(Box<int,3>&v){return static_cast<Box<int,3>&&>(v).category();}
int twiceInt(int n){return Box<int,3>::twice(n);}
Box<int,3> makeInt(int n){return Box<int,3>::make(n);}
int localInt(const Box<int,3>&v){auto r=v.local();return r.n;}
unsigned int localUnsigned(const Box<unsigned int,3>&v){auto r=v.local();return r.n;}
int cleanupInt(Box<int,3>&v){return v.cleanup();}
int&firstState(){return Counter<3>::state();}
int&sameState(){return Counter<1+2>::state();}
int&nextState(){return Counter<4>::state();}
int&unsignedState(){return Counter<3u>::state();}
int&otherState(){return OtherCounter<3>::state();}
int sumRange(Fixed<int,3>&v){int n=0;for(int&x:v){++x;n+=x;}return n;}
int earlyRange(){Fixed<Guard,2>v{{Guard(1),Guard(2)}};for(Guard&x:v)return x.n;return 0;}
"""
    class_methods = check("v2-class-methods-protocol", class_methods_source, profile="cpp-core-v2")
    cm_records = {r["id"]: r for r in class_methods["records"]}
    cm_functions = {f["name"]: f for f in class_methods["functions"]}
    cm_globals = {g["name"]: g for g in class_methods["globals"]}
    assert len(cm_records) == len(class_methods["records"])
    assert len(cm_functions) == len(class_methods["functions"])
    assert len(cm_globals) == len(class_methods["globals"])

    def cm_line(prefix):
        lines = [i for i, line in enumerate(class_methods_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(lines) == 1, (prefix, lines)
        return lines[0]

    def cm_function(prefix):
        found = [f for f in class_methods["functions"] if f["loc"]["line"] == cm_line(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0]

    def cm_selected(prefix):
        calls = gc_calls(cm_function(prefix))
        assert len(calls) == 1, (prefix, calls)
        return cm_functions[calls[0]["callee"]]

    def cm_signature(function, result, params):
        assert function["result"] == result and [p["type"] for p in function["params"]] == params, function

    getter = cm_selected("int getInt(")
    assert getter["name"] == cm_selected("int getSame(")["name"]
    unsigned_getter = cm_selected("unsigned int getUnsigned(")
    four_getter = cm_selected("int getFour(")
    assert len({f["name"] for f in (getter, unsigned_getter, four_getter)}) == 3
    instances = []
    for prefix, function, value_type, default in (("int getInt(", getter, "int", 3),
                                                 ("unsigned int getUnsigned(", unsigned_getter, "uint", 3),
                                                 ("int getFour(", four_getter, "int", 4)):
        caller = cm_function(prefix)
        receiver = caller["params"][0]["type"]
        assert receiver.startswith("cptr:")
        record = cm_records[receiver[5:]]
        instances.append(record)
        assert [f["type"] for f in record["fields"]] == [value_type]
        cm_signature(function, value_type, [receiver, value_type])
        assert function["loc"]["line"] == cm_line(" T get(")
        call = gc_calls(caller)[0]
        assert np_pointer(caller, call["args"][0]) == ("parameter", caller["params"][0]["name"])
        assert gc_identity(caller, call["args"][1]) == default
        accesses = [n for n in walk(function["body"]) if n.get("kind") == "member"]
        assert accesses and all(n["name"] == record["fields"][0]["name"] for n in accesses)
    assert len({r["id"] for r in instances}) == 3
    assert len({r["fields"][0]["name"] for r in instances}) == 3
    record = instances[0]
    rid = record["id"]
    member = record["fields"][0]["name"]
    setter = cm_selected("void setInt(")
    cm_signature(setter, "void", ["ptr:"+rid, "int"])
    writes = [n for n in setter["body"] if n["op"] == "assign" and n["target"].get("kind") == "member"]
    assert len(writes) == 1 and writes[0]["target"]["name"] == member
    assert gc_identity(setter, writes[0]["value"]) == ("parameter", setter["params"][1]["name"])
    for prefix, result, receiver in (("int&refInt(", "ptr:int", "ptr:"+rid),
                                     ("const int&refConst(", "cptr:int", "cptr:"+rid)):
        selected = cm_selected(prefix)
        cm_signature(selected, result, [receiver])
        returned = [n["value"] for n in selected["body"] if n["op"] == "return"]
        assert len(returned) == 1
        assert np_pointer(selected, returned[0]) == ("field", ("parameter", selected["params"][0]["name"]), member)
    left, right = cm_selected("int lvalue("), cm_selected("int rvalue(")
    assert left["name"] != right["name"]
    for selected, value in ((left, 1), (right, 2)):
        cm_signature(selected, "int", ["ptr:"+rid])
        assert [gc_identity(selected, n["value"]) for n in selected["body"] if n["op"] == "return"] == [value]
    cm_signature(cm_selected("int twiceInt("), "int", ["int"])
    factory = cm_selected("Box<int,3> makeInt(")
    cm_signature(factory, "void", ["ptr:"+rid, "int"])
    factory_caller = cm_function("Box<int,3> makeInt(")
    cm_signature(factory_caller, "void", ["ptr:"+rid, "int"])
    factory_call = gc_calls(factory_caller)[0]
    assert np_pointer(factory_caller, factory_call["args"][0]) == ("parameter", factory_caller["params"][0]["name"])
    locals_by_instance = []
    for prefix, owner, scalar in (("int localInt(", instances[0], "int"),
                                  ("unsigned int localUnsigned(", instances[1], "uint")):
        selected = cm_selected(prefix)
        assert selected["result"] == "void" and len(selected["params"]) == 2
        local_id = selected["params"][0]["type"][4:]
        local_record = cm_records[local_id]
        cm_signature(selected, "void", ["ptr:"+local_id, "cptr:"+owner["id"]])
        assert [f["type"] for f in local_record["fields"]] == [scalar]
        locals_by_instance.append(local_record)
    assert locals_by_instance[0]["id"] != locals_by_instance[1]["id"]
    assert locals_by_instance[0]["fields"][0]["name"] != locals_by_instance[1]["fields"][0]["name"]
    assert cm_selected("int&firstState(")["name"] == cm_selected("int&sameState(")["name"]
    storage = []
    for prefix in ("int&firstState(", "int&nextState(", "int&unsignedState(", "int&otherState("):
        selected = cm_selected(prefix)
        cm_signature(selected, "ptr:int", [])
        names = {n["name"] for n in walk(selected["body"]) if n.get("kind") == "var" and n.get("name") in cm_globals}
        assert len(names) == 1, (selected, names)
        storage.append(cm_globals[next(iter(names))])
    assert len(cm_globals) == len({g["name"] for g in storage}) == 4
    assert all(g["type"] == "int" and g["mutable"] and g["value"]["kind"] == "literal" for g in storage)
    assert [int(g["value"]["value"]) for g in storage] == [3, 4, 3, 3]
    guard = next(r for r in class_methods["records"] if r["loc"]["line"] == cm_line("struct Guard{"))
    cleanup = cm_selected("int cleanupInt(")
    calls = gc_calls(cleanup)
    assert len(calls) == 2 and calls[1]["callee"] == guard["id"]+"_destroy"
    assert gc_identity(cleanup, calls[0]["args"][1]) == 3
    assert np_pointer(cleanup, calls[0]["args"][0]) == np_pointer(cleanup, calls[1]["args"][0])
    returned = next(n["value"] for n in cleanup["body"] if n["op"] == "return")
    captures = [i for i, n in enumerate(cleanup["body"]) if n["op"] == "assign" and n["target"].get("name") == returned.get("name")]
    assert len(captures) == 1 and captures[0] < cleanup["body"].index(calls[1])
    range_function = cm_function("int sumRange(")
    range_calls = gc_calls(range_function)
    assert len(range_calls) == 2 and range_calls[0]["callee"] != range_calls[1]["callee"]
    range_receiver = range_function["params"][0]
    range_record = cm_records[range_receiver["type"][4:]]
    assert [f["type"] for f in range_record["fields"]] == ["arr:3:int"]
    for call in range_calls:
        cm_signature(cm_functions[call["callee"]], "ptr:int", [range_receiver["type"]])
        assert np_pointer(range_function, call["args"][0]) == ("parameter", range_receiver["name"])
    # Mutation through a range reference must retain an actual dereferenced store.
    assert any(n["op"] == "assign" and n["target"].get("kind") == "dereference" for n in range_function["body"])
    early = cm_function("int earlyRange(")
    early_calls = gc_calls(early)
    owner = next(r for r in class_methods["records"] if [f["type"] for f in r["fields"]] == ["arr:2:"+guard["id"]])
    destructor = cm_functions[owner["id"]+"_destroy"]
    destroys = gc_calls(destructor)
    assert len(destroys) == 2 and all(c["callee"] == guard["id"]+"_destroy" for c in destroys)
    base = ("field", ("parameter", destructor["params"][0]["name"]), owner["fields"][0]["name"])
    assert [np_pointer(destructor, c["args"][0]) for c in destroys] == [("element", base, 1), ("element", base, 0)]
    assert sum(cm_functions[c["callee"]]["result"] == "ptr:"+guard["id"] for c in early_calls) == 2
    returned = [n["value"] for n in early["body"] if n["op"] == "return"]
    captured = [i for i, n in enumerate(early["body"]) if n["op"] == "assign" and any(v.get("kind") == "member" for v in walk(n["value"]))
                and any(v.get("name") == n["target"].get("name") for v in returned)]
    assert len(captured) == 1
    assert any(i > captured[0] and n["op"] == "call" and n["callee"] == destructor["name"] for i, n in enumerate(early["body"]))
    for function in class_methods["functions"]:
        for call in gc_calls(function):
            assert call["callee"] in cm_functions
            assert [a["type"] for a in call["args"]] == [p["type"] for p in cm_functions[call["callee"]]["params"]]
    with tempfile.TemporaryDirectory(prefix="neverc-class-methods-relocated-") as temp:
        relocated = check("v2-class-methods-relocated", class_methods_source,
                          root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == class_methods
    class_methods_positive = {
        'member-function': 'template<class T>struct R{T n;T get(){return n;}};',
        'explicit-specialization-member': 'template<class T>struct R{T n;};template<>struct R<int>{int n;int f(){return n;}};',
        'template-range': 'template<class T>struct R{T a[1];T*begin(){return a;}T*end(){return a+1;}};void f(){for(int v:R<int>{{1}}){}}',
        'getter': 'template<class T>struct R{T n;T get()const{return n;}};int main(){R<int>r{3};return r.get();}',
        'const-overload': 'template<class T>struct R{T n;T&get(){return n;}const T&get()const{return n;}};int main(){R<int>r{3};const R<int>&c=r;return r.get()+c.get();}',
        'ref-overload': 'template<class T>struct R{T n;int f()&{return 1;}int f()&&{return 2;}};int main(){R<int>r{3};return r.f()+static_cast<R<int>&&>(r).f();}',
        'static-factory': 'template<class T>struct R{T n;static R make(T v){return R{v};}};int main(){R<int>r=R<int>::make(3);return r.n;}',
        'by-value': 'template<class T>struct R{T n;T read(R v)const{return v.n;}R copy()const{return R{n};}};int main(){R<int>r{3};return r.read(r.copy());}',
        'free-template-call': 'template<class T>T twice(T v){return v+v;}template<class T>struct R{T n;T f(){return twice(n);}};int main(){R<int>r{3};return r.f();}',
        'lazy-body': 'template<class T>struct R{T n;int unused(){return T::missing;}};int main(){R<int>r{3};return r.n;}',
        'lazy-auto': 'template<class T>struct R{T n;auto unused(){return T::missing;}};int main(){R<int>r{3};return r.n;}',
        'lazy-default': 'template<class T>struct R{T f(T v=T::missing){return v;}};int main(){R<int>r{};return r.f(3);}',
        'lazy-missing': 'template<class T>struct R{T n;T missing();};int main(){R<int>r{3};return r.n;}',
        'lazy-floating': 'template<class T>struct R{T n;int unused(){return static_cast<int>(1.0);}};int main(){R<int>r{3};return r.n;}',
        'out-of-line': 'template<class T>struct R{T n;T f()const;};template<class T>T R<T>::f()const{return n;}int main(){R<int>r{3};return r.f();}',
        'out-of-line-integer-spelling': 'template<int N>struct R{int f();};template<decltype(1) N>int R<N>::f(){return N;}int main(){R<3>r{};return r.f();}',
        'out-of-line-size-spelling': 'template<decltype(sizeof(int)) N>struct R{int f();};template<decltype(sizeof(int)) N>int R<N>::f(){return static_cast<int>(N);}int main(){R<3>r{};return r.f();}',
        'explicit-class-instantiation': 'template<class T>struct R{T f(){return 3;}};template struct R<int>;',
        'explicit-class-visible-only': 'template<class T>struct R{T n;T missing();T f(){return n;}};template struct R<int>;int main(){R<int>r{3};return r.f();}',
        'explicit-method-instantiation': 'template<class T>struct R{T f(){return 3;}};template int R<int>::f();',
        'explicit-method-specialization': 'template<class T>struct R{T f(){return 1;}};template<>int R<int>::f(){return 3;}int main(){R<int>r{};return r.f();}',
        'explicit-method-forward': 'template<class T>struct R{T f(){return 1;}};template<>int R<int>::f();template<>int R<int>::f(){return 3;}int main(){R<int>r{};return r.f();}',
        'namespace-import': 'namespace N{template<class T>struct R{T n;T f(){return n;}};}using N::R;int main(){R<int>r{3};return r.f();}',
        'scalar-default': 'template<int N>struct R{int f(int v=N){return v;}};int main(){R<3>r{};return r.f();}',
        'scalar-noexcept': 'template<int N>struct R{int f()noexcept(N>0){return N;}};int main(){R<3>r{};return r.f();}',
        'constant-method': 'template<class T>struct R{T n;constexpr T f()const noexcept{return n;}};static_assert(R<int>{3}.f()==3);int main(){return R<int>{4}.f();}',
        'static-local': 'template<auto N>struct R{static int&f(){static int n=N;return n;}};int main(){return R<3>::f()+R<3u>::f();}',
        'local-record': 'template<class T>struct R{T n;auto f(){struct L{T n;};return L{n};}};int main(){R<int>r{3};auto v=r.f();return v.n;}',
        'signature-candidate': 'template<class T>struct R{int f(int v){return v;}int f(T*);};int main(){R<unsigned int>r{};return r.f(3);}',
        'private-inside': 'template<class T>struct R{T n;T f(){return inner();}private:T inner(){return n;}};int main(){R<int>r{3};return r.f();}',
        'full-specialization-call': 'template<class T>struct R{T n;};template<>struct R<int>{int n;int f(){return n;}};int main(){R<int>r{3};return r.f();}',
        'overload-selected-default': 'template<class T>struct R{T f(T v=3){return v;}T f(T v,T w){return v+w;}};int main(){R<int>r{};return r.f()+r.f(1,2);}',
    }
    for name, source in class_methods_positive.items():
        check("v2-class-methods-positive-" + name, source, profile="cpp-core-v2")
    class_methods_reject = {
        'used-floating-body': 'template<class T>struct R{int f(){return static_cast<int>(1.0);}};int main(){R<int>r{};return r.f();}',
        'floating-result': 'template<class T>struct R{double f(){return 1.0;}};int main(){R<int>r{};return static_cast<int>(r.f());}',
        'floating-default': 'template<class T>struct R{int f(int v=static_cast<int>(1.0)){return v;}};int main(){R<int>r{};return r.f();}',
        'floating-noexcept': 'template<class T>struct R{int f()noexcept(1.0>0.0){return 1;}};int main(){R<int>r{};return r.f();}',
        'forced-floating': 'template<class T>struct R{int f(){return static_cast<int>(1.0);}};template struct R<int>;',
        'out-of-line-floating-spelling': 'template<int N>struct R{int f();};template<decltype(static_cast<int>(1.0)) N>int R<N>::f(){return N;}',
        'out-of-line-floating-size': 'template<decltype(sizeof(int)) N>struct R{int f();};template<decltype(sizeof(double)) N>int R<N>::f(){return static_cast<int>(N);}',
        'method-attribute': 'template<class T>struct R{[[nodiscard]] int f(){return 1;}};',
        'parameter-attribute': 'template<class T>struct R{int f([[maybe_unused]]int v){return v;}};',
        'virtual': 'template<class T>struct R{virtual int f(){return 1;}};',
        'variadic': 'template<class T>struct R{int f(int v,...){return v;}};',
        'volatile': 'template<class T>struct R{int f()volatile{return 1;}};',
        'deleted': 'template<class T>struct R{int f()=delete;};',
        'member-template': 'template<class T>struct R{template<class U>U f(U v){return v;}};',
        'static-data': 'template<class T>struct R{static int n;int f(){return n;}};',
        'friend': 'template<class T>struct R{friend int f(R r){return 1;}};',
        'nested-record': 'template<class T>struct R{struct I{int n;};int f(){return 1;}};',
        'partial': 'template<class T>struct R{T n;};template<class T>struct R<T*>{T*n;int f(){return 1;}};',
        'bases': 'struct B{int n;};template<class T>struct R:B{int f(){return n;}};',
        'dynamic-static': 'template<class T>struct R{static int f(int v){static int n=v;return n;}};int main(){return R<int>::f(3);}',
    }
    for name, source in class_methods_reject.items():
        check("v2-class-methods-reject-" + name, source, 'TR0201', profile="cpp-core-v2")
    class_methods_invalid = {
        'private-call': 'template<class T>struct R{T n;private:T f(){return n;}};int main(){R<int>r{3};return r.f();}',
        'const-mutation': 'template<class T>struct R{T n;void f()const{++n;}};int main(){R<int>r{3};r.f();}',
        'wrong-ref-qualifier': 'template<class T>struct R{T n;T f()&{return n;}};int main(){return R<int>{3}.f();}',
        'used-invalid-body': 'template<class T>struct R{int f(){return T::missing;}};int main(){R<int>r{};return r.f();}',
        'used-invalid-auto': 'template<class T>struct R{auto f(){return T::missing;}};int main(){R<int>r{};return r.f();}',
        'used-invalid-default': 'template<class T>struct R{T f(T v=T::missing){return v;}};int main(){R<int>r{};return r.f();}',
        'bad-reference-binding': 'template<class T>struct R{T f(T&v){return v;}};int main(){R<int>r{};return r.f(3);}',
        'specialization-after-use': 'template<class T>struct R{T f(){return 1;}};int main(){R<int>r{};return r.f();}template<>int R<int>::f(){return 2;}',
        'bad-return': 'template<class T>struct R{T*f(){return 3;}};int main(){R<int>r{};return *r.f();}',
        'explicit-missing-instantiation': 'template<class T>struct R{T f();};template int R<int>::f();',
    }
    for name, source in class_methods_invalid.items():
        check("v2-class-methods-invalid-" + name, source, 'TR0202', profile="cpp-core-v2")
    class_methods_missing = {
        'used-missing': 'template<class T>struct R{T f();};int main(){R<int>r{};return r.f();}',
        'sizeof-missing': 'template<class T>struct R{T f();};int main(){R<int>r{};return sizeof(r.f());}',
        'noexcept-missing': 'template<class T>struct R{T f()noexcept;};int main(){R<int>r{};return noexcept(r.f());}',
        'explicit-specialization-missing': 'template<class T>struct R{T f(){return 1;}};template<>int R<int>::f();',
        'explicit-instantiation-declaration': 'template<class T>struct R{T f();};extern template int R<int>::f();',
        'full-specialization-missing': 'template<class T>struct R{T n;};template<>struct R<int>{int n;int f();};',
        'sizeof-uninstantiated-body': 'template<class T>struct R{T f(){return 1;}};int main(){R<int>r{};return sizeof(r.f());}',
        'noexcept-uninstantiated-body': 'template<class T>struct R{T f()noexcept{return 1;}};int main(){R<int>r{};return noexcept(r.f());}',
    }
    for name, source in class_methods_missing.items():
        check("v2-class-methods-missing-" + name, source, 'TR0203', profile="cpp-core-v2")
    check("v1-class-method", "template<class T>struct R{T n;T f(){return n;}};int main(){R<int>r{3};return r.f();}", "TR0201")

    class_templates_source = """enum class Mode:unsigned int{right=7};
template<class T>struct Box{T n;};
template<class T>struct Node{T n;Node*next;};
template<class T>struct SelfAlias{using type=SelfAlias;T n;};
template<class T>struct Other{T n;};
template<class T,int N>struct Fixed{T a[N];};
template<int N>struct Value{int n=N;};
template<auto N>struct Automatic{decltype(N) n=N;};
template<class T,class U=T>struct Pair{T first;U second;};
template<class T>struct Specialized{T n;};
template<>struct Specialized<int>;
template<>struct Specialized<int>{int first;bool second;};
using IntBox=Box<int>;
struct Guard{int n;Guard(int v):n(v){}~Guard(){n=99;}};
Box<int> makeInt(){return Box<int>{3};}
IntBox sameInt(){return IntBox{3};}
Box<unsigned int> makeUnsigned(){return Box<unsigned int>{4u};}
Other<int> otherInt(){return Other<int>{3};}
Box<Box<int>> nested(){return Box<Box<int>>{{5}};}
Fixed<int,2> makeTwo(){return Fixed<int,2>{{1,2}};}
Fixed<int,3> makeThree(){return Fixed<int,3>{{1,2,3}};}
Value<3> makeValue(){return Value<3>{};}
Value<1+2> sameValue(){return Value<1+2>{};}
Value<4> differentValue(){return Value<4>{};}
Automatic<1> signedValue(){return Automatic<1>{};}
Automatic<1u> unsignedValue(){return Automatic<1u>{};}
Automatic<Mode::right> enumValue(){return Automatic<Mode::right>{};}
Pair<int> defaultPair(){return Pair<int>{6,7};}
Specialized<int> specialized(){return Specialized<int>{8,true};}
int readValue(Box<int> value){return value.n;}
int&referenceValue(Box<int>&value){return value.n;}
void mutate(Box<int>&value,int n){value.n=n;}
int cleanup(){Box<Guard>value{Guard(4)};return value.n.n;}
void arrayCleanup(){Fixed<Guard,2>value{{Guard(1),Guard(2)}};}
int temporaryCleanup(){const Box<Guard>&value=Box<Guard>{Guard(3)};return value.n.n;}
Node<int> makeNode(){return Node<int>{3,nullptr};}
SelfAlias<int> selfInstance(){return SelfAlias<int>{4};}
SelfAlias<int>::type selfAlias(){return SelfAlias<int>{4};}
"""
    class_templates = check("v2-class-templates-protocol", class_templates_source, profile="cpp-core-v2")
    ct_records = {r["id"]: r for r in class_templates["records"]}
    ct_functions = {f["name"]: f for f in class_templates["functions"]}
    assert len(ct_records) == len(class_templates["records"])
    assert len(ct_functions) == len(class_templates["functions"])

    def ct_line(prefix):
        found = [i for i, line in enumerate(class_templates_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0]

    def ct_function(prefix):
        found = [f for f in class_templates["functions"] if f["loc"]["line"] == ct_line(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0]

    def ct_result(prefix):
        f = ct_function(prefix)
        assert f["result"] == "void" and len(f["params"]) == 1
        assert f["params"][0]["type"].startswith("ptr:")
        return ct_records[f["params"][0]["type"][4:]]

    def ct_scalar(prefix, ty, value):
        function = ct_function(prefix)
        record = ct_result(prefix)
        assert [f["type"] for f in record["fields"]] == [ty]
        field = record["fields"][0]["name"]
        writes = [n for n in function["body"] if n["op"] == "assign"
                  and n["target"].get("kind") == "member" and n["target"]["name"] == field]
        assert len(writes) == 1 and writes[0]["value"]["type"] == ty
        assert gc_identity(function, writes[0]["value"]) == value
        assert np_place(function, writes[0]["target"]) == ("field", ("parameter", function["params"][0]["name"]), field)
        return record

    int_box = ct_scalar("Box<int> makeInt(", "int", 3)
    assert ct_result("IntBox sameInt(")["id"] == int_box["id"]
    uint_box = ct_scalar("Box<unsigned int> makeUnsigned(", "uint", 4)
    other_box = ct_scalar("Other<int> otherInt(", "int", 3)
    assert len({int_box["id"], uint_box["id"], other_box["id"]}) == 3
    assert len({int_box["fields"][0]["name"], uint_box["fields"][0]["name"], other_box["fields"][0]["name"]}) == 3
    nested = ct_result("Box<Box<int>> nested(")
    assert [f["type"] for f in nested["fields"]] == [int_box["id"]]
    assert class_templates["records"].index(int_box) < class_templates["records"].index(nested)
    for prefix, size in (("Fixed<int,2> makeTwo(", 2), ("Fixed<int,3> makeThree(", 3)):
        record = ct_result(prefix)
        assert [f["type"] for f in record["fields"]] == ["arr:"+str(size)+":int"]
        assert record["layout"] == {"size_bits": size*32, "abi_align_bits": 32, "field_offsets_bits": [0]}
        function = ct_function(prefix)
        field = record["fields"][0]["name"]
        elements = [n for n in function["body"] if n["op"] == "assign" and n["target"].get("kind") == "index"]
        assert len(elements) == size
        for i, node in enumerate(elements):
            assert gc_identity(function, node["value"]) == i+1
            assert np_place(function, node["target"]) == ("element", ("field", ("parameter", function["params"][0]["name"]), field), i)
    assert ct_result("Fixed<int,2> makeTwo(")["id"] != ct_result("Fixed<int,3> makeThree(")["id"]
    three = ct_scalar("Value<3> makeValue(", "int", 3)
    assert ct_result("Value<1+2> sameValue(")["id"] == three["id"]
    assert ct_scalar("Value<4> differentValue(", "int", 4)["id"] != three["id"]
    signed = ct_scalar("Automatic<1> signedValue(", "int", 1)
    unsigned = ct_scalar("Automatic<1u> unsignedValue(", "uint", 1)
    enumerated = ct_scalar("Automatic<Mode::right> enumValue(", "uint", 7)
    assert len({signed["id"], unsigned["id"], enumerated["id"]}) == 3
    assert [f["type"] for f in ct_result("Pair<int> defaultPair(")["fields"]] == ["int", "int"]
    special = ct_result("Specialized<int> specialized(")
    assert [f["type"] for f in special["fields"]] == ["int", "bool"]
    assert special["layout"] == {"size_bits": 64, "abi_align_bits": 32, "field_offsets_bits": [0,32]}
    for prefix, result, parameters in (("int readValue(", "int", ["ptr:"+int_box["id"]]),
                                       ("int&referenceValue(", "ptr:int", ["ptr:"+int_box["id"]]),
                                       ("void mutate(", "void", ["ptr:"+int_box["id"], "int"])):
        function = ct_function(prefix)
        assert function["result"] == result and [p["type"] for p in function["params"]] == parameters
    reference = ct_function("int&referenceValue(")
    returned = [n["value"] for n in reference["body"] if n["op"] == "return"]
    assert len(returned) == 1
    assert np_pointer(reference, returned[0]) == ("field", ("parameter", reference["params"][0]["name"]), int_box["fields"][0]["name"])
    node = ct_result("Node<int> makeNode(")
    assert [f["type"] for f in node["fields"]] == ["int", "ptr:"+node["id"]]
    assert ct_scalar("SelfAlias<int> selfInstance(", "int", 4)["id"] == ct_result("SelfAlias<int>::type selfAlias(")["id"]
    guard = next(r for r in class_templates["records"] if r["loc"]["line"] == ct_line("struct Guard{"))
    for prefix, value in (("int cleanup(", 4), ("int temporaryCleanup(", 3)):
        function = ct_function(prefix)
        calls = gc_calls(function)
        assert len(calls) == 2 and gc_identity(function, calls[0]["args"][1]) == value
        owner_type = calls[1]["args"][0]["type"]
        assert owner_type.startswith("ptr:")
        owner = ct_records[owner_type[4:]]
        assert [f["type"] for f in owner["fields"]] == [guard["id"]]
        assert calls[1]["callee"] == owner["id"]+"_destroy"
        assert np_pointer(function, calls[0]["args"][0]) == ("field", np_pointer(function, calls[1]["args"][0]), owner["fields"][0]["name"])
        returned = [n["value"] for n in function["body"] if n["op"] == "return"]
        assert len(returned) == 1 and returned[0]["kind"] == "var"
        capture = [i for i, n in enumerate(function["body"]) if n["op"] == "assign" and n["target"].get("kind") == "var" and n["target"]["name"] == returned[0]["name"]]
        assert len(capture) == 1 and capture[0] < function["body"].index(calls[1])
        destructor = ct_functions[calls[1]["callee"]]
        destroys = gc_calls(destructor)
        assert len(destroys) == 1 and destroys[0]["callee"] == guard["id"]+"_destroy"
        assert np_pointer(destructor, destroys[0]["args"][0]) == ("field", ("parameter", destructor["params"][0]["name"]), owner["fields"][0]["name"])
    array_function = ct_function("void arrayCleanup(")
    array_calls = gc_calls(array_function)
    assert len(array_calls) == 3
    array_owner = ct_records[array_calls[2]["args"][0]["type"][4:]]
    assert [f["type"] for f in array_owner["fields"]] == ["arr:2:"+guard["id"]]
    assert array_calls[2]["callee"] == array_owner["id"]+"_destroy"
    array_destructor = ct_functions[array_calls[2]["callee"]]
    destroys = gc_calls(array_destructor)
    assert len(destroys) == 2 and all(c["callee"] == guard["id"]+"_destroy" for c in destroys)
    base = ("field", ("parameter", array_destructor["params"][0]["name"]), array_owner["fields"][0]["name"])
    assert [np_pointer(array_destructor, c["args"][0]) for c in destroys] == [("element", base, 1), ("element", base, 0)]
    for function in class_templates["functions"]:
        for call in gc_calls(function):
            assert call["callee"] in ct_functions
            assert [a["type"] for a in call["args"]] == [p["type"] for p in ct_functions[call["callee"]]["params"]]
    with tempfile.TemporaryDirectory(prefix="neverc-class-templates-relocated-") as temp:
        relocated = check("v2-class-templates-relocated", class_templates_source,
                          root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == class_templates
    class_templates_positive = {
        'unused': 'template<class T>struct R{T n;};',
        'integer': 'template<class T>struct R{T n;};int main(){R<int>r{3};return r.n-3;}',
        'boolean': 'template<class T>struct R{T n;};int main(){R<bool>r{true};return !r.n;}',
        'enum-type': 'enum class E:unsigned int{x=3};template<class T>struct R{T n;};int main(){R<E>r{E::x};return r.n!=E::x;}',
        'pointer-field': 'template<class T>struct R{T n;};int main(){int n=3;R<int*>r{&n};*r.n=4;return n-4;}',
        'record-field': 'struct V{int n;};template<class T>struct R{T n;};int main(){R<V>r{{3}};return r.n.n-3;}',
        'array-type': 'template<class T>struct R{T n;};int main(){R<int[2]>r{{1,2}};return r.n[0]+r.n[1]-3;}',
        'nested-value': 'template<class T>struct R{T n;};int main(){R<R<int>>r{{3}};return r.n.n-3;}',
        'fixed-extent': 'template<class T,int N>struct R{T n[N];};int main(){R<int,2>r{{1,2}};return r.n[0]+r.n[1]-3;}',
        'selected-default': 'template<int N>struct R{int n=N;};int main(){R<3>r;return r.n;}',
        'signed-value': 'template<int N>struct R{int n=N;};int main(){R<-3>r{};return r.n+3;}',
        'wide-value': 'template<unsigned long long N>struct R{unsigned long long n=N;};int main(){R<0xffffffffffffffffULL>r{};return r.n!=0xffffffffffffffffULL;}',
        'auto-value': 'template<auto N>struct R{decltype(N) n=N;};int main(){R<true>r{};return !r.n;}',
        'dependent-value': 'template<class T,T N>struct R{T n=N;};int main(){R<int,3>r{};return r.n-3;}',
        'enum-value': 'enum class E:unsigned int{x=3};template<E N>struct R{E n=N;};int main(){R<E::x>r{};return r.n!=E::x;}',
        'alias': 'template<class T>struct R{using type=T;T n;};int main(){R<int>::type n=3;R<int>r{n};return r.n-3;}',
        'enum-and-assert': 'template<int N>struct R{enum{count=N};static_assert(N>0);int n[N];};int main(){R<2>r{};return R<2>::count-2+r.n[0];}',
        'empty': 'template<class T>struct R{};int main(){R<int>r{};return sizeof(r)!=1;}',
        'public-class': 'template<class T>class R{public:T n;};int main(){R<int>r{3};return r.n-3;}',
        'type-default': 'template<class T=int>struct R{T n;};int main(){R<>r{3};return r.n-3;}',
        'dependent-type-default': 'template<class T,class U=T>struct R{T a;U b;};int main(){R<int>r{1,2};return r.a+r.b-3;}',
        'inherited-default': 'template<class T=int>struct R;template<class T>struct R{T n;};int main(){R<>r{3};return r.n-3;}',
        'forward-primary': 'template<class T>struct R;template<class T>struct R{T n;};int main(){R<int>r{3};return r.n-3;}',
        'explicit-instantiation': 'template<class T>struct R{T n;};template struct R<int>;int main(){R<int>r{3};return r.n-3;}',
        'explicit-specialization': 'template<class T>struct R{T n;};template<>struct R<int>{int n[2];};int main(){R<int>r{{1,2}};return r.n[0]+r.n[1]-3;}',
        'forward-specialization': 'template<class T>struct R{T n;};template<>struct R<int>;template<>struct R<int>{int n;};int main(){R<int>r{3};return r.n-3;}',
        'namespace-import': 'namespace N{template<class T>struct R{T n;};}using N::R;int main(){R<int>r{3};return r.n-3;}',
        'inline-namespace': 'namespace N{inline namespace V{template<class T>struct R{T n;};}}int main(){N::R<int>r{3};return r.n-3;}',
        'import-declaration': 'namespace N{template<class T>struct R{T n;};}using N::R;',
        'inline-declaration': 'namespace N{inline namespace V{template<class T>struct R{T n;};}}',
        'function-deduction': 'template<class T>struct R{T n;};template<class T>T get(R<T>r){return r.n;}int main(){R<int>r{3};return get(r)-3;}',
        'lazy-field-default': 'template<class T>struct R{int n=T::missing;};int main(){R<int>r{3};return r.n-3;}',
        'written-type': 'template<decltype(1) N>struct R{int n=N;};int main(){R<3>r{};return r.n-3;}',
        'written-sizeof': 'template<int N>struct R{int n[N];};int main(){R<sizeof(int)>r{};return sizeof(r)!=sizeof(int)*sizeof(int);}',
        'written-default': 'template<class T=decltype(1)>struct R{T n;};int main(){R<>r{3};return r.n-3;}',
        'mixed-64': 'template<class T0,int N1,class T2,int N3,class T4,int N5,class T6,int N7,class T8,int N9,class T10,int N11,class T12,int N13,class T14,int N15,class T16,int N17,class T18,int N19,class T20,int N21,class T22,int N23,class T24,int N25,class T26,int N27,class T28,int N29,class T30,int N31,class T32,int N33,class T34,int N35,class T36,int N37,class T38,int N39,class T40,int N41,class T42,int N43,class T44,int N45,class T46,int N47,class T48,int N49,class T50,int N51,class T52,int N53,class T54,int N55,class T56,int N57,class T58,int N59,class T60,int N61,class T62,int N63>struct R{int n=N1;};int main(){R<int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1>r{};return r.n-1;}',
        'self-pointer': 'template<class T>struct R{T n;R*next;};int main(){R<int>a{1,nullptr};R<int>b{2,&a};b.next->n=3;return a.n-3;}',
        'injected-alias': 'template<class T>struct R{using type=R;T n;};int main(){R<int>::type r{3};return r.n-3;}',
    }
    for name, source in class_templates_positive.items():
        check("v2-class-templates-positive-" + name, source, profile="cpp-core-v2")
    class_templates_reject = {
        'floating-field': 'template<class T>struct R{T n;};int main(){R<double>r{1.0};return 0;}',
        'floating-default': 'template<class T>struct R{int n=static_cast<int>(1.0);};int main(){R<int>r{};return r.n;}',
        'default-value': 'template<int N=3>struct R{int n;};',
        'default-value-inherited': 'template<int N=3>struct R;template<int N>struct R{int n;};',
        'pointer-value': 'int n;template<int*P>struct R{int n;};',
        'auto-pointer': 'int n;template<auto P>struct R{int n;};int main(){R<&n>r{};return r.n;}',
        'auto-null': 'template<auto P>struct R{int n;};int main(){R<nullptr>r{};return r.n;}',
        'value-pack': 'template<int...N>struct R{int n;};',
        'type-pack': 'template<class...T>struct R{int n;};',
        'template-template': 'template<template<class>class T>struct R{int n;};',
        'friend': 'template<class T>struct R{T n;friend int get(R r){return r.n;}};',
        'static-member': 'template<class T>struct R{inline static T n=1;};',
        'nested-record': 'template<class T>struct R{struct I{T n;};};',
        'nested-template': 'struct R{template<class T>struct I{T n;};};',
        'member-template': 'template<class T>struct R{template<class U>U f(U n){return n;}};',
        'alias-template': 'template<class T>using R=T;',
        'partial': 'template<class T>struct R{T n;};template<class T>struct R<T*>{T*n;};',
        'selected-partial': 'template<class T>struct R{T n;};template<class T>struct R<T*>{T*n;};int main(){int n=3;R<int*>r{&n};return *r.n;}',
        'union': 'template<class T>union R{T n;int m;};',
        'base': 'struct B{int n;};template<class T>struct R:B{T m;};',
        'bitfield': 'template<class T>struct R{unsigned int n:3;};int main(){R<int>r{};return r.n;}',
        'mutable-field': 'template<class T>struct R{mutable T n;};int main(){R<int>r{3};return r.n;}',
        'const-field': 'template<class T>struct R{const T n;};int main(){R<int>r{3};return r.n;}',
        'reference-field': 'template<class T>struct R{T&n;};int main(){int n=3;R<int>r{n};return r.n;}',
        'zero-array': 'template<int N>struct R{int n[N];};int main(){R<0>r;return 0;}',
        'oversized-array': 'template<int N>struct R{int n[N];};int main(){R<65537>r{};return r.n[0];}',
        'floating-argument': 'template<int N>struct R{int n;};int main(){R<static_cast<int>(1.0)>r{};return r.n;}',
        'floating-instantiation': 'template<int N>struct R{int n;};template struct R<static_cast<int>(1.0)>;',
        'floating-specialization': 'template<int N>struct R{int n;};template<>struct R<static_cast<int>(1.0)>{int n;};',
        'floating-parameter-type': 'template<decltype(static_cast<int>(1.0)) N>struct R{int n;};',
        'floating-default-type': 'template<class T=decltype(static_cast<int>(1.0))>struct R{T n;};int main(){R<>r{1};return r.n;}',
        'mixed-65': 'template<class T0,int N1,class T2,int N3,class T4,int N5,class T6,int N7,class T8,int N9,class T10,int N11,class T12,int N13,class T14,int N15,class T16,int N17,class T18,int N19,class T20,int N21,class T22,int N23,class T24,int N25,class T26,int N27,class T28,int N29,class T30,int N31,class T32,int N33,class T34,int N35,class T36,int N37,class T38,int N39,class T40,int N41,class T42,int N43,class T44,int N45,class T46,int N47,class T48,int N49,class T50,int N51,class T52,int N53,class T54,int N55,class T56,int N57,class T58,int N59,class T60,int N61,class T62,int N63,class T64>struct R{int n=N1;};int main(){R<int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int>r{};return r.n-1;}',
    }
    for name, source in class_templates_reject.items():
        check("v2-class-templates-reject-" + name, source, 'TR0201', profile="cpp-core-v2")
    class_templates_invalid = {
        'missing-argument': 'template<class T>struct R{T n;};int main(){R<>r{};return 0;}',
        'wrong-argument-kind': 'template<class T>struct R{T n;};int main(){R<3>r{};return 0;}',
        'nonconstant': 'template<int N>struct R{int n;};int main(){int n=3;R<n>r{};return r.n;}',
        'narrowing': 'template<unsigned char N>struct R{int n;};int main(){R<256>r{};return r.n;}',
        'invalid-dependent-type': 'template<class T>struct R{typename T::type n;};int main(){R<int>r{};return 0;}',
        'selected-invalid-default': 'template<class T>struct R{int n=T::missing;};int main(){R<int>r{};return r.n;}',
        'failed-assertion': 'template<int N>struct R{static_assert(N>0);int n;};int main(){R<0>r{};return r.n;}',
        'aggregate-arity': 'template<class T>struct R{T n;};int main(){R<int>r{1,2};return r.n;}',
        'void-field': 'template<class T>struct R{T n;};int main(){R<void>r{};return 0;}',
        'negative-array': 'template<int N>struct R{int n[N];};int main(){R<-1>r{};return 0;}',
    }
    for name, source in class_templates_invalid.items():
        check("v2-class-templates-invalid-" + name, source, 'TR0202', profile="cpp-core-v2")
    check("v1-class-type-template", "template<class T>struct R{T n;};int main(){R<int>r{3};return r.n;}", "TR0201")
    check("v1-class-value-template", "template<int N>struct R{int n=N;};int main(){R<3>r{};return r.n;}", "TR0201")

    non_type_templates_source = """enum class Mode:unsigned int{right=7};
template<int N>int value(){return N;}
template<auto N>auto automatic(){return N;}
template<class T,T N>T typed(){return N;}
template<int N>int&state(){static int n=N;return n;}
template<int N>int&otherState(){static int n=N;return n;}
template<auto N>int&autoState(){static int n=0;return n;}
template<int N>int extent(int(&a)[N]){return N;}
template<int N>int count(){if constexpr(N==0)return 0;else return N+count<N-1>();}
template<int N>int withDefault(int n=N){return n+N;}
struct Guard{int n;Guard(int v):n(v){}~Guard(){n=99;}};
struct Plain{int n;};
template<int N>int guarded(){Guard g(N);return g.n;}
template<bool B>auto choose(){if constexpr(B){Guard g(4);return g.n;}else return Plain{9};}
int three(){return value<1+2>();}
int equivalent(){return value<3>();}
int four(){return value<4>();}
int signedAuto(){return automatic<-3>();}
unsigned int unsignedAuto(){return automatic<0xffffffffu>();}
bool boolAuto(){return automatic<true>();}
Mode enumAuto(){return automatic<Mode::right>();}
long long wide(){return typed<long long,-2147483649LL>();}
int&firstState(){return state<3>();}
int&sameState(){return state<1+2>();}
int&secondState(){return state<4>();}
int&otherPrimary(){return otherState<3>();}
int&intAutoState(){return autoState<1>();}
int&uintAutoState(){return autoState<1u>();}
int extentTwo(int(&a)[2]){return extent(a);}
int extentThree(int(&a)[3]){return extent(a);}
int recursive(){return count<3>();}
int defaultValue(){return withDefault<7>();}
int cleanup(){return guarded<2>();}
int chosenInt(){return choose<true>();}
Plain chosenRecord(){return choose<false>();}
"""
    non_type_templates = check("v2-non-type-templates-protocol", non_type_templates_source, profile="cpp-core-v2")
    nt_functions = {f["name"]: f for f in non_type_templates["functions"]}
    nt_globals = {g["name"]: g for g in non_type_templates["globals"]}
    assert len(nt_functions) == len(non_type_templates["functions"])
    assert len(nt_globals) == len(non_type_templates["globals"])

    def nt_line(prefix):
        found = [i for i, line in enumerate(non_type_templates_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0]

    def nt_function(prefix):
        found = [f for f in non_type_templates["functions"] if f["loc"]["line"] == nt_line(prefix)]
        assert len(found) == 1, (prefix, found)
        return found[0]

    def nt_selected(prefix):
        calls = gc_calls(nt_function(prefix))
        assert len(calls) == 1, (prefix, calls)
        return nt_functions[calls[0]["callee"]]

    for prefix, result, value in (("int three(", "int", 3), ("int four(", "int", 4),
                                  ("int signedAuto(", "int", -3),
                                  ("unsigned int unsignedAuto(", "uint", 4294967295),
                                  ("bool boolAuto(", "bool", 1),
                                  ("Mode enumAuto(", "uint", 7),
                                  ("long long wide(", "i64", -2147483649)):
        selected = nt_selected(prefix)
        assert selected["result"] == result and not selected["params"]
        returned = [n["value"] for n in selected["body"] if n["op"] == "return"]
        assert len(returned) == 1 and returned[0]["type"] == result
        assert gc_identity(selected, returned[0]) == value
    assert nt_selected("int three(")["name"] == nt_selected("int equivalent(")["name"]
    assert nt_selected("int three(")["name"] != nt_selected("int four(")["name"]
    assert nt_selected("int&firstState(")["name"] == nt_selected("int&sameState(")["name"]
    instances = [nt_selected(p) for p in ("int&firstState(", "int&secondState(", "int&otherPrimary(",
                                         "int&intAutoState(", "int&uintAutoState(")]
    assert len({f["name"] for f in instances}) == 5
    storage = []
    for function in instances:
        assert function["result"] == "ptr:int" and not function["params"]
        names = {n["name"] for n in walk(function["body"])
                 if n.get("kind") == "var" and n.get("name") in nt_globals}
        assert len(names) == 1, (function, names)
        storage.append(nt_globals[next(iter(names))])
    assert len({g["name"] for g in storage}) == 5
    assert len(nt_globals) == 5
    assert all(g["type"] == "int" and g["mutable"] and g["value"]["kind"] == "literal" and g["value"]["type"] == "int" for g in storage)
    assert [int(g["value"]["value"]) for g in storage] == [3, 4, 3, 0, 0]
    for prefix, extent in (("int extentTwo(", 2), ("int extentThree(", 3)):
        function = nt_function(prefix)
        selected = nt_selected(prefix)
        assert [p["type"] for p in selected["params"]] == ["ptr:arr:"+str(extent)+":int"]
        assert np_pointer(function, gc_calls(function)[0]["args"][0]) == ("parameter", function["params"][0]["name"])
        assert [gc_identity(selected, n["value"]) for n in selected["body"] if n["op"] == "return"] == [extent]
    chain = []
    current = nt_selected("int recursive(")
    while True:
        assert current["name"] not in chain and len(chain) < 4
        chain.append(current["name"])
        assert current["loc"]["line"] == nt_line("template<int N>int count(")
        assert current["result"] == "int" and not current["params"]
        assert not any(n["op"] == "branch" for n in current["body"])
        calls = gc_calls(current)
        if not calls:
            assert [gc_identity(current, n["value"]) for n in current["body"] if n["op"] == "return"] == [0]
            break
        assert len(calls) == 1
        current = nt_functions[calls[0]["callee"]]
    assert len(chain) == 4
    default_function = nt_function("int defaultValue(")
    default_call = gc_calls(default_function)[0]
    assert [p["type"] for p in nt_functions[default_call["callee"]]["params"]] == ["int"]
    assert gc_identity(default_function, default_call["args"][0]) == 7
    guard = next(r for r in non_type_templates["records"] if r["loc"]["line"] == nt_line("struct Guard{"))
    plain = next(r for r in non_type_templates["records"] if r["loc"]["line"] == nt_line("struct Plain{"))
    for prefix, value in (("int cleanup(", 2), ("int chosenInt(", 4)):
        function = nt_selected(prefix)
        calls = gc_calls(function)
        assert len(calls) == 2 and calls[1]["callee"] == guard["id"]+"_destroy"
        assert gc_identity(function, calls[0]["args"][1]) == value
        assert np_pointer(function, calls[0]["args"][0]) == np_pointer(function, calls[1]["args"][0])
        returned = [n["value"] for n in function["body"] if n["op"] == "return"]
        assert len(returned) == 1 and returned[0]["kind"] == "var"
        captures = [i for i, n in enumerate(function["body"]) if n["op"] == "assign"
                    and n["target"].get("kind") == "var" and n["target"]["name"] == returned[0]["name"]]
        assert len(captures) == 1 and captures[0] < function["body"].index(calls[1])
    selected_record = nt_selected("Plain chosenRecord(")
    assert selected_record["result"] == "void"
    assert [p["type"] for p in selected_record["params"]] == ["ptr:"+plain["id"]]
    assert not gc_calls(selected_record)
    field = plain["fields"][0]["name"]
    writes = [n for n in selected_record["body"] if n["op"] == "assign"
              and n["target"].get("kind") == "member" and n["target"]["name"] == field]
    assert len(writes) == 1 and gc_identity(selected_record, writes[0]["value"]) == 9
    assert np_place(selected_record, writes[0]["target"]) == ("field", ("parameter", selected_record["params"][0]["name"]), field)
    assert not any(v["type"] == guard["id"] for v in selected_record["locals"])
    for function in non_type_templates["functions"]:
        for call in gc_calls(function):
            assert call["callee"] in nt_functions
            assert [a["type"] for a in call["args"]] == [p["type"] for p in nt_functions[call["callee"]]["params"]]
    with tempfile.TemporaryDirectory(prefix="neverc-non-type-templates-relocated-") as temp:
        relocated = check("v2-non-type-templates-relocated", non_type_templates_source,
                          root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == non_type_templates

    non_type_templates_positive = {
        'unused': 'template<int N>int f(){return N;}',
        'signed': 'template<int N>int f(){return N;}int main(){return f<-3>()+3;}',
        'bool': 'template<bool B>bool f(){return B;}int main(){return !f<true>();}',
        'enum': 'enum class E:unsigned int{x=3};template<E N>E f(){return N;}int main(){return static_cast<unsigned int>(f<E::x>())-3u;}',
        'unsigned-max': 'template<unsigned long long N>unsigned long long f(){return N;}int main(){return f<0xffffffffffffffffULL>()!=0xffffffffffffffffULL;}',
        'signed-wide': 'template<long long N>long long f(){return N;}int main(){return f<-2147483649LL>()!=-2147483649LL;}',
        'auto-int': 'template<auto N>auto f(){return N;}int main(){return f<3>()-3;}',
        'auto-bool': 'template<auto N>auto f(){return N;}int main(){return !f<true>();}',
        'auto-enum': 'enum class E:unsigned int{x=3};template<auto N>auto f(){return N;}int main(){return f<E::x>()!=E::x;}',
        'dependent-type': 'template<class T,T N>T f(){return N;}int main(){return f<int,3>()-3;}',
        'type-default': 'template<class T=int,T N>T f(){return N;}int main(){return f<int,3>()-3;}',
        'mixed-order': 'template<int N,class T>T f(T v){return v+N;}int main(){return f<3>(2)-5;}',
        'array-deduction': 'template<int N>int f(int(&a)[N]){return N+a[N-1];}int main(){int a[3]={1,2,3};return f(a)-6;}',
        'array-size-type': 'template<decltype(sizeof(int)) N>int f(int(&a)[N]){return N+a[N-1];}int main(){int a[2]={1,2};return f(a)-4;}',
        'written-decltype': 'template<decltype(1) N>int f(){return N;}int main(){return f<3>()-3;}',
        'written-sizeof': 'template<int N>int f(){return N;}int main(){return f<sizeof(int)>()-sizeof(int);}',
        'explicit-instantiation': 'template<int N>int f(){return N;}template int f<3>();int main(){return f<3>()-3;}',
        'explicit-specialization': 'template<int N>int f(){return N;}template<>int f<3>(){return 7;}int main(){return f<3>()-7;}',
        'redeclared': 'template<int N>int f();template<int N>int f(){return N;}int main(){return f<3>()-3;}',
        'ordinary-overload': 'int f(int n){return n;}template<int N>int f(){return N;}int main(){return f(2)+f<3>()-5;}',
        'namespace-import': 'namespace A{template<int N>int f(){return N;}}using A::f;int main(){return f<3>()-3;}',
        'recursive': 'template<int N>int f(){if constexpr(N==0)return 0;else return N+f<N-1>();}int main(){return f<4>()-10;}',
        'function-default': 'template<int N>int f(int n=N){return n;}int main(){return f<3>()-3;}',
        'lazy-function-default': 'template<class T,int N>int f(int n=T::missing+N){return n;}int main(){return f<int,3>(4)-4;}',
        'static-state': 'template<int N>int&f(){static int n=N;return n;}int main(){++f<3>();return f<3>()-4;}',
        'static-constant': 'template<int N>int f(){static const int n=N;return n;}int main(){return f<3>()-3;}',
        'case-label': 'template<int N>int f(int n){switch(n){case N:return 3;default:return 1;}}int main(){return f<2>(2)-3;}',
        'fixed-local-array': 'template<int N>int f(){int a[N]={};a[N-1]=N;return a[N-1];}int main(){return f<3>()-3;}',
        'constexpr-declaration': 'template<int N>constexpr int f(){return N;}static_assert(f<3>()==3);int main(){return f<3>()-3;}',
        'bool-branch': 'template<bool B>int f(){if constexpr(B)return 3;else return 4;}int main(){return f<true>()+f<false>()-7;}',
        'discarded-dependent-body': 'template<int N>int f(){if constexpr(N==0)return 3;else{double d=N;return static_cast<int>(d);}}int main(){return f<0>()-3;}',
        'mixed-64': 'template<class T0,int N1,class T2,int N3,class T4,int N5,class T6,int N7,class T8,int N9,class T10,int N11,class T12,int N13,class T14,int N15,class T16,int N17,class T18,int N19,class T20,int N21,class T22,int N23,class T24,int N25,class T26,int N27,class T28,int N29,class T30,int N31,class T32,int N33,class T34,int N35,class T36,int N37,class T38,int N39,class T40,int N41,class T42,int N43,class T44,int N45,class T46,int N47,class T48,int N49,class T50,int N51,class T52,int N53,class T54,int N55,class T56,int N57,class T58,int N59,class T60,int N61,class T62,int N63>int f(){return N1;}int main(){return f<int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1>()-1;}',
    }
    for name, source in non_type_templates_positive.items():
        check("v2-non-type-templates-positive-" + name, source, profile="cpp-core-v2")
    non_type_templates_reject = {
        'zero-array': 'template<int N>int f(){int a[N];return 0;}int main(){return f<0>();}',
        'default-value': 'template<int N=3>int f(){return N;}int main(){return f<>();}',
        'default-unused': 'template<int N=3>int f(){return N;}',
        'default-inherited': 'template<int N=3>int f();template<int N>int f(){return N;}int main(){return f<3>();}',
        'value-pack': 'template<int... N>int f(){return sizeof...(N);}int main(){return f<1,2>();}',
        'template-template': 'template<template<class>class T,int N>int f(){return N;}',
        'pointer-parameter': 'int n=3;template<int*P>int f(){return *P;}int main(){return f<&n>();}',
        'reference-parameter': 'int n=3;template<int&N>int f(){return N;}int main(){return f<n>();}',
        'function-parameter': 'int g(){return 3;}template<int(*F)()>int f(){return F();}int main(){return f<g>();}',
        'member-pointer': 'struct R{int n;};template<int R::*P>int f(){return 1;}int main(){return f<&R::n>();}',
        'auto-pointer': 'int n=3;template<auto N>int f(){return 1;}int main(){return f<&n>();}',
        'auto-null': 'template<auto N>int f(){return 1;}int main(){return f<nullptr>();}',
        'dependent-pointer': 'int n=3;template<class T,T N>int f(){return 1;}int main(){return f<int*,&n>();}',
        'selected-floating-body': 'template<int N>int f(){double d=N;return static_cast<int>(d);}int main(){return f<3>();}',
        'selected-floating-default': 'template<int N>int f(int n=static_cast<int>(1.0)+N){return n;}int main(){return f<3>();}',
        'argument-floating-cast': 'template<int N>int f(){return N;}int main(){return f<static_cast<int>(1.0)>();}',
        'instantiation-floating-cast': 'template<int N>int f(){return N;}template int f<static_cast<int>(1.0)>();',
        'specialization-floating-cast': 'template<int N>int f(){return N;}template<>int f<static_cast<int>(1.0)>(){return 1;}',
        'parameter-decltype-float': 'template<decltype(static_cast<int>(1.0)) N>int f(){return N;}int main(){return f<1>();}',
        'parameter-decltype-sizeof-float': 'template<decltype(sizeof(double)) N>int f(){return N;}int main(){return f<1>();}',
        'argument-sizeof-float': 'template<int N>int f(){return N;}int main(){return f<sizeof(double)>();}',
        'oversized-array': 'template<int N>int f(){int a[N]={};return a[0];}int main(){return f<65537>();}',
        'member-template': 'struct R{template<int N>int f(){return N;}};int main(){R r;return r.f<3>();}',
        'mixed-65': 'template<class T0,int N1,class T2,int N3,class T4,int N5,class T6,int N7,class T8,int N9,class T10,int N11,class T12,int N13,class T14,int N15,class T16,int N17,class T18,int N19,class T20,int N21,class T22,int N23,class T24,int N25,class T26,int N27,class T28,int N29,class T30,int N31,class T32,int N33,class T34,int N35,class T36,int N37,class T38,int N39,class T40,int N41,class T42,int N43,class T44,int N45,class T46,int N47,class T48,int N49,class T50,int N51,class T52,int N53,class T54,int N55,class T56,int N57,class T58,int N59,class T60,int N61,class T62,int N63,class T64>int f(){return N1;}int main(){return f<int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int,1,int>()-1;}',
    }
    for name, source in non_type_templates_reject.items():
        check("v2-non-type-templates-reject-" + name, source, 'TR0201', profile="cpp-core-v2")
    non_type_templates_invalid = {
        'nonconstant': 'template<int N>int f(){return N;}int main(){int n=3;return f<n>();}',
        'narrowing': 'template<unsigned char N>int f(){return N;}int main(){return f<256>();}',
        'deduction-mismatch': 'template<int N>int f(int(&a)[N]){return N;}int main(){int a[2]={};return f<3>(a);}',
        'negative-array': 'template<int N>int f(){int a[N];return 1;}int main(){return f<-1>();}',
        'missing-value-argument': 'template<int N>int f(){return N;}int main(){return f<>();}',
        'floating-parameter': 'template<double N>int f(){return 1;}',
        'class-parameter': 'struct R{int n;};template<R N>int f(){return 1;}',
        'floating-argument': 'template<int N>int f(){return N;}int main(){return f<1.0>();}',
    }
    for name, source in non_type_templates_invalid.items():
        check("v2-non-type-templates-invalid-" + name, source, 'TR0202', profile="cpp-core-v2")
    non_type_templates_missing = {
        'selected': 'template<int N>int f();int main(){return f<3>();}',
        'unevaluated-size': 'template<int N>int f();int main(){return sizeof(f<3>());}',
        'unevaluated-noexcept': 'template<int N>int f()noexcept;int main(){return noexcept(f<3>());}',
        'explicit-extern': 'template<int N>int f();extern template int f<3>();',
    }
    for name, source in non_type_templates_missing.items():
        check("v2-non-type-templates-missing-" + name, source, 'TR0203', profile="cpp-core-v2")
    check("v1-non-type-template", "template<int N>int f(){return N;}int main(){return f<3>();}", "TR0201")
    check("v1-auto-value-template", "template<auto N>auto f(){return N;}int main(){return f<3>();}", "TR0201")

    function_templates_positive = {
        'deduced': 'template<class T>T id(T v){return v;}int main(){return id(3)-3;}',
        'explicit': 'template<class T>T id(T v){return v;}int main(){return id<int>(3)-3;}',
        'default-type': 'template<class T=int>T zero(){return T{};}int main(){return zero();}',
        'default-redecl': 'template<class T=int>T zero();template<class U>U zero(){return U{};}int main(){return zero();}',
        'default-dependent-type': 'template<class T,class U=T>U convert(T v){return v;}int main(){return convert(3)-3;}',
        'void-type': 'template<class T>int tag(){return 3;}int main(){return tag<void>()-3;}',
        'reference-type': 'template<class T>T id(T v){return v;}int main(){int n=3;id<int&>(n)=4;return n-4;}',
        'const-pointer': 'template<class T>T* id(T*p){return p;}int main(){const int n=3;return *id(&n)-3;}',
        'record-return': 'struct R{int n;};template<class T>T id(T v){return v;}int main(){return id(R{3}).n-3;}',
        'range-array': 'template<class T>int sum(T&a){int n=0;for(auto&x:a)n+=x;return n;}int main(){int a[2]={1,2};return sum(a)-3;}',
        'constexpr': 'template<class T>constexpr T add(T n){return n+1;}static_assert(add(2)==3,"value");int main(){return add(3)-4;}',
        'constexpr-discarded': 'template<class T>int f(){if constexpr(sizeof(T)==sizeof(int))return 3;else return T::missing;}int main(){return f<int>()-3;}',
        'constexpr-return-types': 'struct R{int n;};template<class T>auto f(){if constexpr(sizeof(T)==sizeof(int))return 3;else return R{4};}int main(){return f<int>()+f<bool>().n-7;}',
        'unused-floating-pattern': 'template<class T>double f(T v){double x=1.0;return x+v;}int main(){return 0;}',
        'discarded-floating-branch': 'template<class T>int f(){if constexpr(sizeof(T)==sizeof(int))return 3;else return static_cast<int>(1.0);}int main(){return f<int>()-3;}',
        'lazy-invalid-default': 'template<class T>int f(T v=T::missing){return v;}int main(){return f(3)-3;}',
        'lazy-floating-default': 'template<class T>int f(T v=static_cast<T>(1.0)){return v;}int main(){return f(3)-3;}',
        'selected-default': 'template<class T>int f(T v=T{}){return v;}int main(){return f<int>();}',
        'specialization': 'template<class T>int f(T v){return 1;}template<>int f<int>(int v){return 3;}int main(){return f(1)-3;}',
        'specialization-forward': 'template<class T>int f(T);template<>int f<int>(int);template<>int f<int>(int v){return v;}int main(){return f(3)-3;}',
        'explicit-definition': 'template<class T>T f(T v){return v;}template int f<int>(int);',
        'extern-defined': 'template<class T>T f(T v){return v;}extern template int f<int>(int);template int f<int>(int);int main(){return f(3)-3;}',
        'candidate-without-body': 'template<class T>int f(T v){double unused=1.0;return 2;}int f(int v){return 3;}int main(){return f(1)-3;}',
        'sfinae': 'template<class T,class=typename T::tag>int f(T v){return 2;}int f(int v){return 3;}int main(){return f(1)-3;}',
        'nested-redeclare': 'template<class T>auto first(T);template<class T>auto second(T n){struct R{T n;};return R{n};}template<class T>auto first(T n){return second(n);}int main(){return first(3).n-3;}',
        'namespace-import': 'namespace N{template<class T>T f(T v){return v;}}namespace A=N;using A::f;int main(){return f(3)-3;}',
        'inline-namespace': 'namespace N{inline namespace V{template<class T>T f(T v){return v;}}}int main(){return N::f(3)-3;}',
        'static-identity': 'template<class T>int f(){static int n=0;return ++n;}int main(){int a=f<int>();int b=f<int>();int c=f<bool>();return a+b+c-4;}',
        'function-reference': 'template<class T>T&& move(T&v){return static_cast<T&&>(v);}int main(){int n=3;int&&r=move(n);r=4;return n-4;}',
        'noexcept-resolved': 'template<class T>int f(T v)noexcept(sizeof(T)==sizeof(int)){return v;}int main(){return f(3)-3;}',
        'static-template-pattern': 'template<class T>int f(){static int n=3;return n;}',
        'import-template-pattern': 'namespace N{template<class T>T f(T n){return n;}}using N::f;',
        'constexpr-template-pattern': 'template<class T>int f(){if constexpr(sizeof(T)==4)return 1;else return 0;}',
        'default-template-pattern': 'template<class T>int f(T n=T{}){return 1;}',
        'exception-template-pattern': 'template<class T>int f(T&t)noexcept(noexcept(t.get())){return 1;}',
        'type-parameter-limit-64': 'template<class T0,class T1,class T2,class T3,class T4,class T5,class T6,class T7,class T8,class T9,class T10,class T11,class T12,class T13,class T14,class T15,class T16,class T17,class T18,class T19,class T20,class T21,class T22,class T23,class T24,class T25,class T26,class T27,class T28,class T29,class T30,class T31,class T32,class T33,class T34,class T35,class T36,class T37,class T38,class T39,class T40,class T41,class T42,class T43,class T44,class T45,class T46,class T47,class T48,class T49,class T50,class T51,class T52,class T53,class T54,class T55,class T56,class T57,class T58,class T59,class T60,class T61,class T62,class T63>int f(){return 1;}int main(){return f<int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int>()-1;}',
        'record-default-lifetime': 'int live=0;struct R{R(){++live;}~R(){--live;}};template<class T>int f(T v=T{}){return live;}int main(){int n=f<R>();return n-1+live;}',
        'reference-default-lifetime': 'int live=0;struct R{R(){++live;}~R(){--live;}};template<class T>int f(const T&v=T{}){return live;}int main(){int n=f<R>();return n-1+live;}',
    }
    for name, source in function_templates_positive.items():
        check("v2-function-templates-positive-" + name, source, profile="cpp-core-v2")
    function_templates_reject = {
        'member': 'struct R{template<class T>T f(T v){return v;}};',
        'friend': 'struct R{template<class T>friend T f(T v){return v;}};',
        'operator': 'struct R{int n;};template<class T>int operator+(const R&r,T v){return r.n+v;}',
        'pack': 'template<class...T>int f(T...v){return 1;}',
        'template-template': 'template<template<class>class T>int f(){return 1;}',
        'variable': 'template<class T>int value=3;',
        'alias': 'template<class T>using Alias=T;',
        'float-argument': 'template<class T>int f(){return 1;}int main(){return f<double>();}',
        'float-signature': 'template<class T>double f(T n){return n;}int main(){return static_cast<int>(f(1));}',
        'float-body': 'template<class T>int f(T n){double x=1.0;return n;}int main(){return f(1);}',
        'floating-selected-default': 'template<class T>int f(T n=static_cast<T>(1.0)){return n;}int main(){return f<int>();}',
        'explicit-unsupported-body': 'template<class T>int f(T n){double x=1.0;return n;}template int f<int>(int);',
        'specialization-unsupported-body': 'template<class T>int f(T n){return n;}template<>int f<int>(int n){double x=1.0;return n;}',
        'runtime-dead-body': 'template<class T>int f(T n){if(false){double x=1.0;}return n;}int main(){return f(1);}',
        'allocation': 'template<class T>T* f(){return new T{};}int main(){return *f<int>();}',
        'dynamic-static': 'int value=1;template<class T>int f(){static int n=value;return n;}int main(){return f<int>();}',
        'constexpr-static': 'template<class T>constexpr int f(){static int n=1;return n;}int main(){return f<int>();}',
        'function-value': 'template<class T>T f(T n){return n;}int main(){auto p=&f<int>;return p(1);}',
        'attribute': 'template<class T>[[nodiscard]]T f(T v){return v;}',
        'parameter-attribute': 'template<class T>T f([[maybe_unused]]T v){return v;}',
        'active-include': '#include <utility>\ntemplate<class T>T f(T v){return v;}',
        'inactive-include': '#if 0\n#include <utility>\n#endif\ntemplate<class T>T f(T v){return v;}',
        'non-template-discarded': 'int f(){if constexpr(true)return 1;else return static_cast<int>(1.0);}',
        'lambda-template-list': 'template<class T>void f(){auto fn=[]<class U>(U v){return v;};}',
        'designated-init': 'template<class T>T f(){return T{.n=1};}',
        'range-init': 'template<class T>void f(T&a){for(int n=0;auto x:a){++n;}}',
        'generic-designator-macro': '#define INIT(T) T{.n=1}\ntemplate<class T>T f(){return INIT(T);}',
        'type-parameter-limit-65': 'template<class T0,class T1,class T2,class T3,class T4,class T5,class T6,class T7,class T8,class T9,class T10,class T11,class T12,class T13,class T14,class T15,class T16,class T17,class T18,class T19,class T20,class T21,class T22,class T23,class T24,class T25,class T26,class T27,class T28,class T29,class T30,class T31,class T32,class T33,class T34,class T35,class T36,class T37,class T38,class T39,class T40,class T41,class T42,class T43,class T44,class T45,class T46,class T47,class T48,class T49,class T50,class T51,class T52,class T53,class T54,class T55,class T56,class T57,class T58,class T59,class T60,class T61,class T62,class T63,class T64>int f(){return 1;}int main(){return f<int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int,int>()-1;}',
    }
    for name, source in function_templates_reject.items():
        check("v2-function-templates-reject-" + name, source, 'TR0201', profile="cpp-core-v2")
    function_templates_invalid = {
        'unused-consteval': 'template<class T>int f(){if consteval{return 1;}else{return 2;}}',
        'discarded-consteval': 'template<class T>int f(){if constexpr(sizeof(T)==4)return 1;else{if consteval{return 2;}else{return 3;}}}int main(){return f<int>();}',
        'deduction': 'template<class T>T f(T a,T b){return a;}int main(){return f(1,true);}',
        'no-argument': 'template<class T>T f(){return T{};}int main(){return f();}',
        'too-many-arguments': 'template<class T>T f(T v){return v;}int main(){return f<int,bool>(1);}',
        'selected-invalid-default': 'template<class T>int f(T n=T::missing){return n;}int main(){return f<int>();}',
        'selected-invalid-branch': 'template<class T>int f(){if constexpr(sizeof(T)==4)return T::missing;else return 1;}int main(){return f<int>();}',
        'late-specialization': 'template<class T>int f(T n){return 1;}int g(){return f(1);}template<>int f<int>(int n){return 2;}',
        'bad-explicit-definition': 'template<class T>int f(T v){return T::missing;}template int f<int>(int);',
        'ambiguous': 'template<class T>int f(T,int){return 1;}template<class T>int f(int,T){return 2;}int main(){return f(1,1);}',
    }
    for name, source in function_templates_invalid.items():
        check("v2-function-templates-invalid-" + name, source, 'TR0202', profile="cpp-core-v2")
    function_templates_missing = {
        'selected-declaration': 'template<class T>T f(T);int main(){return f(1);}',
        'external-instantiation': 'template<class T>T f(T v){return v;}extern template int f<int>(int);int main(){return f(1);}',
        'unused-specialization': 'template<class T>T f(T);template<>int f<int>(int);',
        'sizeof-signature-only': 'template<class T>T f(T v){return v;}int main(){return sizeof(f(1));}',
        'noexcept-signature-only': 'template<class T>T f(T v){return v;}int main(){return noexcept(f(1));}',
    }
    for name, source in function_templates_missing.items():
        check("v2-function-templates-missing-" + name, source, 'TR0203', profile="cpp-core-v2")
    check("v1-function-templates", "template<class T>T f(T v){return v;}int main(){return f(3);}", "TR0201")
    check("v1-function-template-pattern", "template<class T>T f(T v){return v;}", "TR0201")

    default_argument_source = """int number=1;
void mark(int n){number+=n;}
int next(){mark(1);return number;}
int&refer(){mark(2);return number;}
struct Token {
 int n;
 Token(int v):n(v){mark(1);}
 ~Token(){mark(3);}
};
struct Value {
 int n;
 Value(int v=next()):n(v){mark(2);}
 Value(const Value&r,int extra=next()):n(r.n+extra){mark(4);}
 ~Value(){mark(6);}
};
struct Element {
 int n;
 Element(const Token&t=Token(1)):n(t.n){mark(2);}
 Element(const Element&e,const Token&t=Token(1)):n(e.n+t.n){mark(4);}
 ~Element(){mark(5);}
};
struct Group { Element values[2]; };
int scalar(int n=next()){return n;}
void omitted(){scalar();scalar();}
void supplied(){scalar(4);}
int reference(int&r=refer()){return ++r;}
void aliasCall(){reference();}
int object(Value v=Value(5)){return v.n;}
void byValue(){object();}
int temporary(const Token&t=Token(1)){return t.n;}
void callTemporary(){temporary();mark(9);}
void comma(){temporary(),mark(9);}
void defaultArray(){Element values[2];mark(9);}
void emptyArray(){Element values[2]={};mark(9);}
void explicitArray(){Element values[2]={{},{}};mark(9);}
void partialArray(){Element values[2]={Element()};mark(9);}
void copyArray(){Group first;Group second(first);mark(9);}
void constructed(){Value value;}
void copied(){Value first(5);Value second(first);}
constexpr int constant(int n=7){return n;}
constexpr int folded=constant();
void quiet(int n=1)noexcept{}
void noisy(int n=next())noexcept{}
bool query(){return noexcept(quiet());}
bool noisyQuery(){return noexcept(noisy());}
"""
    default_arguments = check("default-arguments", default_argument_source, profile="cpp-core-v2")
    da_functions = {f["name"]: f for f in default_arguments["functions"]}

    def da_line(prefix):
        matches = [i for i, line in enumerate(default_argument_source.splitlines(), 1) if line.startswith(prefix)]
        assert len(matches) == 1, (prefix, matches)
        return matches[0]

    def da_function(prefix, result, parameters):
        matches = [f for f in default_arguments["functions"] if f["loc"]["line"] == da_line(prefix)
                   and f["result"] == result and [p["type"] for p in f["params"]] == parameters]
        assert len(matches) == 1, (prefix, result, parameters, matches)
        return matches[0]

    def da_record(prefix):
        matches = [r["id"] for r in default_arguments["records"] if r["loc"]["line"] == da_line(prefix)]
        assert len(matches) == 1, (prefix, matches)
        return matches[0]

    token = da_record("struct Token {")
    value = da_record("struct Value {")
    element = da_record("struct Element {")
    group = da_record("struct Group {")
    mark = da_function("void mark(", "void", ["int"])["name"]
    next_value = da_function("int next(", "int", [])["name"]
    refer = da_function("int&refer(", "ptr:int", [])["name"]
    token_ctor = da_function(" Token(int v)", "void", ["ptr:"+token, "int"])["name"]
    token_dtor = token+"_destroy"
    value_ctor = da_function(" Value(int v=", "void", ["ptr:"+value, "int"])["name"]
    value_copy = da_function(" Value(const Value&", "void", ["ptr:"+value, "cptr:"+value, "int"])["name"]
    element_ctor = da_function(" Element(const Token&", "void", ["ptr:"+element, "cptr:"+token])["name"]
    element_copy = da_function(" Element(const Element&", "void", ["ptr:"+element, "cptr:"+element, "cptr:"+token])["name"]
    scalar = da_function("int scalar(", "int", ["int"])
    assert not gc_calls(scalar), scalar
    omitted = da_function("void omitted(", "void", [])
    assert [c["callee"] for c in gc_calls(omitted)] == [next_value, scalar["name"]]*2
    supplied = da_function("void supplied(", "void", [])
    assert [c["callee"] for c in gc_calls(supplied)] == [scalar["name"]]
    assert gc_identity(supplied, gc_calls(supplied)[0]["args"][0]) == 4
    reference = da_function("int reference(", "int", ["ptr:int"])
    assert not gc_calls(reference)
    alias = da_function("void aliasCall(", "void", [])
    alias_calls = gc_calls(alias)
    assert [c["callee"] for c in alias_calls] == [refer, reference["name"]]
    assert alias_calls[1]["args"][0]["kind"] == "var"
    def da_alias_result(expr):
        if expr["kind"] in ("cast", "address", "dereference"):
            return da_alias_result(expr["args"][0])
        assert expr["kind"] == "var", expr
        if expr["name"] == alias_calls[0]["target"]["name"]:
            return True
        values = [n["value"] for n in alias["body"] if n["op"] == "assign" and n["target"].get("name") == expr["name"]]
        assert len(values) == 1, values
        return da_alias_result(values[0])
    assert da_alias_result(alias_calls[1]["args"][0])
    object_function = da_function("int object(", "int", ["ptr:"+value])
    assert [c["callee"] for c in gc_calls(object_function)] == [value+"_destroy"]
    by_value = da_function("void byValue(", "void", [])
    calls = gc_calls(by_value)
    assert [c["callee"] for c in calls] == [value_ctor, object_function["name"]]
    assert ve_pointer(by_value, calls[0]["args"][0]) == ve_pointer(by_value, calls[1]["args"][0])
    assert len([v for v in by_value["locals"] if v["type"] == value]) == 1
    temporary = da_function("int temporary(", "int", ["cptr:"+token])
    assert not gc_calls(temporary)
    for prefix, expected in (("void callTemporary(", [token_ctor, temporary["name"], token_dtor, mark]),
                             ("void comma(", [token_ctor, temporary["name"], mark, token_dtor])):
        function = da_function(prefix, "void", [])
        calls = gc_calls(function)
        assert [c["callee"] for c in calls] == expected
        constructed = next(c for c in calls if c["callee"] == token_ctor)
        destroyed = next(c for c in calls if c["callee"] == token_dtor)
        assert ve_pointer(function, constructed["args"][0]) == ve_pointer(function, destroyed["args"][0])
    for prefix, interleaved in (("void defaultArray(", True), ("void emptyArray(", True),
                                ("void explicitArray(", False), ("void partialArray(", False)):
        function = da_function(prefix, "void", [])
        calls = gc_calls(function)
        expected = ([token_ctor, element_ctor, token_dtor]*2 if interleaved else
                    [token_ctor, element_ctor]*2+[token_dtor]*2)+[mark, element+"_destroy", element+"_destroy"]
        assert [c["callee"] for c in calls] == expected, function
        constructed = [ve_pointer(function, c["args"][0]) for c in calls if c["callee"] == token_ctor]
        destroyed = [ve_pointer(function, c["args"][0]) for c in calls if c["callee"] == token_dtor]
        assert len(set(constructed)) == 2
        assert destroyed == (constructed if interleaved else constructed[::-1])
        arrays = [v for v in function["locals"] if v["type"] == "arr:2:"+element]
        assert len(arrays) == 1 and not any(v["type"] == element for v in function["locals"])
        base = ("object", arrays[0]["name"])
        assert [ve_pointer(function, c["args"][0]) for c in calls if c["callee"] == element_ctor] == [("element", base, i) for i in (0, 1)]
        assert [ve_pointer(function, c["args"][0]) for c in calls if c["callee"] == element+"_destroy"] == [("element", base, i) for i in (1, 0)]
    group_copy = da_function("struct Group {", "void", ["ptr:"+group, "cptr:"+group])
    assert [c["callee"] for c in gc_calls(group_copy)] == [token_ctor, element_copy, token_dtor]*2
    group_tokens = [ve_pointer(group_copy, c["args"][0]) for c in gc_calls(group_copy) if c["callee"] == token_ctor]
    assert len(set(group_tokens)) == 2
    assert [ve_pointer(group_copy, c["args"][0]) for c in gc_calls(group_copy) if c["callee"] == token_dtor] == group_tokens
    constructed = da_function("void constructed(", "void", [])
    assert [c["callee"] for c in gc_calls(constructed)] == [next_value, value_ctor, value+"_destroy"]
    copied = da_function("void copied(", "void", [])
    assert [c["callee"] for c in gc_calls(copied)] == [value_ctor, next_value, value_copy, value+"_destroy", value+"_destroy"]
    assert all(c["callee"] != next_value for c in gc_calls(da_functions[value_ctor]))
    assert all(c["callee"] != next_value for c in gc_calls(da_functions[value_copy]))
    for prefix, expected in (("bool query(", True), ("bool noisyQuery(", False)):
        function = da_function(prefix, "bool", [])
        assert not gc_calls(function)
        returned = [n["value"] for n in function["body"] if n["op"] == "return"]
        assert len(returned) == 1 and nq_constant(function, returned[0]) is expected
    for function in default_arguments["functions"]:
        for call in gc_calls(function):
            callee = da_functions[call["callee"]]
            assert [a["type"] for a in call["args"]] == [p["type"] for p in callee["params"]]
    with tempfile.TemporaryDirectory(prefix="neverc-default-arguments-relocated-") as temp:
        relocated = check("default-arguments-relocated", default_argument_source,
                          root=Path(temp)/"project", profile="cpp-core-v2")
        assert relocated == default_arguments, "default argument identities depend on the absolute root"

    default_argument_rejected = {
        'floating': 'int f(int n=(static_cast<void>(1.0),1)){return n;}',
        'overridden-floating': 'int f(int n=(static_cast<void>(1.0),1)){return n;}int main(){return f(0);}',
        'constexpr-floating': 'constexpr int f(int n=(static_cast<void>(1.0),1)){return n;}static_assert(f()==1);',
        'noexcept-floating': 'void f(int n=(static_cast<void>(1.0),1))noexcept{}static_assert(noexcept(f()));',
        'volatile': 'volatile int n=1;int f(int x=n){return x;}',
        'string': 'int f(int n=(static_cast<void>("x"),1)){return n;}',
        'lambda': 'int f(int n=(static_cast<void>([]{}),1)){return n;}',
        'allocation': 'int f(int*p=new int(1)){return *p;}',
        'throw': 'int f(int n=(throw 1,2)){return n;}',
        'function-pointer': 'void g(){}void f(void(*p)()=g){}',
        'member-pointer': 'struct R{int n;};void f(int R::*p=&R::n){}',
        'array-global': 'int a[2]={1,2};int f(int*p=a){return *p;}',
        'fresh-reference-return': 'int f(const int&r=1){return r;}const int&g(){return 1;}',
        'unsupported-default-record': 'struct R{double n;};int f(R r=R{1.0}){return 1;}',
        'unsupported-unused-default': 'int f(int n=(static_cast<void>("unused"),1)){return n;}',
        'expanded-default-storage': 'struct R{int values[32768];};int f(const R&r=R{}){return r.values[0];}int main(){return f();}',
    }
    for name, source in default_argument_rejected.items():
        check("v2-" + 'default_argument_rejected' + "-" + name, source, "TR0201", profile="cpp-core-v2")
    default_argument_invalid = {
        'nontrailing': 'int f(int a=1,int b){return a+b;}',
        'redefined': 'int f(int n=1);int f(int n=2){return n;}',
        'parameter-reference': 'int f(int a,int b=a){return b;}',
        'method-this': 'struct R{int n;int f(int x=this->n){return x;}};',
        'method-field': 'struct R{int n;int f(int x=n){return x;}};',
        'mutable-temporary-reference': 'int f(int&r=1){return r;}',
        'bad-conversion': 'int f(int n=nullptr){return n;}',
        'call-arity': 'int f(int a,int b=1){return a+b;}int main(){return f();}',
        'defaulted-copy-extra': 'struct R{int n;R(const R&r,int n=0)=default;};',
    }
    for name, source in default_argument_invalid.items():
        check("v2-" + 'default_argument_invalid' + "-" + name, source, "TR0202", profile="cpp-core-v2")
    default_argument_missing = {
        'default-call': 'int g();int f(int n=g()){return n;}',
        'overridden-missing': 'int g();int f(int n=g()){return n;}int main(){return f(0);}',
        'default-destructor': 'struct R{int n;~R();};int f(const R&r=R{1}){return r.n;}',
    }
    for name, source in default_argument_missing.items():
        check("v2-" + 'default_argument_missing' + "-" + name, source, "TR0203", profile="cpp-core-v2")
    check("v1-default-argument-function", "int f(int n=1){return n;}", "TR0201")
    check("v1-default-argument-constructor", "struct R{int n;R(int v=1):n(v){}};", "TR0201")
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
        assert place["kind"] == "index", place
        index = place["args"][1]
        assert index["kind"] == "literal" and index["type"] == "int", index
        assert index["value"] == str(expected_index), index
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
        'record-result-c-export': 'struct R{int n;};extern "C" R exported(){return {1};}',
        'record-parameter-c-export': 'struct R{int n;};extern "C" int exported(R r){return r.n;}',
        "untyped-assembly-string": 'asm(""); int main(){}',
        "unsupported-pointer-alias": "using Hidden=float*; int main(){}",
        "unused-volatile-alias": "using Hidden=volatile int; int main(){}",
        "unused-function-alias": "using Hidden=void(); int main(){}",
        "alias-template": "template<class T> using Hidden=T; int main(){}",
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
        "array-initialization-budget": "int f(){int a[65536]={}; return a[0];}",
        "array-temporary-dereference": "struct R{int a[2];}; int f(){const int&r=*R{{1,2}}.a; return r;}",
    })
    v2_rejections.update({
        "switch-case-range": "int f(int n){switch(n){case 1 ... 3:return 7;default:return 9;}}",
        "switch-dead-range": "int f(int n){if(false){switch(n){case 1 ... 3:return 7;}}return 0;}",
        "switch-other-attribute": "int f(int n){switch(n){case 0:[[likely]];case 1:return 7;default:return 9;}}",
    })
    v2_rejections.update({
        "void-size": "static_assert(sizeof(void)>0); int main(){}",
        "function-size": "int f(){return 0;} int main(){return sizeof(f);}",
        "preferred-alignment": "int main(){int n=0; return __alignof__(n);}",
        "expression-alignment": "int main(){int n=0; return alignof(n);}",
        "floating-size-type": "int main(){return sizeof(double);}",
        "floating-size-value": "int main(){return sizeof(1.0);}",
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
        'constructor-const-field': 'struct R{const int n;R():n(1){}};',
        'constructor-reference-field': 'struct R{int &n;R(int &v):n(v){}};',
        'constructor-mutable-field': 'struct R{mutable int n;R():n(1){}};',
        'constructor-union': 'union R{int n;unsigned u;R():n(1){}};',
        'constructor-bitfield': 'struct R{unsigned n:3;R():n(1){}};',
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

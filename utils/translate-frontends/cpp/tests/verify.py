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
        'const-assignment': 'struct R{int n;R&operator=(const R&r)const{return const_cast<R&>(*this);}};',
        'rvalue-assignment': 'struct R{int n;R&operator=(const R&r)&&{n=r.n;return *this;}};',
        'by-value-assignment': 'struct R{int n;R&operator=(R r){n=r.n;return *this;}};',
        'void-assignment': 'struct R{int n;void operator=(const R&r){n=r.n;}};',
        'other-assignment-result': 'struct R{int n;int&operator=(const R&r){n=r.n;return n;}};',
        'noexcept-constructor': 'struct R{int n;R(const R&r)noexcept:n(r.n){}};',
        'noexcept-assignment': 'struct R{int n;R&operator=(const R&r)noexcept{n=r.n;return *this;}};',
        'default-argument': 'struct R{int n;R(const R&r,int extra=0):n(r.n+extra){}};',
        'move-constructor': 'struct R{int n;R(R&&r):n(r.n){}};',
        'move-assignment': 'struct R{int n;R&operator=(R&&r){n=r.n;return *this;}};',
        'arbitrary-operator': 'struct R{int n;R operator+(const R&r){return {n+r.n};}};',
        'temporary-assignment-source': 'struct R{int n;R(int v):n(v){}R&operator=(const R&r){n=r.n;return *this;}};void f(){R r(1);r=R(2);}',
        'temporary-assignment-receiver': 'struct R{int n;R(int v):n(v){}R&operator=(const R&r){n=r.n;return *this;}};void f(){R r(1);R(2)=r;}',
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
        selected = [f for f in generated_copy["functions"] if f["result"] == "void"
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
        'copy-noexcept': 'struct R{int n;R(const R&)noexcept=default;};',
        'copy-noexcept-false': 'struct R{int n;R(const R&)noexcept(false)=default;};',
        'copy-throw': 'struct R{int n;R(const R&)throw()=default;};',
        'out-of-line-noexcept': 'struct R{int n;R(const R&)noexcept;};R::R(const R&)noexcept=default;',
        'move-default': 'struct R{int n;R(R&&)=default;};',
        'move-assignment-default': 'struct R{int n;R&operator=(R&&)=default;};',
        'reference-field': 'struct R{int &n;R(const R&)=default;};',
        'const-field': 'struct R{const int n;R(const R&)=default;};',
        'nonpublic-field': 'class R{int n;public:R(const R&)=default;};',
        'base-copy': 'struct B{int n;};struct R:B{int m;R(const R&)=default;};',
        'temporary-reference': 'struct I{int n;I(const I&s):n(s.n){}};struct R{I i;R(const R&)=default;};void f(const R&s){const R&r=R(s);}',
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
        selected = [f for f in generated_assignment["functions"] if f["result"] == "ptr:" + rid
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
        'noexcept': 'struct R{int n;R&operator=(const R&)noexcept=default;};',
        'noexcept-false': 'struct R{int n;R&operator=(const R&)noexcept(false)=default;};',
        'out-of-line-noexcept': 'struct R{int n;R&operator=(const R&)noexcept;};R&R::operator=(const R&)noexcept=default;',
        'rvalue-receiver': 'struct R{int n;R&operator=(const R&)&&=default;};',
        'move-assignment': 'struct R{int n;R&operator=(R&&)=default;};',
        'const-field': 'struct R{const int n;R&operator=(const R&)=default;};',
        'reference-field': 'struct R{int&n;R&operator=(const R&)=default;};',
        'private-field': 'class R{int n;public:R&operator=(const R&)=default;};',
        'base-field': 'struct B{int n;};struct R:B{int m;R&operator=(const R&)=default;};',
        'temporary-source': 'struct R{int n;R&operator=(const R&)=default;};void f(R&r){r=R{1};}',
        'temporary-receiver': 'struct R{int n;R&operator=(const R&)=default;};void f(const R&s){R{1}=s;}',
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
                     and [p["type"] for p in f["params"]] == ["ptr:" + dc_records[3]["id"], "ptr:" + dc_records[1]["id"]])
    constructor = next(f for f in cleanup_defaults["functions"] if f["result"] == "void"
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
        'temporary-reference': 'int take(const int&n){return n;}struct R{int n=take(1);};',
        'temporary-receiver': 'struct A{int n;int get(){return n;}};struct R{int n=A{1}.get();};',
        'move-constructor': 'struct R{int n=1;R(R&&s):n(s.n){}};',
        'template-default': 'template<class T>struct R{T n=1;};',
        'excessive-array': 'struct R{int n[65537]={1};};',
        'address-of-member': 'struct R{int n=1;int R::*p=&R::n;};',
    }
    for name, source in default_member_rejected.items():
        check("v2-default-member-reject-" + name, source, "TR0201", profile="cpp-core-v2")
    check("v2-default-member-missing", "int missing();struct R{int n=missing();};void f(){R r{7};}",
          "TR0203", profile="cpp-core-v2")
    check("v1-default-member", "struct R{int n=1;};", "TR0201")
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
        selected = [f for f in defaulted["functions"] if f["result"] == "void"
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
        'constructor-noexcept': 'struct R{int n;R()noexcept=default;};',
        'constructor-noexcept-false': 'struct R{int n;R()noexcept(false)=default;};',
        'destructor-noexcept': 'struct R{int n;~R()noexcept=default;};',
        'destructor-throw': 'struct R{int n;~R()throw()=default;};',
        'out-of-line-noexcept': 'struct R{int n;R()noexcept;};R::R()noexcept=default;',
        'out-of-line-destructor-noexcept': 'struct R{int n;~R()noexcept;};R::~R()noexcept=default;',
        'nonpublic-field': 'class R{int n;public:R()=default;};',
        'virtual-destructor': 'struct R{int n;virtual ~R()=default;};',
        'move-default': 'struct R{int n;R(R&&)=default;};',
        'explicit-destruction': 'struct R{int n;~R()=default;};void f(){R r{1};r.~R();}',
        'temporary-reference': 'struct R{int n;explicit R()=default;};int f(){const R&r=R{};return r.n;}',
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
        'explicit-noexcept': 'struct R{int n;~R()noexcept{}};',
        'explicit-noexcept-false': 'struct R{int n;~R()noexcept(false){}};',
        'explicit-empty-throw': 'struct R{int n;~R()throw(){}};',
        'explicit-deleted': 'struct R{int n;~R()=delete;};',
        'virtual': 'struct R{int n;virtual ~R(){}};',
        'explicit-call': 'struct R{int n;~R(){}};void f(R&r){r.~R();}',
        'explicit-dead-call': 'struct R{int n;~R(){}};void f(R&r){if(false)r.~R();}',
        'explicit-alias-call': 'struct R{int n;~R(){}};using T=R;void f(R&r){r.~T();}',
        'move-constructor': 'struct R{int n;R(R&&r):n(r.n){}~R(){}};',
        'global': 'struct R{int n;~R(){}};const R r{1};',
        'global-containing': 'struct R{int n;~R(){}};struct Box{R r;};const Box box{{1}};',
        'static-local': 'struct R{int n;~R(){}};int f(){static R r{1};return r.n;}',
        'thread-local': 'struct R{int n;~R(){}};int f(){thread_local R r{1};return r.n;}',
        'reference-extension': 'struct R{int n;~R(){}};int f(){const R&r=R{1};return r.n;}',
        'temporary-receiver': 'struct R{int n;~R(){}int get(){return n;}};int f(){return R{1}.get();}',
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
        'record-move-constructor': 'struct R{int n;R(int v):n(v){}R(R&&x):n(x.n){}};R f(){return R(1);}',
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
        "temporary-reference": "int f(){const int&r=1; return r;}",
        "dead-temporary-reference": "int f(){if(false){const int&r=1;} return 0;}",
        "conversion-temporary": "int f(){int x=1; const unsigned int&r=x; return r;}",
        "temporary-subobject": "struct R{int x;}; int f(){const int&r=R{1}.x; return r;}",
        "temporary-reference-argument": "int f(const int&r){return r;} int main(){return f(1);}",
        "rvalue-reference": "int f(int&&r){return r;}",
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
        'method-temporary-dot': 'struct R{int n;int get()const{return n;}};int f(){return R{1}.get();}',
        'method-temporary-arrow': 'struct E{int n;int get()const{return n;}};struct H{E a[1];};int f(){return H{{{1}}}.a->get();}',
        'method-temporary-arrow-offset': 'struct E{int n;int get()const{return n;}};struct H{E a[1];};int f(){return (H{{{1}}}.a+0)->get();}',
        'method-dead-temporary-receiver': 'struct R{int n;int get()const{return n;}};int f(){if(false)return R{1}.get();return 0;}',
        'method-folded-temporary-receiver': 'struct R{int n;constexpr int get()const{return n;}};static_assert(R{1}.get()==1);',
        'method-folded-static-function-value': 'struct R{int n;static int get(){return 1;}};static_assert((R::get,true));',
        'method-folded-parenthesized-static-value': 'struct R{int n;static int get(){return 1;}};static_assert(((R::get),true));',
        'method-method-pointer': 'struct R{int n;int get(){return n;}};auto f(){return &R::get;}',
        'method-static-function-pointer': 'struct R{int n;static int get(){return 1;}};int f(){auto p=&R::get;return p();}',
        'method-virtual-method': 'struct R{int n;virtual int get(){return n;}};',
        'method-base-class': 'struct B{int n;};struct R:B{int get(){return n;}};',
        'method-conversion': 'struct R{int n;operator int()const{return n;}};',
        'method-operator': 'struct R{int n;int operator()()const{return n;}};',
        'method-volatile-method': 'struct R{int n;int get()volatile{return n;}};',
        'method-rvalue-method': 'struct R{int n;int get()&&{return n;}};',
        'method-noexcept-method': 'struct R{int n;int get()const noexcept{return n;}};',
        'method-mutable-field': 'struct R{mutable int n;int get()const{return n;}};',
        'method-reference-field': 'struct R{int&n;int get()const{return n;}};',
        'method-member-template': 'struct R{int n;template<class T>T get(T v){return v;}};',
        'method-static-data': 'struct R{int n;static int value;int get(){return value;}};int R::value=1;',
        'method-default-argument': 'struct R{int n;int get(int v=1){return n+v;}};int f(){R r{1};return r.get();}',
        'method-constant-static-data': 'struct R{int n;static const int value=1;int get(){return value;}};',
        'method-method-temporary-reference-argument': 'struct R{int n;int get(const int&v){return n+v;}};int f(){R r{1};return r.get(2);}',
        'method-static-temporary-reference-argument': 'struct R{int n;static int get(const int&v){return v;}};int f(){return R::get(2);}',
        'method-method-comma-callee': 'struct R{int n;static int get(){return 1;}};int f(){return (0,R::get)();}',
        'method-temporary-reverse-arrow-offset': 'struct E{int n;int get()const{return n;}};struct H{E a[1];};int f(){return (0+H{{{1}}}.a)->get();}',
    })
    v2_rejections.update({
        'constructor-delegating': 'struct R{int n;R():R(1){} R(int v):n(v){}};',
        'constructor-base-initializer': 'struct B{int n;B(int v):n(v){}};struct R:B{R():B(1){}};',
        'constructor-inherited-constructor': 'struct B{int n;B(int v):n(v){}};struct R:B{using B::B;};',
        'constructor-move-constructor': 'struct R{int n;R(int v):n(v){} R(R&&v):n(v.n){}};',
        'constructor-virtual-method': 'struct R{int n;R():n(1){} virtual int get(){return n;}};',
        'constructor-template-constructor': 'struct R{int n;template<class T> R(T v):n(v){}};',
        'constructor-variadic-constructor': 'struct R{int n;R(int v,...):n(v){}};',
        'constructor-deleted-constructor': 'struct R{int n;R()=delete;};',
        'constructor-default-argument': 'struct R{int n;R(int v=1):n(v){}};',
        'constructor-noexcept-constructor': 'struct R{int n;R() noexcept:n(1){}};',
        'constructor-private-field': 'class R{int n;public:R():n(1){}};',
        'constructor-protected-field': 'struct R{protected:int n;public:R():n(1){}};',
        'constructor-const-field': 'struct R{const int n;R():n(1){}};',
        'constructor-reference-field': 'struct R{int &n;R(int &v):n(v){}};',
        'constructor-mutable-field': 'struct R{mutable int n;R():n(1){}};',
        'constructor-nested-record': 'struct R{struct I{int n;};I i;R():i{1}{}};',
        'constructor-union': 'union R{int n;unsigned u;R():n(1){}};',
        'constructor-bitfield': 'struct R{unsigned n:3;R():n(1){}};',
        'constructor-static-data': 'struct R{int n;static int value;R():n(1){}};int R::value=1;',
        'constructor-temporary-reference-argument': 'struct R{int n;R(const int &v):n(v){}};int f(){R r(1);return r.n;}',
        'constructor-dead-temporary-reference-argument': 'struct R{int n;R(const int &v):n(v){}};int f(){if(false){R r(1);return r.n;}return 0;}',
        'constructor-temporary-method-receiver': 'struct R{int n;R(int v):n(v){} int get()const{return n;}};int f(){return R(1).get();}',
        'constructor-temporary-subobject-receiver': 'struct I{int n;int get()const{return n;}};struct R{I i;R():i{1}{}};int f(){return R().i.get();}',
        'constructor-temporary-array-receiver': 'struct I{int n;int get()const{return n;}};struct R{I i[1];R():i{{1}}{}};int f(){return (R().i+0)->get();}',
        'constructor-folded-unsupported-initializer': 'struct R{int n;constexpr R():n(sizeof(float)){}};constexpr R r;',
        'constructor-folded-throw-body': 'struct R{int n;constexpr R(int v):n(v){if(v)throw 1;}};constexpr R r(0);',
        'constructor-conversion-function': 'struct R{int n;R():n(1){} operator int()const{return n;}};int f(){R r;return r;}',
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

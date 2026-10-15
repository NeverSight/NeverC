using Size=decltype(sizeof(0));
struct Arena {unsigned long long alignment;unsigned char bytes[2048];};
Arena arena{};Size offset;unsigned allocations,frees;
void* operator new(Size n){++allocations;auto p=arena.bytes+offset;offset+=(n+15)/16*16;return p;}
void operator delete(void*)noexcept{++frees;}
#include <random>
struct G{using result_type=unsigned;unsigned state,calls;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
template<class R> unsigned long long bits(R value){const auto *p=static_cast<const unsigned char*>(static_cast<const void*>(&value));unsigned long long result=0;for(unsigned i=0;i<sizeof(R);++i)result|=static_cast<unsigned long long>(p[i])<<(8*i);return result;}
unsigned weightCalls;unsigned long long argumentHash;
template<class R>R weight(R x){++weightCalls;argumentHash=argumentHash*257+bits(x);return x*x+R(1);}
template<class R>using Weight=R(*)(R);
template<class R>Weight<R> choose(bool alternate){return alternate?weight<R>:weight<R>;}
struct Result{unsigned long long low,high,bounds,densities,samples,next,arguments;unsigned calls,count,releases;Size bytes;};
template<class R>void inspect(std::piecewise_constant_distribution<R>&d,Result&r){r.low=bits(d.min());r.high=bits(d.max());auto b=d.intervals();auto density=d.densities();for(R v:b)r.bounds=r.bounds*257+bits(v);for(R v:density)r.densities=r.densities*257+bits(v);G e{17,0};for(unsigned i=0;i<10003;++i)r.samples=r.samples*257+bits(d(e));r.next=e();}
void reset(){offset=0;allocations=frees=weightCalls=0;argumentHash=0;}
void finish(Result&r){r.arguments=argumentHash;r.calls=weightCalls;r.count=allocations;r.releases=frees;r.bytes=offset;}
template<class R>Result run(bool parameter,unsigned n){reset();Result r{};{Weight<R> fn=choose<R>(n%2);if(parameter){typename std::piecewise_constant_distribution<R>::param_type p(n,R(-3.125),R(5.75),fn);std::piecewise_constant_distribution<R>d(p);inspect(d,r);}else{std::piecewise_constant_distribution<R>d(n,R(-3.125),R(5.75),fn);inspect(d,r);}}finish(r);return r;}
int verify(Result r,unsigned long long low,unsigned long long high,unsigned long long b,unsigned long long d,unsigned long long h,unsigned long long next,unsigned long long args,unsigned calls,unsigned count,unsigned releases,Size bytes){if(r.low!=low||r.high!=high||r.bounds!=b||r.densities!=d)return 1;if(r.samples!=h||r.next!=next)return 2;if(r.arguments!=args||r.calls!=calls)return 3;if(r.count!=count||r.releases!=releases||r.bytes!=bytes)return 4;return 0;}
int main(){
if(int r=verify(run<float>(false,0),3225944064ULL,1085800448ULL,830153424896ULL,1038533300ULL,18290920595254952078ULL,1860345957ULL,1067974656ULL,1,5,5,80))return 0+r;
if(int r=verify(run<float>(false,1),3225944064ULL,1085800448ULL,830153424896ULL,1038533300ULL,18290920595254952078ULL,1860345957ULL,1067974656ULL,1,5,5,80))return 8+r;
if(int r=verify(run<float>(false,3),3225944064ULL,1085800448ULL,54970092589244250ULL,68171383587185ULL,716211612465858891ULL,1860345957ULL,212838044117588ULL,3,5,5,80))return 16+r;
if(int r=verify(run<float>(false,17),3225944064ULL,1085800448ULL,3537521555710074117ULL,16922647372419586281ULL,14077435573591501568ULL,1860345957ULL,16763253012567331047ULL,17,5,5,400))return 24+r;
if(int r=verify(run<float>(true,0),3225944064ULL,1085800448ULL,830153424896ULL,1038533300ULL,18290920595254952078ULL,1860345957ULL,1067974656ULL,1,8,8,128))return 32+r;
if(int r=verify(run<float>(true,1),3225944064ULL,1085800448ULL,830153424896ULL,1038533300ULL,18290920595254952078ULL,1860345957ULL,1067974656ULL,1,8,8,128))return 40+r;
if(int r=verify(run<float>(true,3),3225944064ULL,1085800448ULL,54970092589244250ULL,68171383587185ULL,716211612465858891ULL,1860345957ULL,212838044117588ULL,3,8,8,128))return 48+r;
if(int r=verify(run<float>(true,17),3225944064ULL,1085800448ULL,3537521555710074117ULL,16922647372419586281ULL,14077435573591501568ULL,1860345957ULL,16763253012567331047ULL,17,8,8,640))return 56+r;
if(int r=verify(run<double>(false,0),13837591330072559616ULL,4618159942891732992ULL,657525545596092416ULL,4592783586188565259ULL,11809112335668010277ULL,2013693826ULL,4608589793683570688ULL,1,5,5,80))return 64+r;
if(int r=verify(run<double>(false,1),13837591330072559616ULL,4618159942891732992ULL,657525545596092416ULL,4592783586188565259ULL,11809112335668010277ULL,2013693826ULL,4608589793683570688ULL,1,5,5,80))return 72+r;
if(int r=verify(run<double>(false,3),13837591330072559616ULL,4618159942891732992ULL,90353467523768486ULL,12998244246023137503ULL,17419960995779887335ULL,2013693826ULL,9151431724057122220ULL,3,5,5,160))return 80+r;
if(int r=verify(run<double>(false,17),13837591330072559616ULL,4618159942891732992ULL,3286935940690772747ULL,1023352572233214027ULL,4719297083809823112ULL,2013693826ULL,7417728567195807566ULL,17,5,5,720))return 88+r;
if(int r=verify(run<double>(true,0),13837591330072559616ULL,4618159942891732992ULL,657525545596092416ULL,4592783586188565259ULL,11809112335668010277ULL,2013693826ULL,4608589793683570688ULL,1,8,8,128))return 96+r;
if(int r=verify(run<double>(true,1),13837591330072559616ULL,4618159942891732992ULL,657525545596092416ULL,4592783586188565259ULL,11809112335668010277ULL,2013693826ULL,4608589793683570688ULL,1,8,8,128))return 104+r;
if(int r=verify(run<double>(true,3),13837591330072559616ULL,4618159942891732992ULL,90353467523768486ULL,12998244246023137503ULL,17419960995779887335ULL,2013693826ULL,9151431724057122220ULL,3,8,8,256))return 112+r;
if(int r=verify(run<double>(true,17),13837591330072559616ULL,4618159942891732992ULL,3286935940690772747ULL,1023352572233214027ULL,4719297083809823112ULL,2013693826ULL,7417728567195807566ULL,17,8,8,1152))return 120+r;
return 0;}

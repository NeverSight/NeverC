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
template<class R>void inspect(std::piecewise_linear_distribution<R>&d,Result&r){r.low=bits(d.min());r.high=bits(d.max());auto b=d.intervals();auto density=d.densities();for(R v:b)r.bounds=r.bounds*257+bits(v);for(R v:density)r.densities=r.densities*257+bits(v);G e{17,0};for(unsigned i=0;i<10003;++i)r.samples=r.samples*257+bits(d(e));r.next=e();}
void reset(){offset=0;allocations=frees=weightCalls=0;argumentHash=0;}
void finish(Result&r){r.arguments=argumentHash;r.calls=weightCalls;r.count=allocations;r.releases=frees;r.bytes=offset;}
template<class R>Result run(bool parameter,unsigned n){reset();Result r{};{Weight<R> fn=choose<R>(n%2);if(parameter){typename std::piecewise_linear_distribution<R>::param_type p(n,R(-3.125),R(5.75),fn);std::piecewise_linear_distribution<R>d(p);inspect(d,r);}else{std::piecewise_linear_distribution<R>d(n,R(-3.125),R(5.75),fn);inspect(d,r);}}finish(r);return r;}
int verify(Result r,unsigned long long low,unsigned long long high,unsigned long long b,unsigned long long d,unsigned long long h,unsigned long long next,unsigned long long args,unsigned calls,unsigned count,unsigned releases,Size bytes){if(r.low!=low||r.high!=high||r.bounds!=b||r.densities!=d)return 1;if(r.samples!=h||r.next!=next)return 2;if(r.arguments!=args||r.calls!=calls)return 3;if(r.count!=count||r.releases!=releases||r.bytes!=bytes)return 4;return 0;}
int main(){
if(int r=verify(run<float>(false,0),3225944064ULL,1085800448ULL,830153424896ULL,265637394604ULL,17473009911073439608ULL,1860345957ULL,830153424896ULL,2ULL,5ULL,5ULL,80ULL))return 0+r;
if(int r=verify(run<float>(false,1),3225944064ULL,1085800448ULL,830153424896ULL,265637394604ULL,17473009911073439608ULL,1860345957ULL,830153424896ULL,2ULL,5ULL,5ULL,80ULL))return 8+r;
if(int r=verify(run<float>(false,3),3225944064ULL,1085800448ULL,54970092589244250ULL,17696135536510796ULL,8567988954983572518ULL,1860345957ULL,54970092589244250ULL,4ULL,5ULL,5ULL,80ULL))return 16+r;
if(int r=verify(run<float>(false,17),3225944064ULL,1085800448ULL,3537521555710074117ULL,2528377718950354893ULL,10322821774237508089ULL,1860345957ULL,3537521555710074117ULL,18ULL,5ULL,5ULL,400ULL))return 24+r;
if(int r=verify(run<float>(true,0),3225944064ULL,1085800448ULL,830153424896ULL,265637394604ULL,17473009911073439608ULL,1860345957ULL,830153424896ULL,2ULL,8ULL,8ULL,128ULL))return 32+r;
if(int r=verify(run<float>(true,1),3225944064ULL,1085800448ULL,830153424896ULL,265637394604ULL,17473009911073439608ULL,1860345957ULL,830153424896ULL,2ULL,8ULL,8ULL,128ULL))return 40+r;
if(int r=verify(run<float>(true,3),3225944064ULL,1085800448ULL,54970092589244250ULL,17696135536510796ULL,8567988954983572518ULL,1860345957ULL,54970092589244250ULL,4ULL,8ULL,8ULL,128ULL))return 48+r;
if(int r=verify(run<float>(true,17),3225944064ULL,1085800448ULL,3537521555710074117ULL,2528377718950354893ULL,10322821774237508089ULL,1860345957ULL,3537521555710074117ULL,18ULL,8ULL,8ULL,640ULL))return 56+r;
if(int r=verify(run<double>(false,0),13837591330072559616ULL,4618159942891732992ULL,657525545596092416ULL,3109488197249270193ULL,13450155193842279597ULL,2013693826ULL,657525545596092416ULL,2ULL,5ULL,5ULL,80ULL))return 64+r;
if(int r=verify(run<double>(false,1),13837591330072559616ULL,4618159942891732992ULL,657525545596092416ULL,3109488197249270193ULL,13450155193842279597ULL,2013693826ULL,657525545596092416ULL,2ULL,5ULL,5ULL,80ULL))return 72+r;
if(int r=verify(run<double>(false,3),13837591330072559616ULL,4618159942891732992ULL,90353467523768486ULL,3750427851172196989ULL,17734454657278988052ULL,2013693826ULL,90353467523768486ULL,4ULL,5ULL,5ULL,160ULL))return 80+r;
if(int r=verify(run<double>(false,17),13837591330072559616ULL,4618159942891732992ULL,3286935940690772747ULL,10315002658858633896ULL,13745681136652230860ULL,2013693826ULL,3286935940690772747ULL,18ULL,5ULL,5ULL,720ULL))return 88+r;
if(int r=verify(run<double>(true,0),13837591330072559616ULL,4618159942891732992ULL,657525545596092416ULL,3109488197249270193ULL,13450155193842279597ULL,2013693826ULL,657525545596092416ULL,2ULL,8ULL,8ULL,128ULL))return 96+r;
if(int r=verify(run<double>(true,1),13837591330072559616ULL,4618159942891732992ULL,657525545596092416ULL,3109488197249270193ULL,13450155193842279597ULL,2013693826ULL,657525545596092416ULL,2ULL,8ULL,8ULL,128ULL))return 104+r;
if(int r=verify(run<double>(true,3),13837591330072559616ULL,4618159942891732992ULL,90353467523768486ULL,3750427851172196989ULL,17734454657278988052ULL,2013693826ULL,90353467523768486ULL,4ULL,8ULL,8ULL,256ULL))return 112+r;
if(int r=verify(run<double>(true,17),13837591330072559616ULL,4618159942891732992ULL,3286935940690772747ULL,10315002658858633896ULL,13745681136652230860ULL,2013693826ULL,3286935940690772747ULL,18ULL,8ULL,8ULL,1152ULL))return 120+r;
return 0;}

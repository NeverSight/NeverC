using Size=decltype(sizeof(0));
struct Arena {unsigned long long alignment;unsigned char bytes[1024];};
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
template<class R>Result run(bool parameter,unsigned n){reset();Result r{};{std::initializer_list<R> empty{};std::initializer_list<R> one{R(-3.125)};std::initializer_list<R> two{R(-3.125),R(5.75)};std::initializer_list<R> four{R(-3.125),R(-1.3),R(2.125),R(5.75)};auto boundaries=n==0?empty:n==1?one:n==2?two:four;Weight<R> fn=choose<R>(n%2);if(parameter){typename std::piecewise_constant_distribution<R>::param_type p(boundaries,fn);std::piecewise_constant_distribution<R>d(p);inspect(d,r);}else{std::piecewise_constant_distribution<R>d(boundaries,fn);inspect(d,r);}}finish(r);return r;}
int verify(Result r,unsigned long long low,unsigned long long high,unsigned long long b,unsigned long long d,unsigned long long h,unsigned long long next,unsigned long long args,unsigned calls,unsigned count,unsigned releases,Size bytes){if(r.low!=low||r.high!=high||r.bounds!=b||r.densities!=d)return 1;if(r.samples!=h||r.next!=next)return 2;if(r.arguments!=args||r.calls!=calls)return 3;if(r.count!=count||r.releases!=releases||r.bytes!=bytes)return 4;return 0;}
int main(){
if(int r=verify(run<float>(false,0),0ULL,1065353216ULL,1065353216ULL,1065353216ULL,16862645768230927621ULL,1860345957ULL,0ULL,0,5,5,80))return 0+r;
if(int r=verify(run<float>(false,1),0ULL,1065353216ULL,1065353216ULL,1065353216ULL,16862645768230927621ULL,1860345957ULL,0ULL,0,6,6,96))return 8+r;
if(int r=verify(run<float>(false,2),3225944064ULL,1085800448ULL,830153424896ULL,1038533300ULL,18290920595254952078ULL,1860345957ULL,1067974656ULL,1,5,5,80))return 16+r;
if(int r=verify(run<float>(false,4),3225944064ULL,1085800448ULL,54971735579112038ULL,69018353525599ULL,2299610224159324138ULL,1860345957ULL,213089557230798ULL,3,5,5,80))return 24+r;
if(int r=verify(run<float>(true,0),0ULL,1065353216ULL,1065353216ULL,1065353216ULL,16862645768230927621ULL,1860345957ULL,0ULL,0,8,8,128))return 32+r;
if(int r=verify(run<float>(true,1),0ULL,1065353216ULL,1065353216ULL,1065353216ULL,16862645768230927621ULL,1860345957ULL,0ULL,0,9,9,144))return 40+r;
if(int r=verify(run<float>(true,2),3225944064ULL,1085800448ULL,830153424896ULL,1038533300ULL,18290920595254952078ULL,1860345957ULL,1067974656ULL,1,8,8,128))return 48+r;
if(int r=verify(run<float>(true,4),3225944064ULL,1085800448ULL,54971735579112038ULL,69018353525599ULL,2299610224159324138ULL,1860345957ULL,213089557230798ULL,3,8,8,128))return 56+r;
if(int r=verify(run<double>(false,0),0ULL,4607182418800017408ULL,4607182418800017408ULL,4607182418800017408ULL,12522533668589908900ULL,2013693826ULL,0ULL,0,5,5,80))return 64+r;
if(int r=verify(run<double>(false,1),0ULL,4607182418800017408ULL,4607182418800017408ULL,4607182418800017408ULL,12522533668589908900ULL,2013693826ULL,0ULL,0,6,6,96))return 72+r;
if(int r=verify(run<double>(false,2),13837591330072559616ULL,4618159942891732992ULL,657525545596092416ULL,4592783586188565259ULL,11809112335668010277ULL,2013693826ULL,4608589793683570688ULL,1,5,5,80))return 80+r;
if(int r=verify(run<double>(false,4),13837591330072559616ULL,4618159942891732992ULL,15167053940072343245ULL,6543233517038934417ULL,13430307620351239889ULL,2013693826ULL,15054295064402027929ULL,3,5,5,160))return 88+r;
if(int r=verify(run<double>(true,0),0ULL,4607182418800017408ULL,4607182418800017408ULL,4607182418800017408ULL,12522533668589908900ULL,2013693826ULL,0ULL,0,8,8,128))return 96+r;
if(int r=verify(run<double>(true,1),0ULL,4607182418800017408ULL,4607182418800017408ULL,4607182418800017408ULL,12522533668589908900ULL,2013693826ULL,0ULL,0,9,9,144))return 104+r;
if(int r=verify(run<double>(true,2),13837591330072559616ULL,4618159942891732992ULL,657525545596092416ULL,4592783586188565259ULL,11809112335668010277ULL,2013693826ULL,4608589793683570688ULL,1,8,8,128))return 112+r;
if(int r=verify(run<double>(true,4),13837591330072559616ULL,4618159942891732992ULL,15167053940072343245ULL,6543233517038934417ULL,13430307620351239889ULL,2013693826ULL,15054295064402027929ULL,3,8,8,256))return 120+r;
return 0;}

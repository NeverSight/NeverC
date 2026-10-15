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
template<class R>void inspect(std::piecewise_linear_distribution<R>&d,Result&r){r.low=bits(d.min());r.high=bits(d.max());auto b=d.intervals();auto density=d.densities();for(R v:b)r.bounds=r.bounds*257+bits(v);for(R v:density)r.densities=r.densities*257+bits(v);G e{17,0};for(unsigned i=0;i<10003;++i)r.samples=r.samples*257+bits(d(e));r.next=e();}
void reset(){offset=0;allocations=frees=weightCalls=0;argumentHash=0;}
void finish(Result&r){r.arguments=argumentHash;r.calls=weightCalls;r.count=allocations;r.releases=frees;r.bytes=offset;}
template<class R>Result run(bool parameter,unsigned n){reset();Result r{};{std::initializer_list<R> empty{};std::initializer_list<R> one{R(-3.125)};std::initializer_list<R> two{R(-3.125),R(5.75)};std::initializer_list<R> four{R(-3.125),R(-1.3),R(2.125),R(5.75)};auto boundaries=n==0?empty:n==1?one:n==2?two:four;Weight<R> fn=choose<R>(n%2);if(parameter){typename std::piecewise_linear_distribution<R>::param_type p(boundaries,fn);std::piecewise_linear_distribution<R>d(p);inspect(d,r);}else{std::piecewise_linear_distribution<R>d(boundaries,fn);inspect(d,r);}}finish(r);return r;}
int verify(Result r,unsigned long long low,unsigned long long high,unsigned long long b,unsigned long long d,unsigned long long h,unsigned long long next,unsigned long long args,unsigned calls,unsigned count,unsigned releases,Size bytes){if(r.low!=low||r.high!=high||r.bounds!=b||r.densities!=d)return 1;if(r.samples!=h||r.next!=next)return 2;if(r.arguments!=args||r.calls!=calls)return 3;if(r.count!=count||r.releases!=releases||r.bytes!=bytes)return 4;return 0;}
int main(){
if(int r=verify(run<float>(false,0),0ULL,1065353216ULL,1065353216ULL,274861129728ULL,16862645768230927621ULL,1860345957ULL,0ULL,0ULL,5ULL,5ULL,80ULL))return 0+r;
if(int r=verify(run<float>(false,1),0ULL,1065353216ULL,1065353216ULL,274861129728ULL,16862645768230927621ULL,1860345957ULL,0ULL,0ULL,6ULL,6ULL,96ULL))return 8+r;
if(int r=verify(run<float>(false,2),3225944064ULL,1085800448ULL,830153424896ULL,265637394604ULL,17473009911073439608ULL,1860345957ULL,830153424896ULL,2ULL,5ULL,5ULL,80ULL))return 16+r;
if(int r=verify(run<float>(false,4),3225944064ULL,1085800448ULL,54971735579112038ULL,17689806854599048ULL,16432310484299988301ULL,1860345957ULL,54971735579112038ULL,4ULL,5ULL,5ULL,80ULL))return 24+r;
if(int r=verify(run<float>(true,0),0ULL,1065353216ULL,1065353216ULL,274861129728ULL,16862645768230927621ULL,1860345957ULL,0ULL,0ULL,8ULL,8ULL,128ULL))return 32+r;
if(int r=verify(run<float>(true,1),0ULL,1065353216ULL,1065353216ULL,274861129728ULL,16862645768230927621ULL,1860345957ULL,0ULL,0ULL,9ULL,9ULL,144ULL))return 40+r;
if(int r=verify(run<float>(true,2),3225944064ULL,1085800448ULL,830153424896ULL,265637394604ULL,17473009911073439608ULL,1860345957ULL,830153424896ULL,2ULL,8ULL,8ULL,128ULL))return 48+r;
if(int r=verify(run<float>(true,4),3225944064ULL,1085800448ULL,54971735579112038ULL,17689806854599048ULL,16432310484299988301ULL,1860345957ULL,54971735579112038ULL,4ULL,8ULL,8ULL,128ULL))return 56+r;
if(int r=verify(run<double>(false,0),0ULL,4607182418800017408ULL,4607182418800017408ULL,8061443332993187840ULL,12522533668589908900ULL,2013693826ULL,0ULL,0ULL,5ULL,5ULL,80ULL))return 64+r;
if(int r=verify(run<double>(false,1),0ULL,4607182418800017408ULL,4607182418800017408ULL,8061443332993187840ULL,12522533668589908900ULL,2013693826ULL,0ULL,0ULL,6ULL,6ULL,96ULL))return 72+r;
if(int r=verify(run<double>(false,2),13837591330072559616ULL,4618159942891732992ULL,657525545596092416ULL,3109488197249270193ULL,13450155193842279597ULL,2013693826ULL,657525545596092416ULL,2ULL,5ULL,5ULL,80ULL))return 80+r;
if(int r=verify(run<double>(false,4),13837591330072559616ULL,4618159942891732992ULL,15167053940072343245ULL,268967843321548306ULL,14802310174777409836ULL,2013693826ULL,15167053940072343245ULL,4ULL,5ULL,5ULL,160ULL))return 88+r;
if(int r=verify(run<double>(true,0),0ULL,4607182418800017408ULL,4607182418800017408ULL,8061443332993187840ULL,12522533668589908900ULL,2013693826ULL,0ULL,0ULL,8ULL,8ULL,128ULL))return 96+r;
if(int r=verify(run<double>(true,1),0ULL,4607182418800017408ULL,4607182418800017408ULL,8061443332993187840ULL,12522533668589908900ULL,2013693826ULL,0ULL,0ULL,9ULL,9ULL,144ULL))return 104+r;
if(int r=verify(run<double>(true,2),13837591330072559616ULL,4618159942891732992ULL,657525545596092416ULL,3109488197249270193ULL,13450155193842279597ULL,2013693826ULL,657525545596092416ULL,2ULL,8ULL,8ULL,128ULL))return 112+r;
if(int r=verify(run<double>(true,4),13837591330072559616ULL,4618159942891732992ULL,15167053940072343245ULL,268967843321548306ULL,14802310174777409836ULL,2013693826ULL,15167053940072343245ULL,4ULL,8ULL,8ULL,256ULL))return 120+r;
return 0;}

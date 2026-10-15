using Size=decltype(sizeof(0));
struct Arena {unsigned long long alignment;unsigned char bytes[1024];};
Arena arena{};Size offset;unsigned allocations,frees;
void* operator new(Size n){++allocations;auto p=arena.bytes+offset;offset+=(n+15)/16*16;return p;}
void operator delete(void*)noexcept{++frees;}
#include <random>
struct G{using result_type=unsigned;unsigned state,calls;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
template<class R> unsigned long long bits(R value){const auto *p=static_cast<const unsigned char*>(static_cast<const void*>(&value));unsigned long long result=0;for(unsigned i=0;i<sizeof(R);++i)result|=static_cast<unsigned long long>(p[i])<<(8*i);return result;}
unsigned wrongIdentity;unsigned weightCalls;unsigned long long argumentHash;
template<class R>R weight(R x){++weightCalls;argumentHash=argumentHash*257+bits(x);return x*x+R(1);}
template<class R>using Weight=R(*)(R);
template<class R>Weight<R> choose(bool alternate){return alternate?weight<R>:weight<R>;}
struct Result{unsigned long long low,high,bounds,densities,samples,next,arguments;unsigned calls,count,releases;Size bytes;};
template<class R>void inspect(std::piecewise_linear_distribution<R>&d,Result&r){r.low=bits(d.min());r.high=bits(d.max());auto b=d.intervals();auto density=d.densities();for(R v:b)r.bounds=r.bounds*257+bits(v);for(R v:density)r.densities=r.densities*257+bits(v);G e{17,0};for(unsigned i=0;i<10003;++i)r.samples=r.samples*257+bits(d(e));r.next=e();}
void reset(){offset=0;allocations=frees=weightCalls=0;argumentHash=0;wrongIdentity=0;}
void finish(Result&r){r.arguments=argumentHash;r.calls=weightCalls;r.count=allocations;r.releases=frees;r.bytes=offset;}
double doubleValue;float floatValue;
const double&doubleWeight(const double& x){++weightCalls;if(static_cast<const void*>(&x)!=static_cast<const void*>(arena.bytes+(weightCalls-1)*sizeof(double)))++wrongIdentity;argumentHash=argumentHash*257+bits(x);doubleValue=static_cast<double>(x*x+weightCalls);return doubleValue;}
float&floatWeight(const float&x){++weightCalls;argumentHash=argumentHash*257+bits(x);floatValue=x*x+static_cast<float>(weightCalls);return floatValue;}
template<class R,class F>Result run(bool list,bool parameter,F fn){reset();Result r{};{std::initializer_list<R> b{R(-3.125),R(-1.3),R(2.125),R(5.75)};
 if(list){if(parameter){typename std::piecewise_linear_distribution<R>::param_type p(b,fn);std::piecewise_linear_distribution<R>d(p);inspect(d,r);}else{std::piecewise_linear_distribution<R>d(b,fn);inspect(d,r);}}
 else{if(parameter){typename std::piecewise_linear_distribution<R>::param_type p(3,R(-3.125),R(5.75),fn);std::piecewise_linear_distribution<R>d(p);inspect(d,r);}else{std::piecewise_linear_distribution<R>d(3,R(-3.125),R(5.75),fn);inspect(d,r);}}
 }finish(r);return r;}
int verify(Result r,unsigned long long low,unsigned long long high,unsigned long long b,unsigned long long d,unsigned long long h,unsigned long long next,unsigned long long args,unsigned calls,unsigned count,unsigned releases,Size bytes){if(wrongIdentity)return 5;if(r.low!=low||r.high!=high||r.bounds!=b||r.densities!=d)return 1;if(r.samples!=h||r.next!=next)return 2;if(r.arguments!=args||r.calls!=calls)return 3;if(r.count!=count||r.releases!=releases||r.bytes!=bytes)return 4;return 0;}
int main(){
if(int r=verify(run<double>(false,false,doubleWeight),13837591330072559616ULL,4618159942891732992ULL,90353467523768486ULL,7144495439703035797ULL,7595433771771068252ULL,2013693826ULL,90353467523768486ULL,4ULL,5ULL,5ULL,160ULL))return 0+r;
if(int r=verify(run<double>(false,true,doubleWeight),13837591330072559616ULL,4618159942891732992ULL,90353467523768486ULL,7144495439703035797ULL,7595433771771068252ULL,2013693826ULL,90353467523768486ULL,4ULL,8ULL,8ULL,256ULL))return 8+r;
if(int r=verify(run<double>(true,false,doubleWeight),13837591330072559616ULL,4618159942891732992ULL,15167053940072343245ULL,12857540928582851965ULL,3546364904318442070ULL,2013693826ULL,15167053940072343245ULL,4ULL,5ULL,5ULL,160ULL))return 16+r;
if(int r=verify(run<double>(true,true,doubleWeight),13837591330072559616ULL,4618159942891732992ULL,15167053940072343245ULL,12857540928582851965ULL,3546364904318442070ULL,2013693826ULL,15167053940072343245ULL,4ULL,8ULL,8ULL,256ULL))return 24+r;
if(int r=verify(run<double>(false,false,floatWeight),13837591330072559616ULL,4618159942891732992ULL,90353467523768486ULL,7142023222929499307ULL,13764637460166044167ULL,2013693826ULL,54970092588914262ULL,4ULL,5ULL,5ULL,160ULL))return 32+r;
if(int r=verify(run<double>(false,true,floatWeight),13837591330072559616ULL,4618159942891732992ULL,90353467523768486ULL,7142023222929499307ULL,13764637460166044167ULL,2013693826ULL,54970092588914262ULL,4ULL,8ULL,8ULL,256ULL))return 40+r;
if(int r=verify(run<double>(true,false,floatWeight),13837591330072559616ULL,4618159942891732992ULL,15167053940072343245ULL,12858015961080570732ULL,2043767508938670698ULL,2013693826ULL,54971735579112038ULL,4ULL,5ULL,5ULL,160ULL))return 48+r;
if(int r=verify(run<double>(true,true,floatWeight),13837591330072559616ULL,4618159942891732992ULL,15167053940072343245ULL,12858015961080570732ULL,2043767508938670698ULL,2013693826ULL,54971735579112038ULL,4ULL,8ULL,8ULL,256ULL))return 56+r;
return 0;}

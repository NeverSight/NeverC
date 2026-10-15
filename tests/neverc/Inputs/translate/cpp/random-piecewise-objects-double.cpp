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
struct Result{unsigned long long low,high,bounds,densities,samples,next,arguments;unsigned calls,count,releases;Size bytes;unsigned copies,deaths,notes,noteDeaths,wrong,constructions;unsigned long long trace;};
template<class R>void inspect(std::piecewise_constant_distribution<R>&d,Result&r){r.low=bits(d.min());r.high=bits(d.max());auto b=d.intervals();auto density=d.densities();for(R v:b)r.bounds=r.bounds*257+bits(v);for(R v:density)r.densities=r.densities*257+bits(v);G e{17,0};for(unsigned i=0;i<10003;++i)r.samples=r.samples*257+bits(d(e));r.next=e();}
void reset(){offset=0;allocations=frees=weightCalls=0;argumentHash=0;}
void finish(Result&r){r.arguments=argumentHash;r.calls=weightCalls;r.count=allocations;r.releases=frees;r.bytes=offset;}
unsigned copies,deaths,notes,noteDeaths,wrong,constructions;unsigned long long trace;
struct Note{Note(){++notes;trace=trace*257+3;}~Note(){++noteDeaths;trace=trace*257+4;}};
template<class R>struct Owner{Owner*self;unsigned tag,state;R value;
 Owner():self(this),tag(0),state(1),value(0){++constructions;trace=trace*257+1;}
 Owner(const Owner&other,const Note& note=Note()):self(this),tag(other.tag+1),state(other.state),value(other.value){++copies;trace=trace*257+100+tag;}
 ~Owner(){++deaths;trace=trace*257+200+tag;}
 const R&operator()(const double&x)&{++weightCalls;argumentHash=argumentHash*257+bits(x);if(self!=this)++wrong;trace=trace*257+tag*10+state;value=static_cast<R>(x*x+static_cast<double>(state++ + tag));return value;}
};
template<class R>Result run(bool list,bool parameter,unsigned n){reset();copies=deaths=notes=noteDeaths=wrong=constructions=0;trace=0;Result r{};{Owner<R>w;std::initializer_list<R> empty{};std::initializer_list<R> one{R(-3.125)};std::initializer_list<R> four{R(-3.125),R(-1.3),R(2.125),R(5.75)};auto boundaries=n==0?empty:n==1?one:four;
 if(list){if(parameter){typename std::piecewise_constant_distribution<R>::param_type p(boundaries,w);std::piecewise_constant_distribution<R>d(p);inspect(d,r);}else{std::piecewise_constant_distribution<R>d(boundaries,w);inspect(d,r);}}
 else{if(parameter){typename std::piecewise_constant_distribution<R>::param_type p(n,R(-3.125),R(5.75),w);std::piecewise_constant_distribution<R>d(p);inspect(d,r);}else{std::piecewise_constant_distribution<R>d(n,R(-3.125),R(5.75),w);inspect(d,r);}}
 if(w.state!=1||w.self!=&w)++wrong;
 }finish(r);r.copies=copies;r.deaths=deaths;r.notes=notes;r.noteDeaths=noteDeaths;r.wrong=wrong;r.constructions=constructions;r.trace=trace;return r;}
int verify(Result r,unsigned long long low,unsigned long long high,unsigned long long b,unsigned long long d,unsigned long long h,unsigned long long next,unsigned long long args,unsigned calls,unsigned count,unsigned releases,Size bytes,unsigned c,unsigned deaths,unsigned notes,unsigned nd,unsigned wrong,unsigned constructions,unsigned long long trace){if(r.low!=low||r.high!=high||r.bounds!=b||r.densities!=d)return 1;if(r.samples!=h||r.next!=next)return 2;if(r.arguments!=args||r.calls!=calls)return 3;if(r.count!=count||r.releases!=releases||r.bytes!=bytes)return 4;if(r.copies!=c||r.deaths!=deaths||r.notes!=notes||r.noteDeaths!=nd||r.wrong!=wrong||r.constructions!=constructions||r.trace!=trace)return 5;return 0;}
int main(){
if(int r=verify(run<double>(false,false,0),13837591330072559616ULL,4618159942891732992ULL,657525545596092416ULL,4592783586188565259ULL,11809112335668010277ULL,2013693826ULL,4608589793683570688ULL,1,5,5,80,2,3,2,2,0,1,2049256182627695690ULL))return 0+r;
if(int r=verify(run<double>(false,false,1),13837591330072559616ULL,4618159942891732992ULL,657525545596092416ULL,4592783586188565259ULL,11809112335668010277ULL,2013693826ULL,4608589793683570688ULL,1,5,5,80,2,3,2,2,0,1,2049256182627695690ULL))return 8+r;
if(int r=verify(run<double>(false,false,3),13837591330072559616ULL,4618159942891732992ULL,90353467523768486ULL,4888521233255514140ULL,411874396879819921ULL,2013693826ULL,9151431724057122220ULL,3,5,5,160,2,3,2,2,0,1,7508494250645111159ULL))return 16+r;
if(int r=verify(run<double>(false,true,0),13837591330072559616ULL,4618159942891732992ULL,657525545596092416ULL,4592783586188565259ULL,11809112335668010277ULL,2013693826ULL,4608589793683570688ULL,1,8,8,128,1,2,1,1,0,1,291941081702921ULL))return 24+r;
if(int r=verify(run<double>(false,true,1),13837591330072559616ULL,4618159942891732992ULL,657525545596092416ULL,4592783586188565259ULL,11809112335668010277ULL,2013693826ULL,4608589793683570688ULL,1,8,8,128,1,2,1,1,0,1,291941081702921ULL))return 32+r;
if(int r=verify(run<double>(false,true,3),13837591330072559616ULL,4618159942891732992ULL,90353467523768486ULL,8619770318740790580ULL,5337055431201808237ULL,2013693826ULL,9151431724057122220ULL,3,8,8,256,1,2,1,1,0,1,835671607332610338ULL))return 40+r;
if(int r=verify(run<double>(true,false,0),0ULL,4607182418800017408ULL,4607182418800017408ULL,4607182418800017408ULL,12522533668589908900ULL,2013693826ULL,0ULL,0,5,5,80,2,3,2,2,0,1,11851214904331911477ULL))return 48+r;
if(int r=verify(run<double>(true,false,1),0ULL,4607182418800017408ULL,4607182418800017408ULL,4607182418800017408ULL,12522533668589908900ULL,2013693826ULL,0ULL,0,6,6,96,2,3,2,2,0,1,11851214904331911477ULL))return 56+r;
if(int r=verify(run<double>(true,false,3),13837591330072559616ULL,4618159942891732992ULL,15167053940072343245ULL,16488714797545287566ULL,8312191764487049360ULL,2013693826ULL,15054295064402027929ULL,3,5,5,160,2,3,2,2,0,1,7508494250645111159ULL))return 64+r;
if(int r=verify(run<double>(true,true,0),0ULL,4607182418800017408ULL,4607182418800017408ULL,4607182418800017408ULL,12522533668589908900ULL,2013693826ULL,0ULL,0,8,8,128,1,2,1,1,0,1,1135970015230ULL))return 72+r;
if(int r=verify(run<double>(true,true,1),0ULL,4607182418800017408ULL,4607182418800017408ULL,4607182418800017408ULL,12522533668589908900ULL,2013693826ULL,0ULL,0,9,9,144,1,2,1,1,0,1,1135970015230ULL))return 80+r;
if(int r=verify(run<double>(true,true,3),13837591330072559616ULL,4618159942891732992ULL,15167053940072343245ULL,4235531582723088675ULL,17605886817247999927ULL,2013693826ULL,15054295064402027929ULL,3,8,8,256,1,2,1,1,0,1,835671607332610338ULL))return 88+r;
return 0;}

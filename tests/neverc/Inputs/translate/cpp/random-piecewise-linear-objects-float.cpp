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
template<class R>void inspect(std::piecewise_linear_distribution<R>&d,Result&r){r.low=bits(d.min());r.high=bits(d.max());auto b=d.intervals();auto density=d.densities();for(R v:b)r.bounds=r.bounds*257+bits(v);for(R v:density)r.densities=r.densities*257+bits(v);G e{17,0};for(unsigned i=0;i<10003;++i)r.samples=r.samples*257+bits(d(e));r.next=e();}
void reset(){offset=0;allocations=frees=weightCalls=0;argumentHash=0;}
void finish(Result&r){r.arguments=argumentHash;r.calls=weightCalls;r.count=allocations;r.releases=frees;r.bytes=offset;}
unsigned copies,deaths,notes,noteDeaths,wrong,constructions;unsigned long long trace;
struct Note{Note(){++notes;trace=trace*257+3;}~Note(){++noteDeaths;trace=trace*257+4;}};
template<class R>struct Owner{Owner*self;unsigned tag,state;R value;
 Owner():self(this),tag(0),state(1),value(0){++constructions;trace=trace*257+1;}
 Owner(const Owner&other,const Note& note=Note()):self(this),tag(other.tag+1),state(other.state),value(other.value){++copies;trace=trace*257+100+tag;}
 ~Owner(){++deaths;trace=trace*257+200+tag;}
 const R&operator()(const R&x)&{++weightCalls;argumentHash=argumentHash*257+bits(x);if(self!=this)++wrong;if(static_cast<const void*>(&x)!=static_cast<const void*>(arena.bytes+(weightCalls-1)*sizeof(R)))++wrong;trace=trace*257+tag*10+state;value=static_cast<R>(x*x+static_cast<double>(state++ + tag));return value;}
};
template<class R>Result run(bool list,bool parameter,unsigned n){reset();copies=deaths=notes=noteDeaths=wrong=constructions=0;trace=0;Result r{};{Owner<R>w;std::initializer_list<R> empty{};std::initializer_list<R> one{R(-3.125)};std::initializer_list<R> four{R(-3.125),R(-1.3),R(2.125),R(5.75)};auto boundaries=n==0?empty:n==1?one:four;
 if(list){if(parameter){typename std::piecewise_linear_distribution<R>::param_type p(boundaries,w);std::piecewise_linear_distribution<R>d(p);inspect(d,r);}else{std::piecewise_linear_distribution<R>d(boundaries,w);inspect(d,r);}}
 else{if(parameter){typename std::piecewise_linear_distribution<R>::param_type p(n,R(-3.125),R(5.75),w);std::piecewise_linear_distribution<R>d(p);inspect(d,r);}else{std::piecewise_linear_distribution<R>d(n,R(-3.125),R(5.75),w);inspect(d,r);}}
 if(w.state!=1||w.self!=&w)++wrong;
 }finish(r);r.copies=copies;r.deaths=deaths;r.notes=notes;r.noteDeaths=noteDeaths;r.wrong=wrong;r.constructions=constructions;r.trace=trace;return r;}
int verify(Result r,unsigned long long low,unsigned long long high,unsigned long long b,unsigned long long d,unsigned long long h,unsigned long long next,unsigned long long args,unsigned calls,unsigned count,unsigned releases,Size bytes,unsigned c,unsigned deaths,unsigned notes,unsigned nd,unsigned wrong,unsigned constructions,unsigned long long trace){if(r.low!=low||r.high!=high||r.bounds!=b||r.densities!=d)return 1;if(r.samples!=h||r.next!=next)return 2;if(r.arguments!=args||r.calls!=calls)return 3;if(r.count!=count||r.releases!=releases||r.bytes!=bytes)return 4;if(r.copies!=c||r.deaths!=deaths||r.notes!=notes||r.noteDeaths!=nd||r.wrong!=wrong||r.constructions!=constructions||r.trace!=trace)return 5;return 0;}
int main(){
if(int r=verify(run<float>(false,false,0),3225944064ULL,1085800448ULL,830153424896ULL,265886515791ULL,16986371077391899571ULL,1860345957ULL,830153424896ULL,2ULL,5ULL,5ULL,80ULL,2ULL,3ULL,2ULL,2ULL,0ULL,1ULL,10149803924007702880ULL))return 0+r;
if(int r=verify(run<float>(false,false,1),3225944064ULL,1085800448ULL,830153424896ULL,265886515791ULL,16986371077391899571ULL,1860345957ULL,830153424896ULL,2ULL,5ULL,5ULL,80ULL,2ULL,3ULL,2ULL,2ULL,0ULL,1ULL,10149803924007702880ULL))return 8+r;
if(int r=verify(run<float>(false,false,3),3225944064ULL,1085800448ULL,54970092589244250ULL,17669892564683723ULL,4780625745324772627ULL,1860345957ULL,54970092589244250ULL,4ULL,5ULL,5ULL,80ULL,2ULL,3ULL,2ULL,2ULL,0ULL,1ULL,11221440044867341711ULL))return 16+r;
if(int r=verify(run<float>(false,true,0),3225944064ULL,1085800448ULL,830153424896ULL,265728170877ULL,7196890587359477270ULL,1860345957ULL,830153424896ULL,2ULL,8ULL,8ULL,128ULL,1ULL,2ULL,1ULL,1ULL,0ULL,1ULL,75028854802414101ULL))return 24+r;
if(int r=verify(run<float>(false,true,1),3225944064ULL,1085800448ULL,830153424896ULL,265728170877ULL,7196890587359477270ULL,1860345957ULL,830153424896ULL,2ULL,8ULL,8ULL,128ULL,1ULL,2ULL,1ULL,1ULL,0ULL,1ULL,75028854802414101ULL))return 32+r;
if(int r=verify(run<float>(false,true,3),3225944064ULL,1085800448ULL,54970092589244250ULL,17667674071029532ULL,14765231368603563173ULL,1860345957ULL,54970092589244250ULL,4ULL,8ULL,8ULL,128ULL,1ULL,2ULL,1ULL,1ULL,0ULL,1ULL,11853418270514501680ULL))return 40+r;
if(int r=verify(run<float>(true,false,0),0ULL,1065353216ULL,1065353216ULL,274861129728ULL,16862645768230927621ULL,1860345957ULL,0ULL,0ULL,5ULL,5ULL,80ULL,2ULL,3ULL,2ULL,2ULL,0ULL,1ULL,11851214904331911477ULL))return 48+r;
if(int r=verify(run<float>(true,false,1),0ULL,1065353216ULL,1065353216ULL,274861129728ULL,16862645768230927621ULL,1860345957ULL,0ULL,0ULL,6ULL,6ULL,96ULL,2ULL,3ULL,2ULL,2ULL,0ULL,1ULL,11851214904331911477ULL))return 56+r;
if(int r=verify(run<float>(true,false,3),3225944064ULL,1085800448ULL,54971735579112038ULL,17662184980823611ULL,1287299994649065572ULL,1860345957ULL,54971735579112038ULL,4ULL,5ULL,5ULL,80ULL,2ULL,3ULL,2ULL,2ULL,0ULL,1ULL,11221440044867341711ULL))return 64+r;
if(int r=verify(run<float>(true,true,0),0ULL,1065353216ULL,1065353216ULL,274861129728ULL,16862645768230927621ULL,1860345957ULL,0ULL,0ULL,8ULL,8ULL,128ULL,1ULL,2ULL,1ULL,1ULL,0ULL,1ULL,1135970015230ULL))return 72+r;
if(int r=verify(run<float>(true,true,1),0ULL,1065353216ULL,1065353216ULL,274861129728ULL,16862645768230927621ULL,1860345957ULL,0ULL,0ULL,9ULL,9ULL,144ULL,1ULL,2ULL,1ULL,1ULL,0ULL,1ULL,1135970015230ULL))return 80+r;
if(int r=verify(run<float>(true,true,3),3225944064ULL,1085800448ULL,54971735579112038ULL,17659581674015983ULL,12876004567583930279ULL,1860345957ULL,54971735579112038ULL,4ULL,8ULL,8ULL,128ULL,1ULL,2ULL,1ULL,1ULL,0ULL,1ULL,11853418270514501680ULL))return 88+r;
return 0;}

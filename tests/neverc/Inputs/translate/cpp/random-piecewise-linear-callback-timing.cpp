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
 const R&operator()(const double&x)&{++weightCalls;argumentHash=argumentHash*257+bits(x);if(self!=this)++wrong;trace=trace*257+tag*10+state;value=static_cast<R>(x*x+static_cast<double>(state++ + tag));return value;}
};
void clear(){reset();copies=deaths=notes=noteDeaths=wrong=constructions=0;trace=0;}
int timing(bool list,unsigned n){using D=std::piecewise_linear_distribution<double>;using P=D::param_type;std::initializer_list<double> empty{};std::initializer_list<double> one{-3.125};std::initializer_list<double> four{-3.125,-1.3,2.125,5.75};auto b=n==0?empty:n==1?one:four;unsigned observed;
 clear();if(list)observed=(D(b,Owner<double>{}),deaths+10*noteDeaths);else observed=(D(n,-3.125,5.75,Owner<double>{}),deaths+10*noteDeaths);
 if(observed!=11||copies!=1||deaths!=2||notes!=1||noteDeaths!=1||constructions!=1||wrong||allocations!=frees)return 1;
 if(weightCalls!=(list?(n>1?4:0):(n?n+1:2)))return 2;
 clear();if(list)observed=(P(b,Owner<double>{}),deaths+10*noteDeaths);else observed=(P(n,-3.125,5.75,Owner<double>{}),deaths+10*noteDeaths);
 if(observed||copies||deaths!=1||notes||noteDeaths||constructions!=1||wrong||allocations!=frees)return 3;
 if(weightCalls!=(list?(n>1?4:0):(n?n+1:2)))return 4;return 0;}
int main(){for(unsigned n:{0u,1u,3u}){if(int r=timing(false,n))return r;if(int r=timing(true,n))return 8+r;}return 0;}

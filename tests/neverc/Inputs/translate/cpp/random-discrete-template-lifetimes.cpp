using Size=decltype(sizeof(0));
struct Arena {unsigned long long alignment;unsigned char bytes[1024];};
Arena arena{};Size offset;unsigned allocations,frees;
void* operator new(Size n){++allocations;auto p=arena.bytes+offset;offset+=(n+15)/16*16;return p;}
void operator delete(void*)noexcept{++frees;}
#include <random>
struct G{using result_type=unsigned;unsigned state,calls;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
template<class R> unsigned long long bits(R value){const auto *p=static_cast<const unsigned char*>(static_cast<const void*>(&value));unsigned long long result=0;for(unsigned i=0;i<sizeof(R);++i)result|=static_cast<unsigned long long>(p[i])<<(8*i);return result;}
unsigned calls,copies,constructions,destructions,wrong;unsigned long long deathOrder;
template<class T>struct Owner {Owner* self;unsigned tag;T state;
 Owner():self(this),tag(0),state(1){++constructions;}
 Owner(const Owner& other):self(this),tag(other.tag+1),state(other.state){++copies;}
 ~Owner(){++destructions;deathOrder=deathOrder*257+tag;}
 double operator()(double x){++calls;if(self!=this)++wrong;return x*x+static_cast<double>(state++ + tag);}
};
using Weight=Owner<unsigned>;
void reset(){offset=0;allocations=0;frees=0;calls=0;copies=0;constructions=0;destructions=0;wrong=0;deathOrder=0;}
using D=std::discrete_distribution<int>;
int properties(D& d,unsigned long long ph,unsigned long long h,unsigned long long next){auto p=d.probabilities();unsigned long long actual=0;for(double v:p)actual=actual*257+bits(v);if(actual!=ph)return 1;G e{17,0};actual=0;for(unsigned i=0;i<10003;++i)actual=actual*257+d(e);return actual!=h||e()!=next?2:0;}
int lvalue(unsigned n,unsigned long long ph,unsigned long long h,unsigned long long next,unsigned count,Size bytes){reset();{Weight w;D d(n,-3.,5.,w);if(int r=properties(d,ph,h,next))return r;if(w.state!=1||copies!=2||constructions!=1||destructions!=2||deathOrder!=515)return 3;}if(calls!=(n>1?n:0)||copies!=2||constructions!=1||destructions!=3||wrong||deathOrder!=132355)return 4;if(allocations!=count||frees!=count||offset!=bytes)return 5;return 0;}
int prvalue(unsigned n,unsigned long long ph,unsigned long long h,unsigned long long next,unsigned count,Size bytes){reset();{D d(n,-3.,5.,Weight{});if(int r=properties(d,ph,h,next))return r;}if(calls!=(n>1?n:0)||copies!=1||constructions!=1||destructions!=2||wrong||deathOrder!=257)return 4;if(allocations!=count||frees!=count||offset!=bytes)return 5;return 0;}
int timing(unsigned n){reset();unsigned observed=(D(n,-3.,5.,Weight{}),destructions);if(observed!=1||copies!=1||constructions!=1||destructions!=2||deathOrder!=257||wrong||allocations!=frees)return 1;reset();observed=(D::param_type(n,-3.,5.,Weight{}),destructions);if(observed!=0||copies||constructions!=1||destructions!=1||deathOrder||wrong||allocations!=frees)return 2;return 0;}
int main(){
if(int r=lvalue(0,4607182418800017408ULL,0ULL,2013693826ULL,1,16))return 0+r;
if(int r=lvalue(1,4607182418800017408ULL,0ULL,2013693826ULL,1,16))return 8+r;
if(int r=lvalue(3,7408387139118679597ULL,9583251001277050441ULL,2013693826ULL,4,96))return 16+r;
if(int r=prvalue(0,4607182418800017408ULL,0ULL,2013693826ULL,1,16))return 24+r;
if(int r=prvalue(1,4607182418800017408ULL,0ULL,2013693826ULL,1,16))return 32+r;
if(int r=prvalue(3,7209232518759830097ULL,9818719622499481655ULL,2013693826ULL,4,96))return 40+r;
for(unsigned n: {0u,1u,3u})if(int r=timing(n))return 80+r;return 0;}

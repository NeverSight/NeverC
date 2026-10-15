using Size=decltype(sizeof(0));
struct Arena {unsigned long long alignment;unsigned char bytes[65536];};
Arena arena{};Size offset;unsigned allocations,frees;
void* operator new(Size n){++allocations;auto p=arena.bytes+offset;offset+=(n+15)/16*16;return p;}
void operator delete(void*)noexcept{++frees;}
#include <random>
struct G{using result_type=unsigned;unsigned state,calls;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
template<class R> unsigned long long bits(R value){const auto *p=static_cast<const unsigned char*>(static_cast<const void*>(&value));unsigned long long result=0;for(unsigned i=0;i<sizeof(R);++i)result|=static_cast<unsigned long long>(p[i])<<(8*i);return result;}
unsigned funCalls,sameIdentity;
struct Identity {Identity* self;unsigned own;unsigned padding[8];Identity():self(this),own(1),padding{}{}double operator()(double x){++funCalls;const bool same=self==this;if(same)++sameIdentity;return x*x+static_cast<double>(own++ + (same?0:10));}};
using D=std::discrete_distribution<int>;
int properties(const D::param_type& p,unsigned long long ph,unsigned long long h,unsigned long long next){auto values=p.probabilities();unsigned long long actual=0;for(double v:values)actual=actual*257+bits(v);if(actual!=ph)return 1;G e{17,0};D d;actual=0;for(unsigned i=0;i<10003;++i)actual=actual*257+d(e,p);if(actual!=h||e()!=next)return 2;return 0;}
int prvalue(unsigned n,unsigned same,unsigned long long ph,unsigned long long h,unsigned long long next,unsigned count,Size bytes){offset=0;allocations=frees=funCalls=sameIdentity=0;Identity original;{D::param_type p(n,-3.,5.,Identity{});if(int r=properties(p,ph,h,next))return r;}if(funCalls!=(n>1?n:0)||sameIdentity!=same||original.own!=1)return 3;if(allocations!=count||frees!=count||offset!=bytes)return 4;return 0;}
int main(){
if(int r=prvalue(0,0,4607182418800017408ULL,0ULL,2013693826ULL,1,16))return 8+r;
if(int r=prvalue(1,0,4607182418800017408ULL,0ULL,2013693826ULL,1,16))return 24+r;
if(int r=prvalue(3,3,15873076950770661666ULL,13340493079067243979ULL,2013693826ULL,4,96))return 40+r;
return 0;}

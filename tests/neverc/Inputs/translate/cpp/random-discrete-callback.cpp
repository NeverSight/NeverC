using Size=decltype(sizeof(0));
struct Arena {unsigned long long alignment;unsigned char bytes[65536];};
Arena arena{};Size offset;unsigned allocations,frees;
void* operator new(Size n){++allocations;auto p=arena.bytes+offset;offset+=(n+15)/16*16;return p;}
void operator delete(void*)noexcept{++frees;}
#include <random>
struct G{using result_type=unsigned;unsigned state,calls;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
struct N{using result_type=unsigned short;unsigned state,calls;static constexpr unsigned short min(){return 13;}static constexpr unsigned short max(){return 37;}unsigned short operator()(){++calls;state=(state*13+7)%25;return static_cast<unsigned short>(13+state);}};
template<class E> E make(){return E(17);}template<> G make<G>(){return G{17,0};}template<> N make<N>(){return N{17,0};}
template<class R> unsigned long long bits(R value){const auto *p=static_cast<const unsigned char*>(static_cast<const void*>(&value));unsigned long long result=0;for(unsigned i=0;i<sizeof(R);++i)result|=static_cast<unsigned long long>(p[i])<<(8*i);return result;}
unsigned weightCalls;unsigned long long weightPoints;
double weight(double x){++weightCalls;weightPoints=weightPoints*257+bits(x);return x*x+1.;}
using Weight=double(*)(double);unsigned argumentCalls;
Weight callback(){++argumentCalls;return weight;}
double lower(){++argumentCalls;return -3.;}double upper(){++argumentCalls;return 5.;}
int check(unsigned n,unsigned calls,unsigned long long points,unsigned long long ph,unsigned long long h,unsigned long long next,unsigned allocationsExpected,Size bytes){
 offset=0;allocations=0;frees=0;weightCalls=0;weightPoints=0;
 {std::discrete_distribution<int> d(n,-3.,5.,weight);auto p=d.probabilities();
 unsigned long long actual=0;for(double v:p)actual=actual*257+bits(v);if(actual!=ph)return 1;
 std::mt19937 e(17);actual=0;for(unsigned i=0;i<10003;++i)actual=actual*257+d(e);
 if(actual!=h||e()!=next)return 2;}
 if(weightCalls!=calls||weightPoints!=points)return 3;
 if(allocations!=allocationsExpected||frees!=allocationsExpected||offset!=bytes)return 4;
 return 0;
}
int api(){
 using D=std::discrete_distribution<int>;using P=D::param_type;
 Weight f=weight;argumentCalls=0;weightCalls=0;
 P p(3,lower(),upper(),callback());if(argumentCalls!=3||weightCalls!=3)return 1;
 D d(p),explicitPointer(3,-3.,5.,f);if(d!=explicitPointer)return 2;
 auto probabilities=p.probabilities();if(probabilities.size()!=3)return 3;
 weightCalls=0;P empty(0,-3.,5.,f),single(1,-3.,5.,f);if(weightCalls!=0||empty!=single)return 4;
 D copy=explicitPointer;copy.param(p);std::mt19937 e(17),clone=e;
 if(copy(e,p)!=d.operator()(clone,p)||e()!=clone())return 5;
 return 0;
}
unsigned arithmeticCalls;unsigned long long arithmeticPoints;
float floatWeight(float x){++arithmeticCalls;arithmeticPoints=arithmeticPoints*257+bits(x);return x*x+1.f;}
int intWeight(int x){++arithmeticCalls;arithmeticPoints=arithmeticPoints*257+bits(x);return x*x+1;}
bool boolWeight(double x){++arithmeticCalls;arithmeticPoints=arithmeticPoints*257+bits(x);return x!=0.;}
template<class F>int arithmeticCheck(unsigned n,F f,unsigned long long points,unsigned long long ph,unsigned long long h,unsigned long long next,unsigned count,Size bytes){
 offset=0;allocations=0;frees=0;arithmeticCalls=0;arithmeticPoints=0;
 {std::discrete_distribution<short> d(n,-3.,5.,f);auto p=d.probabilities();unsigned long long actual=0;
 for(double v:p)actual=actual*257+bits(v);if(actual!=ph)return 1;
 std::mt19937 e(17);actual=0;for(unsigned i=0;i<10003;++i)actual=actual*257+d(e);if(actual!=h||e()!=next)return 2;}
 if(arithmeticCalls!=n||arithmeticPoints!=points)return 3;
 if(allocations!=count||frees!=count||offset!=bytes)return 4;return 0;}
int main(){
if(int r=check(0,0,0ULL,4607182418800017408ULL,0ULL,2276022928ULL,1,16))return 0+r;
if(int r=check(1,0,0ULL,4607182418800017408ULL,0ULL,2276022928ULL,1,16))return 8+r;
if(int r=check(2,2,17291570769289019392ULL,4973475188492817664ULL,14887778377278581060ULL,2276022928ULL,4,64))return 16+r;
if(int r=check(3,3,14985727760075346941ULL,17441105123949596008ULL,1997870797126677312ULL,2276022928ULL,4,96))return 24+r;
if(int r=check(17,17,8482000470655033693ULL,13755623134622359698ULL,3518643885968826746ULL,2276022928ULL,4,656))return 32+r;
if(int r=check(64,64,16086857868967411712ULL,16538865554101966448ULL,1084331979593374905ULL,2276022928ULL,4,2544))return 40+r;
if(int r=arithmeticCheck(3,floatWeight,212848911297024ULL,17441090315593662366ULL,1997870797126677312ULL,2276022928ULL,4,96))return 80+r;
if(int r=arithmeticCheck(3,intWeight,283678294867458ULL,12371041745646205952ULL,14409544392072852361ULL,2276022928ULL,4,96))return 88+r;
if(int r=arithmeticCheck(4,boolWeight,4616189618054758400ULL,7650114567021024256ULL,14613458500009530810ULL,2276022928ULL,4,144))return 96+r;
offset=0;allocations=0;frees=0;if(int r=api())return 64+r;if(allocations!=frees)return 70;return 0;}

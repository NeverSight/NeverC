using Size=decltype(sizeof(0));
double storage;bool heapEffects;
struct Arena {unsigned long long alignment;unsigned char bytes[65536];};
Arena arena{};Size offset;unsigned allocations,frees;
void* operator new(Size n){++allocations;if(heapEffects)storage+=10.;auto p=arena.bytes+offset;offset+=(n+15)/16*16;return p;}
void operator delete(void*)noexcept{++frees;if(heapEffects)storage+=100.;}
#include <random>
struct G{using result_type=unsigned;unsigned state,calls;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
struct N{using result_type=unsigned short;unsigned state,calls;static constexpr unsigned short min(){return 13;}static constexpr unsigned short max(){return 37;}unsigned short operator()(){++calls;state=(state*13+7)%25;return static_cast<unsigned short>(13+state);}};
template<class E> E make(){return E(17);}template<> G make<G>(){return G{17,0};}template<> N make<N>(){return N{17,0};}
template<class R> unsigned long long bits(R value){const auto *p=static_cast<const unsigned char*>(static_cast<const void*>(&value));unsigned long long result=0;for(unsigned i=0;i<sizeof(R);++i)result|=static_cast<unsigned long long>(p[i])<<(8*i);return result;}
unsigned calls;unsigned long long points;
double byConst(const double& x){++calls;points=points*257+bits(x);return x*x+1.;}
int byInt(const int& x){++calls;points=points*257+bits(x);return x*x+1;}
double byRvalue(double&& x){++calls;points=points*257+bits(x);x+=.25;return x*x+1.;}
double& byResult(double x){++calls;points=points*257+bits(x);storage=x*x+1.;return storage;}
const double& byBoth(const double& x){++calls;points=points*257+bits(x);storage=x*x+1.;return storage;}
template<class F>int check(unsigned n,F f,unsigned long long expectedPoints,unsigned long long expectedStorage,unsigned long long ph,unsigned long long h,unsigned long long next,unsigned count,Size bytes){
 offset=0;allocations=0;frees=0;calls=0;points=0;storage=0.;
 {std::discrete_distribution<int> d(n,-3.,5.,f);auto p=d.probabilities();unsigned long long actual=0;
 for(double v:p)actual=actual*257+bits(v);if(actual!=ph)return 1;
 std::mt19937 e(17);actual=0;for(unsigned i=0;i<10003;++i)actual=actual*257+d(e);if(actual!=h||e()!=next)return 2;}
 if(calls!=(n>1?n:0)||points!=expectedPoints||bits(storage)!=expectedStorage)return 3;
 if(allocations!=count||frees!=count||offset!=bytes)return 4;return 0;}
using Weight=const double&(*)(const double&);unsigned argumentCalls;
Weight callback(){++argumentCalls;return byBoth;}
int api(){using D=std::discrete_distribution<int>;using P=D::param_type;
 argumentCalls=0;P p(3,-3.,5.,callback());if(argumentCalls!=1)return 1;
 D d(p),copy(3,-3.,5.,byBoth);if(d!=copy)return 2;
 auto probabilities=p.probabilities();if(probabilities.size()!=3)return 3;
 std::mt19937 e(17),clone=e;if(d(e,p)!=copy.operator()(clone,p)||e()!=clone())return 4;return 0;}
int main(){
if(int r=check(0,byConst,0ULL,0ULL,4607182418800017408ULL,0ULL,2276022928ULL,1,16))return 0+r;
if(int r=check(1,byConst,0ULL,0ULL,4607182418800017408ULL,0ULL,2276022928ULL,1,16))return 8+r;
if(int r=check(3,byConst,14985727760075346941ULL,0ULL,17441105123949596008ULL,1997870797126677312ULL,2276022928ULL,4,96))return 16+r;
if(int r=check(3,byInt,283678294867458ULL,0ULL,12371041745646205952ULL,14409544392072852361ULL,2276022928ULL,4,96))return 24+r;
if(int r=check(17,byRvalue,8482000470655033693ULL,0ULL,15973813114545274568ULL,16082913852683312852ULL,2276022928ULL,4,656))return 32+r;
if(int r=check(3,byResult,14985727760075346941ULL,4624321117381955126ULL,17441105123949596008ULL,1997870797126677312ULL,2276022928ULL,4,96))return 40+r;
if(int r=check(17,byBoth,8482000470655033693ULL,4627364856403471434ULL,13755623134622359698ULL,3518643885968826746ULL,2276022928ULL,4,656))return 48+r;
offset=0;allocations=0;frees=0;if(int r=api())return 64+r;if(allocations!=frees)return 70;return 0;}

using Size=decltype(sizeof(0));
struct Arena {unsigned long long alignment;unsigned char bytes[1024];};
Arena arena{};Size offset;unsigned allocations,frees;
void* operator new(Size n){++allocations;auto p=arena.bytes+offset;offset+=(n+15)/16*16;return p;}
void operator delete(void*)noexcept{++frees;}
#include <random>
struct G{using result_type=unsigned;unsigned state,calls;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
template<class R> unsigned long long bits(R value){const auto *p=static_cast<const unsigned char*>(static_cast<const void*>(&value));unsigned long long result=0;for(unsigned i=0;i<sizeof(R);++i)result|=static_cast<unsigned long long>(p[i])<<(8*i);return result;}
unsigned calls;
template<class T,int Bias=1>struct Weight {T own;Weight():own(Bias){}double operator()(double x){++calls;return x*x+static_cast<double>(own++);}};
template<class T>struct Weight<T,2> {T own;Weight():own(2){}double operator()(double x){++calls;return x*x+static_cast<double>(2*own++);}};
template<>struct Weight<unsigned,3>{unsigned own;Weight():own(3){}double operator()(const double& x)const{++calls;return x*x+static_cast<double>(own);}};
using D=std::discrete_distribution<int>;
template<class F>int check(unsigned n,F f,unsigned long long ph,unsigned long long h,unsigned long long next,unsigned count,Size bytes){offset=0;allocations=frees=calls=0;auto before=f.own;{D d(n,-3.,5.,f);auto p=d.probabilities();unsigned long long actual=0;for(double v:p)actual=actual*257+bits(v);if(actual!=ph)return 1;G e{17,0};actual=0;for(unsigned i=0;i<10003;++i)actual=actual*257+d(e);if(actual!=h||e()!=next)return 2;}if(calls!=(n>1?n:0)||f.own!=before)return 3;if(allocations!=count||frees!=count||offset!=bytes)return 4;return 0;}
int main(){
if(int r=check(0,Weight<int>{},4607182418800017408ULL,0ULL,2013693826ULL,1,16))return 0+r;
if(int r=check(1,Weight<int>{},4607182418800017408ULL,0ULL,2013693826ULL,1,16))return 8+r;
if(int r=check(3,Weight<int>{},15873076950770661666ULL,13340493079067243979ULL,2013693826ULL,4,96))return 16+r;
if(int r=check(3,Weight<unsigned>{},15873076950770661666ULL,13340493079067243979ULL,2013693826ULL,4,96))return 24+r;
if(int r=check(17,Weight<short,2>{},18045483647320646384ULL,8181281820335157941ULL,2013693826ULL,4,656))return 32+r;
if(int r=check(3,Weight<unsigned,3>{},5725143777147125902ULL,15839693547950508614ULL,2013693826ULL,4,96))return 40+r;
return 0;}

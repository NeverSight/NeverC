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
template<class F>int effectsCheck(F f,unsigned long long expectedStorage,unsigned long long ph,unsigned long long h,unsigned long long next,unsigned count,Size bytes){
 offset=0;allocations=0;frees=0;calls=0;points=0;storage=0.;heapEffects=true;
 {std::discrete_distribution<int> d(3,-3.,5.,f);auto p=d.probabilities();unsigned long long actual=0;
 for(double v:p)actual=actual*257+bits(v);if(actual!=ph)return 1;
 std::mt19937 e(17);actual=0;for(unsigned i=0;i<10003;++i)actual=actual*257+d(e);if(actual!=h||e()!=next)return 2;}
 heapEffects=false;if(bits(storage)!=expectedStorage)return 3;
 if(allocations!=count||frees!=count||offset!=bytes)return 4;return 0;}
int main(){
if(int r=effectsCheck(byConst,4646448178051153920ULL,17441105123949596008ULL,1997870797126677312ULL,2276022928ULL,4,96))return 80+r;
if(int r=effectsCheck(byResult,4646526365544684658ULL,17315148437926495957ULL,11556459184173258823ULL,2276022928ULL,4,96))return 88+r;
if(int r=effectsCheck(byBoth,4646526365544684658ULL,17315148437926495957ULL,11556459184173258823ULL,2276022928ULL,4,96))return 96+r;
return 0;}

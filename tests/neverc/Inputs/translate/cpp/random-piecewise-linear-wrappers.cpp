using Size=decltype(sizeof(0));
struct Arena {unsigned long long alignment;unsigned char bytes[1024];};
Arena arena{};Size offset;unsigned allocations,frees;
void* operator new(Size n){++allocations;auto p=arena.bytes+offset;offset+=(n+15)/16*16;return p;}
void operator delete(void*)noexcept{++frees;}
#include <random>
struct G{using result_type=unsigned;unsigned state,calls;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
template<class R> unsigned long long bits(R value){const auto *p=static_cast<const unsigned char*>(static_cast<const void*>(&value));unsigned long long result=0;for(unsigned i=0;i<sizeof(R);++i)result|=static_cast<unsigned long long>(p[i])<<(8*i);return result;}
template<class R>int properties(std::piecewise_linear_distribution<R>& d,unsigned long long low,unsigned long long high,unsigned long long bh,unsigned long long dh,unsigned long long h,unsigned long long next){if(bits(d.min())!=low||bits(d.max())!=high)return 1;auto b=d.intervals();auto density=d.densities();unsigned long long actualB=0,actualD=0,actual=0;for(R v:b)actualB=actualB*257+bits(v);for(R v:density)actualD=actualD*257+bits(v);if(actualB!=bh||actualD!=dh)return 2;G e{17,0};for(unsigned i=0;i<10003;++i)actual=actual*257+bits(d(e));if(actual!=h||e()!=next)return 3;return 0;}
template<class R>int wrapper(unsigned long long low,unsigned long long high,unsigned long long bh,unsigned long long dh,unsigned long long h,unsigned long long next,unsigned count,Size bytes){offset=0;allocations=frees=0;{std::vector<int>b{-3,-1,2,5};const std::vector<short>w{1,0,4,2};std::piecewise_linear_distribution<R>d(b.begin(),b.end(),w.cbegin());if(int r=properties(d,low,high,bh,dh,h,next))return r;}if(allocations!=count||frees!=count||offset!=bytes)return 4;return 0;}
int main(){
if(int r=wrapper<float>(3225419776ULL,1084227584ULL,54962669649657856ULL,17514634940514304ULL,1182111340672980190ULL,1860345957ULL,7,112))return 0+r;
if(int r=wrapper<double>(13837309855095848960ULL,4617315517961601024ULL,17873661021126656000ULL,11475171850540023808ULL,7944735590530653230ULL,2013693826ULL,7,192))return 8+r;
return 0;}

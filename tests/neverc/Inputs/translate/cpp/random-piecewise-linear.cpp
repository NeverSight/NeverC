using Size=decltype(sizeof(0));
struct Arena {unsigned long long alignment;unsigned char bytes[1024];};
Arena arena{};Size offset;unsigned allocations,frees;
void* operator new(Size n){++allocations;auto p=arena.bytes+offset;offset+=(n+15)/16*16;return p;}
void operator delete(void*)noexcept{++frees;}
#include <random>
struct G{using result_type=unsigned;unsigned state,calls;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
template<class R> unsigned long long bits(R value){const auto *p=static_cast<const unsigned char*>(static_cast<const void*>(&value));unsigned long long result=0;for(unsigned i=0;i<sizeof(R);++i)result|=static_cast<unsigned long long>(p[i])<<(8*i);return result;}
template<class R>int properties(std::piecewise_linear_distribution<R>& d,unsigned long long low,unsigned long long high,unsigned long long bh,unsigned long long dh,unsigned long long h,unsigned long long next){if(bits(d.min())!=low||bits(d.max())!=high)return 1;auto b=d.intervals();auto density=d.densities();unsigned long long actualB=0,actualD=0,actual=0;for(R v:b)actualB=actualB*257+bits(v);for(R v:density)actualD=actualD*257+bits(v);if(actualB!=bh||actualD!=dh)return 2;G e{17,0};for(unsigned i=0;i<10003;++i)actual=actual*257+bits(d(e));if(actual!=h||e()!=next)return 3;return 0;}
template<class R>int check(bool defaults,unsigned n,unsigned long long low,unsigned long long high,unsigned long long bh,unsigned long long dh,unsigned long long h,unsigned long long next,unsigned count,Size bytes){offset=0;allocations=frees=0;R b[]={R(-3),R(-1),R(2),R(5)};R w[]={R(1),R(0),R(4),R(2)};if(defaults){std::piecewise_linear_distribution<R>d;if(int r=properties(d,low,high,bh,dh,h,next))return r;}else{std::piecewise_linear_distribution<R>d(b,b+n,w);if(int r=properties(d,low,high,bh,dh,h,next))return r;}if(allocations!=count||frees!=count||offset!=bytes)return 4;return 0;}
int main(){
if(int r=check<float>(true,0,0ULL,1065353216ULL,1065353216ULL,274861129728ULL,16862645768230927621ULL,1860345957ULL,5,80))return 0+r;
if(int r=check<float>(false,0,0ULL,1065353216ULL,1065353216ULL,274861129728ULL,16862645768230927621ULL,1860345957ULL,5,80))return 8+r;
if(int r=check<float>(false,1,0ULL,1065353216ULL,1065353216ULL,274861129728ULL,16862645768230927621ULL,1860345957ULL,6,96))return 16+r;
if(int r=check<float>(false,2,3225419776ULL,3212836864ULL,832145719296ULL,273795776512ULL,13528738683256794936ULL,1860345957ULL,5,80))return 24+r;
if(int r=check<float>(false,4,3225419776ULL,1084227584ULL,54962669649657856ULL,17514634940514304ULL,1182111340672980190ULL,1860345957ULL,5,80))return 32+r;
if(int r=check<double>(true,0,0ULL,4607182418800017408ULL,4607182418800017408ULL,8061443332993187840ULL,12522533668589908900ULL,2013693826ULL,5,80))return 40+r;
if(int r=check<double>(false,0,0ULL,4607182418800017408ULL,4607182418800017408ULL,8061443332993187840ULL,12522533668589908900ULL,2013693826ULL,5,80))return 48+r;
if(int r=check<double>(false,1,0ULL,4607182418800017408ULL,4607182418800017408ULL,8061443332993187840ULL,12522533668589908900ULL,2013693826ULL,6,96))return 56+r;
if(int r=check<double>(false,2,13837309855095848960ULL,13830554455654793216ULL,9797580989344514048ULL,3454260914193170432ULL,10345109398868301905ULL,2013693826ULL,5,80))return 64+r;
if(int r=check<double>(false,4,13837309855095848960ULL,4617315517961601024ULL,17873661021126656000ULL,11475171850540023808ULL,7944735590530653230ULL,2013693826ULL,5,160))return 72+r;
return 0;}

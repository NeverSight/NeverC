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
unsigned funCalls,wrongIdentity;
struct Stateful {unsigned own;double operator()(double x){++funCalls;return x*x+static_cast<double>(own++);}};
struct Identity {Identity* self;unsigned own;Identity():self(this),own(1){}double operator()(double x){++funCalls;if(self==this)++wrongIdentity;return x*x+static_cast<double>(own++);}};
struct Constant {double bias;double operator()(const double& x)const{++funCalls;return x*x+bias;}};
double returnedWeight;struct Reference {const double& operator()(const double& x)const;};
const double& Reference::operator()(const double& x)const{++funCalls;returnedWeight=x*x+2.;return returnedWeight;}
int properties(std::discrete_distribution<int>& d,unsigned long long ph,unsigned long long h,unsigned long long next){
 auto p=d.probabilities();unsigned long long actual=0;for(double v:p)actual=actual*257+bits(v);if(actual!=ph)return 1;
 std::mt19937 e(17);actual=0;for(unsigned i=0;i<10003;++i)actual=actual*257+d(e);if(actual!=h||e()!=next)return 2;return 0;}
template<class F>int check(unsigned n,F f,unsigned long long ph,unsigned long long h,unsigned long long next,unsigned count,Size bytes){
 offset=0;allocations=0;frees=0;funCalls=0;wrongIdentity=0;
 {std::discrete_distribution<int> d(n,-3.,5.,f);if(int r=properties(d,ph,h,next))return r;}
 if(funCalls!=(n>1?n:0)||wrongIdentity)return 3;if(allocations!=count||frees!=count||offset!=bytes)return 4;return 0;}
int prvalue(unsigned long long ph,unsigned long long h,unsigned long long next,unsigned count,Size bytes){
 offset=0;allocations=0;frees=0;funCalls=0;wrongIdentity=0;
 {std::discrete_distribution<int> d(3,-3.,5.,Identity{});if(int r=properties(d,ph,h,next))return r;}
 if(funCalls!=3||wrongIdentity)return 3;if(allocations!=count||frees!=count||offset!=bytes)return 4;return 0;}
int main(){Stateful state{1};Identity identity;
if(int result=check(0,state,4607182418800017408ULL,0ULL,2276022928ULL,1,16))return 0+result;
if(int result=check(1,state,4607182418800017408ULL,0ULL,2276022928ULL,1,16))return 8+result;
if(int result=check(3,state,15873076950770661666ULL,6535926658098385399ULL,2276022928ULL,4,96))return 16+result;
if(int result=check(3,identity,15873076950770661666ULL,6535926658098385399ULL,2276022928ULL,4,96))return 24+result;
if(int result=check(17,Constant{2.},3232486370877550585ULL,16237582902677155639ULL,2276022928ULL,4,656))return 32+result;
if(int result=prvalue(15873076950770661666ULL,6535926658098385399ULL,2276022928ULL,4,96))return 48+result;
if(int result=check(17,Reference{},3232486370877550585ULL,16237582902677155639ULL,2276022928ULL,4,656))return 64+result;
if(bits(returnedWeight)!=4627646331380182090ULL)return 70;
if(state.own!=1||identity.own!=1)return 60;return 0;}

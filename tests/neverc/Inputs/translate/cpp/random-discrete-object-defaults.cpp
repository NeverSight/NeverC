using Size=decltype(sizeof(0));
struct Arena {unsigned long long alignment;unsigned char bytes[1024];};
Arena arena{};Size offset;unsigned allocations,frees;
void* operator new(Size n){++allocations;auto p=arena.bytes+offset;offset+=(n+15)/16*16;return p;}
void operator delete(void*)noexcept{++frees;}
#include <random>
struct G{using result_type=unsigned;unsigned state,calls;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
template<class R> unsigned long long bits(R value){const auto *p=static_cast<const unsigned char*>(static_cast<const void*>(&value));unsigned long long result=0;for(unsigned i=0;i<sizeof(R);++i)result|=static_cast<unsigned long long>(p[i])<<(8*i);return result;}
unsigned calls,copies,constructions,destructions,wrong,tokenConstructions,tokenDestructions;unsigned long long deathOrder;
struct Token{unsigned serial;Token():serial(++tokenConstructions){}~Token(){++tokenDestructions;deathOrder=deathOrder*257+128+serial;}};
struct Weight{Weight* self;unsigned tag,state;Weight():self(this),tag(0),state(1){++constructions;}Weight(const Weight& other,const Token& token=Token{}):self(this),tag(other.tag+token.serial),state(other.state){++copies;}~Weight(){++destructions;deathOrder=deathOrder*257+1+tag;}double operator()(double x){++calls;if(self!=this)++wrong;return x*x+static_cast<double>(state++ + tag);}};
using D=std::discrete_distribution<int>;
void reset(){offset=0;allocations=frees=calls=copies=constructions=destructions=wrong=tokenConstructions=tokenDestructions=0;deathOrder=0;}
int properties(D& d,unsigned long long ph,unsigned long long h,unsigned long long next){auto p=d.probabilities();unsigned long long actual=0;for(double v:p)actual=actual*257+bits(v);if(actual!=ph)return 1;G e{17,0};actual=0;for(unsigned i=0;i<10003;++i)actual=actual*257+d(e);return actual!=h||e()!=next?2:0;}
int lvalue(unsigned n,unsigned expectedCopies,unsigned expectedDestructions,unsigned expectedTokens,unsigned long long order,unsigned long long ph,unsigned long long h,unsigned long long next,unsigned count,Size bytes){reset();{Weight w;D d(n,-3.,5.,w);if(int r=properties(d,ph,h,next))return r;}if(calls!=(n>1?n:0)||copies!=expectedCopies||constructions!=1||destructions!=expectedDestructions||wrong||tokenConstructions!=expectedTokens||tokenDestructions!=expectedTokens||deathOrder!=order)return 3;if(allocations!=count||frees!=count||offset!=bytes)return 4;return 0;}
int prvalue(unsigned n,unsigned expectedCopies,unsigned expectedDestructions,unsigned expectedTokens,unsigned long long order,unsigned long long ph,unsigned long long h,unsigned long long next,unsigned count,Size bytes){reset();{D d(n,-3.,5.,Weight{});if(int r=properties(d,ph,h,next))return r;}if(calls!=(n>1?n:0)||copies!=expectedCopies||constructions!=1||destructions!=expectedDestructions||wrong||tokenConstructions!=expectedTokens||tokenDestructions!=expectedTokens||deathOrder!=order)return 3;if(allocations!=count||frees!=count||offset!=bytes)return 4;return 0;}
int main(){
if(int r=lvalue(0,2,3,2,19656743946ULL,4607182418800017408ULL,0ULL,2013693826ULL,1,16))return 0+r;
if(int r=prvalue(0,1,2,1,165252ULL,4607182418800017408ULL,0ULL,2013693826ULL,1,16))return 8+r;
if(int r=lvalue(1,2,3,2,19656743946ULL,4607182418800017408ULL,0ULL,2013693826ULL,1,16))return 16+r;
if(int r=prvalue(1,1,2,1,165252ULL,4607182418800017408ULL,0ULL,2013693826ULL,1,16))return 24+r;
if(int r=lvalue(3,2,3,2,19656743946ULL,700635864787957375ULL,13961990924490876637ULL,2013693826ULL,4,96))return 32+r;
if(int r=prvalue(3,1,2,1,165252ULL,7209232518759830097ULL,9818719622499481655ULL,2013693826ULL,4,96))return 40+r;
return 0;}

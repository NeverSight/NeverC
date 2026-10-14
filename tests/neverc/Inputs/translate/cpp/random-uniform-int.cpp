// Pinned reference: LLVM 20.1.8 libc++.
#include <random>
#include <utility>
template<class E,class T> int check(T a,T b,unsigned long long expected,unsigned long long next){
 using D=std::uniform_int_distribution<T>;using P=typename D::param_type;
 E e(17);D d(a,b);if(d.a()!=a||d.b()!=b||d.min()!=a||d.max()!=b)return 1;
 unsigned long long digest=0;for(unsigned i=0;i<10003;++i)digest=digest*257+static_cast<unsigned long long>(d(e));
 if(digest!=expected||static_cast<unsigned long long>(e())!=next)return 2;
 P p(a,b);D from(p),copied(d);if(from!=d||copied!=d||d.param()!=p)return 3;
 D assigned;assigned=d;if(assigned!=d)return 4;
 assigned.reset();if(assigned!=d)return 5;
 P one(b,b);E before(e);if(d(e,one)!=b||before!=e||d.param()!=p)return 6;
 assigned.param(one);if(assigned.min()!=b||assigned.max()!=b)return 7;
 P moved(std::move(p));P replacement;replacement=std::move(moved);if(replacement!=d.param())return 8;
 assigned.param(replacement);E direct(e),ordinary(e);
 if(assigned.operator()(direct,replacement)!=d(ordinary)||direct!=ordinary)return 9;
 D defaulted;P defaultParam;if(defaulted.param()!=defaultParam||defaulted.a()!=0)return 10;
 D lowerOnly(a);P lowerParam(a);if(lowerOnly.param()!=lowerParam)return 11;
 return 0;
}
int main(){
 if(int r=check<std::minstd_rand,signed char>(-128,127,6255382951872891583ULL,1931632209ULL))return 0+r;
 if(int r=check<std::mt19937,unsigned char>(0,255,2549846920314179056ULL,1619580843ULL))return 16+r;
 if(int r=check<std::ranlux24,short>(-32768,32767,4127300003764508722ULL,10749435ULL))return 32+r;
 if(int r=check<std::knuth_b,unsigned short>(13,65535,11928195835428640196ULL,880368771ULL))return 48+r;
 if(int r=check<std::mt19937,int>(-1000000,-3,14548531550445485261ULL,3881256104ULL))return 64+r;
 if(int r=check<std::mt19937_64,int>(-500000,700000,9158552674535327338ULL,15277021189968718510ULL))return 80+r;
 if(int r=check<std::minstd_rand,int>((-2147483647-1),2147483647,1163488346279151024ULL,764354073ULL))return 96+r;
 if(int r=check<std::ranlux48,unsigned>(0,0xffffffffu,9753022292511093492ULL,63187190503770ULL))return 112+r;
 if(int r=check<std::mt19937,int>(-7,-7,9021848912348103035ULL,1265576559ULL))return 128+r;
 if(int r=check<std::mt19937_64,long long>((-9223372036854775807LL-1),-100000000000LL,1137318967705930820ULL,16283156750775118513ULL))return 144+r;
 if(int r=check<std::mt19937,long long>(-700000000000LL,900000000000LL,2812529937905443466ULL,1944021026ULL))return 160+r;
 if(int r=check<std::mt19937,long long>((-9223372036854775807LL-1),9223372036854775807LL,11492198984645093024ULL,2276022928ULL))return 176+r;
 if(int r=check<std::mt19937_64,unsigned long long>(0,0xffffffffffffffffULL,1137318967705930820ULL,16283156750775118513ULL))return 192+r;
 if(int r=check<std::minstd_rand,unsigned long long>(0xffffffffffff0000ULL,0xffffffffffffffffULL,15015560382072578623ULL,1931632209ULL))return 208+r;
 return 0;
}

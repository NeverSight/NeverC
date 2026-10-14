using Size = decltype(sizeof(0));
struct Arena { unsigned long long alignment; unsigned char bytes[65536]; };
Arena arena{}; Size offset; unsigned frees;
void *operator new(Size n) { auto p = arena.bytes + offset; offset += (n+15)/16*16; return p; }
void operator delete(void *) noexcept {++frees;}
#include <random>
template<class E> int check(unsigned long long expected,unsigned long long next){
 std::seed_seq q{1u,2u,0x12345678u,0xffffffffu,7u};E e(q),same;
 same.seed(q);if(e!=same)return 1;
 unsigned long long digest=0;for(unsigned i=0;i<10003;++i)digest=digest*257+e();
 if(digest!=expected || e()!=next)return 2;
 same.seed(q);E copied(same);if(copied!=same)return 3;
 return 0;
}
int main(){
 if(int r=check<std::minstd_rand>(10516926965029632678ULL,759496245ULL))return 0+r;
 if(int r=check<std::linear_congruential_engine<unsigned short,25173,13849,65521>>(8763974817266128590ULL,2941ULL))return 16+r;
 if(int r=check<std::linear_congruential_engine<unsigned long long,48271,7,9223372036854775783ULL>>(12003607771878839209ULL,7540865566600180306ULL))return 32+r;
 if(int r=check<std::mt19937>(2205271939796768955ULL,227286690ULL))return 48+r;
 if(int r=check<std::mt19937_64>(12983384292005046178ULL,10701747697713214982ULL))return 64+r;
 if(int r=check<std::ranlux24_base>(988778652776926067ULL,12200607ULL))return 80+r;
 if(int r=check<std::ranlux48_base>(5606247965430274653ULL,154537019903130ULL))return 96+r;
 if(int r=check<std::ranlux24>(1745832978546749518ULL,11307802ULL))return 112+r;
 if(int r=check<std::ranlux48>(12096807046370140430ULL,165825852266368ULL))return 128+r;
 if(int r=check<std::knuth_b>(8397073388552953475ULL,610110159ULL))return 144+r;
 if(int r=check<std::independent_bits_engine<std::ranlux48_base,64,unsigned long long>>(6199544746287731975ULL,4893816231848347632ULL))return 160+r;
 if(int r=check<std::shuffle_order_engine<std::mt19937_64,17>>(4138046671816135272ULL,12299821098190343817ULL))return 176+r;
 return frees==48?0:240;
}

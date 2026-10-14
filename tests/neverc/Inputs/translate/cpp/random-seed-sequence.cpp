using Size = decltype(sizeof(0));
struct Arena { unsigned long long alignment; unsigned char bytes[65536]; };
Arena arena{}; Size offset; unsigned frees;
void *operator new(Size n) { auto p = arena.bytes + offset; offset += (n+15)/16*16; return p; }
void operator delete(void *) noexcept {++frees;}
#include <random>
template<class U> int check(std::seed_seq &q,unsigned n,unsigned long long expected) {
 U values[700]{};q.generate(values,values+n);
 unsigned long long digest=0;for(unsigned i=0;i<n;++i)digest=digest*257+values[i];
 if(digest!=expected)return 1;
 q.generate(values,values+n);digest=0;for(unsigned i=0;i<n;++i)digest=digest*257+values[i];
 return digest==expected?0:2;
}
int main(){
 { std::seed_seq q{1u,2u,0x12345678u,0xffffffffu,7u}; std::seed_seq empty;
 if(int r=check<unsigned>(q,0,0ULL))return 0+r;
 if(int r=check<unsigned long long>(q,0,0ULL))return 3+r;
 if(int r=check<unsigned>(q,1,1983039877ULL))return 6+r;
 if(int r=check<unsigned long long>(q,1,1983039877ULL))return 9+r;
 if(int r=check<unsigned>(q,2,145383812802ULL))return 12+r;
 if(int r=check<unsigned long long>(q,2,145383812802ULL))return 15+r;
 if(int r=check<unsigned>(q,3,242962010850754ULL))return 18+r;
 if(int r=check<unsigned long long>(q,3,242962010850754ULL))return 21+r;
 if(int r=check<unsigned>(q,6,524243162258708875ULL))return 24+r;
 if(int r=check<unsigned long long>(q,6,524243162258708875ULL))return 27+r;
 if(int r=check<unsigned>(q,7,15291589704693089632ULL))return 30+r;
 if(int r=check<unsigned long long>(q,7,15291589704693089632ULL))return 33+r;
 if(int r=check<unsigned>(q,38,3549298646415197714ULL))return 36+r;
 if(int r=check<unsigned long long>(q,38,3549298646415197714ULL))return 39+r;
 if(int r=check<unsigned>(q,39,9139193867175830191ULL))return 42+r;
 if(int r=check<unsigned long long>(q,39,9139193867175830191ULL))return 45+r;
 if(int r=check<unsigned>(q,67,12159024908901055176ULL))return 48+r;
 if(int r=check<unsigned long long>(q,67,12159024908901055176ULL))return 51+r;
 if(int r=check<unsigned>(q,68,15820901142798431019ULL))return 54+r;
 if(int r=check<unsigned long long>(q,68,15820901142798431019ULL))return 57+r;
 if(int r=check<unsigned>(q,622,15983316756822394546ULL))return 60+r;
 if(int r=check<unsigned long long>(q,622,15983316756822394546ULL))return 63+r;
 if(int r=check<unsigned>(q,623,14092896524852239584ULL))return 66+r;
 if(int r=check<unsigned long long>(q,623,14092896524852239584ULL))return 69+r;
 if(int r=check<unsigned>(q,700,2470968862172574369ULL))return 72+r;
 if(int r=check<unsigned long long>(q,700,2470968862172574369ULL))return 75+r;
 if(int r=check<unsigned>(empty,0,0ULL))return 78+r;
 if(int r=check<unsigned long long>(empty,0,0ULL))return 81+r;
 if(int r=check<unsigned>(empty,1,3344521480ULL))return 84+r;
 if(int r=check<unsigned long long>(empty,1,3344521480ULL))return 87+r;
 if(int r=check<unsigned>(empty,2,113751343750ULL))return 90+r;
 if(int r=check<unsigned long long>(empty,2,113751343750ULL))return 93+r;
 if(int r=check<unsigned>(empty,3,120116760600753ULL))return 96+r;
 if(int r=check<unsigned long long>(empty,3,120116760600753ULL))return 99+r;
 if(int r=check<unsigned>(empty,6,17190151561934788581ULL))return 102+r;
 if(int r=check<unsigned long long>(empty,6,17190151561934788581ULL))return 105+r;
 if(int r=check<unsigned>(empty,7,7421774216777664014ULL))return 108+r;
 if(int r=check<unsigned long long>(empty,7,7421774216777664014ULL))return 111+r;
 if(int r=check<unsigned>(empty,38,4232853304580465385ULL))return 114+r;
 if(int r=check<unsigned long long>(empty,38,4232853304580465385ULL))return 117+r;
 if(int r=check<unsigned>(empty,39,12321836141887902820ULL))return 120+r;
 if(int r=check<unsigned long long>(empty,39,12321836141887902820ULL))return 123+r;
 if(int r=check<unsigned>(empty,67,9884566445170206763ULL))return 126+r;
 if(int r=check<unsigned long long>(empty,67,9884566445170206763ULL))return 129+r;
 if(int r=check<unsigned>(empty,68,2609144095229104163ULL))return 132+r;
 if(int r=check<unsigned long long>(empty,68,2609144095229104163ULL))return 135+r;
 if(int r=check<unsigned>(empty,622,1817330340679887974ULL))return 138+r;
 if(int r=check<unsigned long long>(empty,622,1817330340679887974ULL))return 141+r;
 if(int r=check<unsigned>(empty,623,3007095860250911867ULL))return 144+r;
 if(int r=check<unsigned long long>(empty,623,3007095860250911867ULL))return 147+r;
 if(int r=check<unsigned>(empty,700,14282124103743220367ULL))return 150+r;
 if(int r=check<unsigned long long>(empty,700,14282124103743220367ULL))return 153+r;
 } return frees==4?0:200;
}

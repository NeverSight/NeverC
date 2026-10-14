using Size = decltype(sizeof(0));
struct Arena { unsigned long long alignment; unsigned char bytes[2048]; };
Arena arena{}; Size offset;
void *operator new(Size) { auto p = arena.bytes + offset; offset += 256; return p; }
void operator delete(void *) noexcept {}
#include <algorithm>
#include <vector>
struct G {using result_type=unsigned;unsigned state,calls;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
int main(){
 int a[17]{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
 std::vector<int> b(8); G g{17,0};
 std::shuffle(a,a+17,g);
 unsigned long long digest=0;
 for(int i=0;i<17;++i)digest=digest*257+static_cast<unsigned>(a[i]+1);
 if(digest!=18293149044577076889ULL||g.state!=3092164965u||g.calls!=20)return 1;
 auto end=std::sample(a,a+17,b.begin(),8,g);
 int expected[8]{11,7,6,14,8,2,16,9};
 for(int i=0;i<8;++i)if(b[i]!=expected[i])return 2;
 return end==b.end()&&g.state==735758288u&&g.calls==37?0:3;
}

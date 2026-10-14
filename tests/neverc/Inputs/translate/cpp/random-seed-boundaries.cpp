using Size = decltype(sizeof(0));
struct Arena { unsigned long long alignment; unsigned char bytes[65536]; };
Arena arena{}; Size offset; unsigned allocations,frees;
void *operator new(Size n) {++allocations; auto p=arena.bytes+offset; offset+=(n+15)/16*16;return p;}
void operator delete(void *) noexcept {++frees;}
#include <random>
#include <vector>
#include <initializer_list>
int main(){
 {
  long long input[4]{-1,0x100000001LL,2,3};
  std::seed_seq range(input,input+4),literal{0xffffffffu,1u,2u,3u};
  std::initializer_list<long long> list{-1,0x100000001LL,2,3};
  std::seed_seq named(list);
  const auto &constant=range;unsigned long long params[4]{};constant.param(params);
  if(constant.size()!=4 || params[0]!=0xffffffffu || params[1]!=1 || params[2]!=2 || params[3]!=3)return 1;
  unsigned a[100]{},b[100]{},c[100]{};range.generate(a,a+100);literal.generate(b,b+100);named.generate(c,c+100);
  for(unsigned i=0;i<100;++i)if(a[i]!=b[i]||a[i]!=c[i])return 2;
  std::vector<long long> source{-1,0x100000001LL,2,3};const auto &view=source;
  std::seed_seq wrapped(view.begin(),view.end());
  std::vector<unsigned> result(100),copied(4);wrapped.generate(result.begin(),result.end());wrapped.param(copied.begin());
  for(unsigned i=0;i<100;++i)if(result[i]!=a[i])return 3;
  for(unsigned i=0;i<4;++i)if(copied[i]!=params[i])return 4;
  std::seed_seq nullRange(static_cast<const int*>(nullptr),static_cast<const int*>(nullptr));
  if(nullRange.size()!=0)return 5;
  nullRange.generate(static_cast<unsigned*>(nullptr),static_cast<unsigned*>(nullptr));
  std::seed_seq single{1};unsigned one[1]{};single.generate(one,one+1);
  if(single.size()!=1)return 6;
 }
 return allocations==frees?0:7;
}

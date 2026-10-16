#include <unordered_map>
#include <new>
#include <tuple>
using Map=std::unordered_map<int,int>;
using Size=std::size_t;
struct Slot{unsigned long long alignment;unsigned char bytes[32];Size size;bool used;};
struct Wide{unsigned long long alignment;unsigned char bytes[1024];Size size;bool used;};
Slot slots[48]{};Wide wide[4]{};unsigned allocations,deletions,effects;unsigned long long hash=1469598103934665603ULL;
void mix(unsigned long long value){hash=(hash^value)*1099511628211ULL;}
void*operator new(Size n){
 if(n<=32)for(unsigned i=0;i<48;++i)if(!slots[i].used){slots[i].used=true;slots[i].size=n;++allocations;mix(n*2+1);return slots[i].bytes;}
 if(n<=1024)for(unsigned i=0;i<4;++i)if(!wide[i].used){wide[i].used=true;wide[i].size=n;++allocations;mix(n*2+1);return wide[i].bytes;}
 for(;;){}
}
void operator delete(void*p)noexcept{
 for(unsigned i=0;i<48;++i)if(p==slots[i].bytes){mix(slots[i].size*2);slots[i].used=false;++deletions;return;}
 for(unsigned i=0;i<4;++i)if(p==wide[i].bytes){mix(wide[i].size*2);wide[i].used=false;++deletions;return;}
}
void operator delete(void*p,Size)noexcept{operator delete(p);}
void observe(const Map&m){mix(m.size());mix(m.bucket_count());mix(unsigned(m.max_load_factor()*100));for(auto it=m.begin();it!=m.end();++it){mix(unsigned(it->first));mix(unsigned(it->second));}for(Size i=0;i<m.bucket_count();++i)mix(m.bucket_size(i));}
int main(){
 {Map m;auto zero=m.emplace(std::piecewise_construct,std::tuple<>{},std::tuple<>{});if(!zero.second||zero.first->first!=0||zero.first->second!=0)return 1;
  unsigned a=allocations,d=deletions;auto duplicate=m.emplace(std::piecewise_construct_t{},std::tuple<>{},std::tuple<int>{19});
  if(duplicate.second||duplicate.first!=zero.first||zero.first->second!=0||allocations!=a+1||deletions!=d+1)return 2;
  int key=7,value=23;auto keys=std::tie(key);auto values=std::tie(value);
  auto refs=m.emplace(std::piecewise_construct,keys,values);if(!refs.second||refs.first->first!=7||refs.first->second!=23)return 3;
  key=9;value=41;auto hint=m.emplace_hint(m.cend(),std::piecewise_construct,std::forward_as_tuple(key),std::forward_as_tuple(value));if(hint->first!=9||hint->second!=41)return 4;
  const int fixed=11;const std::tuple<int> stored{51};auto mixed=m.emplace(std::piecewise_construct,std::tie(fixed),stored);if(!mixed.second||mixed.first->first!=11||mixed.first->second!=51)return 5;
  auto direct=m.emplace(std::piecewise_construct,std::make_tuple(13),std::make_tuple(61));if(!direct.second||direct.first->first!=13||direct.first->second!=61)return 6;
  key=17;value=71;auto rvalues=m.emplace(std::piecewise_construct,std::forward_as_tuple(static_cast<int&&>(key)),std::forward_as_tuple(static_cast<int&&>(value)));if(!rvalues.second||rvalues.first->first!=17||rvalues.first->second!=71||key!=17||value!=71)return 7;
  observe(m);
 }
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==14907254049489688332ULL?0:90;
}

#include <unordered_map>
#include <new>
#include <tuple>
using Map=std::unordered_map<int,int>;
using Size=std::size_t;
struct Slot{unsigned long long alignment;unsigned char bytes[32];Size size;bool used;};
struct Wide{unsigned long long alignment;unsigned char bytes[1024];Size size;bool used;};
Slot slots[48]{};Wide wide[4]{};unsigned allocations,deletions,effects;unsigned long long hash=1469598103934665603ULL;
void mix(unsigned long long value){hash=(hash^value)*1099511628211ULL;}
int*source_key;int*source_value;unsigned value_stage;
void*operator new(Size n){
 if(value_stage==1&&n==sizeof(void*)*2+sizeof(int)*2){*source_key=7;*source_value=43;value_stage=2;}else if(value_stage==2){*source_key=9;*source_value=59;value_stage=3;}
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
unsigned sequence;Map*receiver(Map*p){sequence=sequence*10+1;return p;}
Map::const_iterator hint(Map&m){sequence=sequence*10+2;return m.cend();}
int main(){
 {Map m;int key=5,value=17;source_key=&key;source_value=&value;value_stage=1;
  auto first=m.emplace(std::piecewise_construct,std::tie(key),std::tie(value));
  if(!first.second||first.first->first!=7||first.first->second!=43||key!=9||value!=59||value_stage!=3)return 1;value_stage=0;
  first.first->second=71;key=11;value=17;unsigned a=allocations,d=deletions;value_stage=1;
  auto duplicate=m.emplace(std::piecewise_construct,std::tie(key),std::tie(value));
  if(duplicate.second||duplicate.first!=first.first||first.first->second!=71||key!=7||value!=43||value_stage!=2||allocations!=a+1||deletions!=d+1)return 2;value_stage=0;
  m.reserve(17);std::tuple<int> keys{1},values{2};source_key=&std::get<0>(keys);source_value=&std::get<0>(values);value_stage=1;sequence=0;
  auto same=receiver(&m)->emplace_hint(hint(m),std::piecewise_construct,keys,values);
  if(sequence!=12||same!=first.first||same->second!=71||std::get<0>(keys)!=7||std::get<0>(values)!=43||value_stage!=2)return 3;value_stage=0;
  m.erase(7);std::get<0>(keys)=21;std::get<0>(values)=22;value_stage=1;auto inserted=m.emplace(std::piecewise_construct,keys,values);
  if(!inserted.second||inserted.first->first!=7||inserted.first->second!=43||value_stage!=2)return 4;value_stage=0;
  observe(m);
 }
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==18241245176907825010ULL?0:90;
}

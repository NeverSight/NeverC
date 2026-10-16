#include <unordered_map>
#include <new>
using Map=std::unordered_map<int,int>;
using Size=std::size_t;
struct Slot{unsigned long long alignment;unsigned char bytes[32];Size size;bool used;};
struct Wide{unsigned long long alignment;unsigned char bytes[1024];Size size;bool used;};
Slot slots[48]{};Wide wide[4]{};unsigned allocations,deletions,effects;unsigned long long hash=1469598103934665603ULL;
void mix(unsigned long long value){hash=(hash^value)*1099511628211ULL;}
Map*active;bool change_limit;int*source_value;unsigned value_stage;
void*operator new(Size n){
 if(value_stage==1&&n==sizeof(void*)*2+sizeof(int)*2){*source_value=43;value_stage=2;}else if(value_stage==2){*source_value=59;value_stage=3;}
 if(change_limit){change_limit=false;active->max_load_factor(.25f);}
 if(n<=32)for(unsigned i=0;i<48;++i)if(!slots[i].used){slots[i].used=true;slots[i].size=n;++allocations;mix(n*2+1);return slots[i].bytes;}
 if(n<=1024)for(unsigned i=0;i<4;++i)if(!wide[i].used){wide[i].used=true;wide[i].size=n;++allocations;mix(n*2+1);return wide[i].bytes;}
 for(;;){}
}
void operator delete(void*p)noexcept{
 for(unsigned i=0;i<48;++i)if(p==slots[i].bytes){mix(slots[i].size*2);slots[i].used=false;++deletions;return;}
 for(unsigned i=0;i<4;++i)if(p==wide[i].bytes){mix(wide[i].size*2);wide[i].used=false;++deletions;return;}
}
void operator delete(void*p,Size)noexcept{operator delete(p);}
Map*receiver(Map*p){++effects;return p;}
int key_argument(int k){effects=effects*10+2;return k;}
void observe(const Map&m){mix(m.size());mix(m.bucket_count());for(Size i=0;i<m.bucket_count();++i)mix(m.bucket_size(i));}
int main(){
 {Map m;active=&m;int key=4,value=17;source_value=&value;value_stage=1;
  auto result=m.try_emplace(key,value);
  if(!result.second||result.first->second!=43||value!=59||value_stage!=3)return 1;
  unsigned allocated=allocations,deleted=deletions;value=71;value_stage=1;
  auto duplicate=m.try_emplace(key,value);if(duplicate.second||duplicate.first!=result.first||value!=71||value_stage!=1)return 2;
  if(allocations!=allocated||deletions!=deleted)return 3;value_stage=0;
  auto assigned=m.insert_or_assign(key,value);if(assigned.second||assigned.first!=result.first||assigned.first->second!=71)return 4;
  effects=0;auto once=receiver(&m)->try_emplace(key_argument(5),value);if(effects!=12||!once.second||once.first->second!=71)return 5;
  m.reserve(17);Map::value_type pair{7,13};source_value=&pair.second;value_stage=1;
  auto copied=m.insert(pair);if(!copied.second||copied.first->second!=43||pair.second!=43||value_stage!=2)return 6;value_stage=0;
  unsigned a=allocations;effects=0;auto hint=receiver(&m)->emplace_hint(m.cend(),key_argument(8),88);
  if(effects!=12||hint->second!=88||allocations<a+1)return 7;
  value=91;source_value=&value;value_stage=1;auto fresh=m.insert_or_assign(9,value);
  if(!fresh.second||fresh.first->second!=43||value_stage!=2)return 8;value_stage=0;
  observe(m);for(auto it=m.begin();it!=m.end();++it){mix(unsigned(it->first));mix(unsigned(it->second));}
 }
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==6435396046558137918ULL?0:90;
}

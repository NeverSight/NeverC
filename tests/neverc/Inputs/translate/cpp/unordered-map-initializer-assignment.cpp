#include <unordered_map>
#include <new>
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
 {Map map(7);map.max_load_factor(0.75f);map[3]=31;map[7]=71;map[11]=111;
  std::initializer_list<Map::value_type> values={{1,11},{8,81}};
  Map&assigned=(map=values);
  if(&assigned!=&map||map.size()!=2||map[1]!=11||map[8]!=81||map.max_load_factor()!=0.75f)return 1;
  observe(map);
  map={{2,21},{9,91},{16,161},{23,231}};
  if(map.size()!=4||map[2]!=21||map[23]!=231||map.max_load_factor()!=0.75f)return 2;
  observe(map);map={};if(!map.empty()||map.bucket_count()!=7)return 3;
  observe(map);map={{5,51}};observe(map);
  Map fresh;Map&same=fresh.operator=({{4,41},{11,111},{18,181}});
  if(&same!=&fresh||fresh.size()!=3||fresh[4]!=41)return 4;
  observe(fresh);fresh={};observe(fresh);
 }

 if(allocations!=deletions)return 80;
 for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;
 for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==7098168454619317418ULL?0:90;
}

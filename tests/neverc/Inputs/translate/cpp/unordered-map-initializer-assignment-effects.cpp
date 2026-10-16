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
void deletion_observer();
void operator delete(void*p)noexcept{
 deletion_observer();
 for(unsigned i=0;i<48;++i)if(p==slots[i].bytes){mix(slots[i].size*2);slots[i].used=false;++deletions;return;}
 for(unsigned i=0;i<4;++i)if(p==wide[i].bytes){mix(wide[i].size*2);wide[i].used=false;++deletions;return;}
}
void operator delete(void*p,Size)noexcept{operator delete(p);}
void observe(const Map&m){mix(m.size());mix(m.bucket_count());mix(unsigned(m.max_load_factor()*100));for(auto it=m.begin();it!=m.end();++it){mix(unsigned(it->first));mix(unsigned(it->second));}for(Size i=0;i<m.bucket_count();++i)mix(m.bucket_size(i));}

using List=std::initializer_list<Map::value_type>;
Map*watched_map;const List*input_values;unsigned phase,observations,trace;
void deletion_observer(){if(watched_map){++observations;mix(phase);mix(watched_map->size());mix(watched_map->count(101));}}
Map*receiver(Map*p){++effects;trace=trace*10+1;return p;}
List argument(){++effects;trace=trace*10+2;return *input_values;}
int main(){
 {Map map(7);map[3]=31;map[7]=71;map[11]=111;map[15]=151;
  List values={{101,1011},{108,1081}};input_values=&values;
  watched_map=&map;phase=1;effects=trace=observations=0;
  Map&assigned=(*receiver(&map)=argument());
  if(&assigned!=&map||effects!=2||trace!=21||observations!=2||map.size()!=2||map[101]!=1011)return 1;
  mix(trace);effects=trace=0;
  Map&explicit_assignment=receiver(&map)->operator=(argument());
  if(&explicit_assignment!=&map||effects!=2||trace!=12||observations!=2||map.size()!=2)return 2;
  mix(trace);phase=2;map={};
  if(observations!=4||!map.empty())return 3;
  watched_map=nullptr;input_values=nullptr;observe(map);
 }

 if(allocations!=deletions)return 80;
 for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;
 for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==12147560493499963858ULL?0:90;
}

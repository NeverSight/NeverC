#include <unordered_map>
#include <new>
using Map=std::unordered_map<int,int>;
using Size=std::size_t;
struct Slot{unsigned long long alignment;unsigned char bytes[32];Size size;bool used;};
struct Wide{unsigned long long alignment;unsigned char bytes[1024];Size size;bool used;};
Slot slots[48]{};Wide wide[4]{};unsigned allocations,deletions,effects;unsigned long long hash=1469598103934665603ULL;
void mix(unsigned long long value){hash=(hash^value)*1099511628211ULL;}

int*live_value;int*live_key;unsigned hook_stage;
void*operator new(Size n){
 if(hook_stage==1&&n==7*sizeof(void*)){if(live_key)*live_key=9;*live_value=43;hook_stage=2;}
 else if(hook_stage==2&&n==2*sizeof(void*)+2*sizeof(int)){*live_value=59;hook_stage=3;}
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
using A=Map::allocator_type;using H=Map::hasher;using E=Map::key_equal;using P=std::pair<int,int>;using V=Map::value_type;using L=std::initializer_list<V>;
P input[2]={{7,17},{2,41}};unsigned trace;Size wanted=7;
P*first(){++effects;trace=trace*10+1;return input;}
P*last(){++effects;trace=trace*10+2;return input+2;}
Size count(){++effects;trace=trace*10+3;return wanted;}
A allocator(){++effects;trace=trace*10+4;wanted=11;input[0].second=37;return A{};}
Map*source_map;
Map::const_iterator map_first(){++effects;return source_map->cbegin();}
Map::const_iterator map_last(){++effects;return source_map->cend();}
L named_list={{1,31},{1,71},{2,41}};
L list_argument(){++effects;return named_list;}
int main(){
 {trace=effects=0;live_key=&input[0].first;live_value=&input[0].second;hook_stage=1;
  Map m(first(),last(),count(),allocator());
  if(effects!=4||trace!=1234||wanted!=11||hook_stage!=3||m.size()!=2||m.bucket_count()!=7||m.count(7)!=0||m[9]!=59)return 1;
  hook_stage=0;mix(trace);observe(m);
 }
 {Map source;source[1]=17;source[2]=41;source_map=&source;live_key=nullptr;
  auto first=source.cbegin();live_value=&source.find(first->first)->second;effects=0;hook_stage=1;
  Map m(map_first(),map_last(),7);
  if(effects!=2||hook_stage!=3||m.size()!=2||m.find(first->first)->second!=59)return 2;
  hook_stage=0;observe(m);observe(source);
 }
 {effects=0;Map m(list_argument(),7);if(effects!=1||m.size()!=2||m[1]!=31)return 3;observe(m);}
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==11760454430397605394ULL?0:90;
}

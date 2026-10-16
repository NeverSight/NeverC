#include <unordered_map>
#include <new>
using Map=std::unordered_map<int,int>;
using Size=std::size_t;
struct Slot{unsigned long long alignment;unsigned char bytes[32];Size size;bool used;};
struct Wide{unsigned long long alignment;unsigned char bytes[1024];Size size;bool used;};
Slot slots[48]{};Wide wide[4]{};unsigned allocations,deletions,effects;unsigned long long hash=1469598103934665603ULL;
void mix(unsigned long long value){hash=(hash^value)*1099511628211ULL;}
Map*active;bool change_limit;
void*operator new(Size n){
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
 {Map m,source;std::pair<int,int> input[]={{1,11},{2,22},{1,99},{-3,33},{4,44}};
  m.insert(input,input+5);if(m.size()!=4||m[1]!=11||m[-3]!=33)return 1;
  unsigned before=allocations;const std::pair<int,int>*empty=nullptr;m.insert(empty,empty);
  m.insert(input,input);if(allocations!=before||m.size()!=4)return 2;
  Map::value_type values[]={{5,55},{6,66},{2,77}};m.insert(values,values+3);
  if(m.size()!=6||m[5]!=55||m[6]!=66||m[2]!=22)return 3;
  source[7]=77;source[8]=88;source[1]=111;const Map&c=source;
  m.insert(source.begin(),source.end());if(m.size()!=8||m[7]!=77||m[8]!=88||m[1]!=11)return 4;
  before=allocations;m.insert(c.cbegin(),c.cend());if(allocations!=before||m.size()!=8)return 5;
  m.insert({{9,99},{10,100},{1,101}});if(m.size()!=10||m[9]!=99||m[10]!=100||m[1]!=11)return 6;
  before=allocations;m.insert({});if(allocations!=before||m.size()!=10)return 7;
  auto first=c.cbegin();m.insert(first,first);if(allocations!=before)return 8;
  for(auto it=m.begin();it!=m.end();++it){mix(unsigned(it->first));mix(unsigned(it->second));}observe(m);observe(source);
 }
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==13507316987936916267ULL?0:90;
}

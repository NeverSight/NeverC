#include <unordered_map>
#include <new>
using Map=std::unordered_map<int,int>;
using Size=std::size_t;
struct Slot{unsigned long long alignment;unsigned char bytes[32];Size size;bool used;};
struct Wide{unsigned long long alignment;unsigned char bytes[1024];Size size;bool used;};
Slot slots[48]{};Wide wide[4]{};unsigned allocations,deletions,effects;unsigned long long hash=1469598103934665603ULL;
void mix(unsigned long long value){hash=(hash^value)*1099511628211ULL;}
int*source_value;unsigned value_stage;
void*operator new(Size n){
 if(value_stage==1&&n==sizeof(void*)*2+sizeof(int)*2){*source_value=43;value_stage=2;}else if(value_stage==2){*source_value=59;value_stage=3;}
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
using A=Map::allocator_type;
Map*active;
A allocator_argument(){++effects;(*active)[7]=31;active->max_load_factor(.25f);return A{};}
A move_allocator(){++effects;(*active)[7]=61;active->max_load_factor(2.f);return A{};}
Map*receiver(Map*p){++effects;return p;}
int main(){
 {Map source;source[7]=17;active=&source;source_value=&source[7];value_stage=1;effects=0;
  Map copy(source,allocator_argument());if(effects!=1||copy[7]!=43||source[7]!=43||value_stage!=2||copy.max_load_factor()!=.5f)return 1;value_stage=0;observe(copy);observe(source);
  int*p=&source[7];unsigned a=allocations,d=deletions;effects=0;Map moved(static_cast<Map&&>(source),move_allocator());
  if(effects!=1||allocations!=a||deletions!=d||moved[7]!=61||&moved[7]!=p||moved.max_load_factor()!=2.f||source.size()!=0||source.bucket_count()!=0)return 2;observe(moved);observe(source);
  effects=0;A first=receiver(&moved)->get_allocator();A second=receiver(&moved)->get_allocator();if(effects!=2||first!=second||first!=A{})return 3;
 }
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==10583650831958226801ULL?0:90;
}

#include <unordered_map>
#include <new>
using Map=std::unordered_map<int,int>;
using Size=std::size_t;
struct Slot{unsigned long long alignment;unsigned char bytes[32];Size size;bool used;};
struct Wide{unsigned long long alignment;unsigned char bytes[1024];Size size;bool used;};
Slot slots[48]{};Wide wide[4]{};unsigned allocations,deletions,effects;unsigned long long hash=1469598103934665603ULL;
void mix(unsigned long long value){hash=(hash^value)*1099511628211ULL;}
Map*watched;Map*other;int*source_value;unsigned value_stage;bool watch_delete;
void*operator new(Size n){
 if(value_stage==1&&n==sizeof(void*)*2+sizeof(int)*2){*source_value=43;value_stage=2;}else if(value_stage==2){*source_value=59;value_stage=3;}
 if(n<=32)for(unsigned i=0;i<48;++i)if(!slots[i].used){slots[i].used=true;slots[i].size=n;++allocations;mix(n*2+1);return slots[i].bytes;}
 if(n<=1024)for(unsigned i=0;i<4;++i)if(!wide[i].used){wide[i].used=true;wide[i].size=n;++allocations;mix(n*2+1);return wide[i].bytes;}
 for(;;){}
}
void operator delete(void*p)noexcept{
 if(watch_delete){mix(watched->size());mix(watched->bucket_count());mix(unsigned(watched->max_load_factor()*100));mix(other->size());mix(other->bucket_count());}
 for(unsigned i=0;i<48;++i)if(p==slots[i].bytes){mix(slots[i].size*2);slots[i].used=false;++deletions;return;}
 for(unsigned i=0;i<4;++i)if(p==wide[i].bytes){mix(wide[i].size*2);wide[i].used=false;++deletions;return;}
}
void operator delete(void*p,Size)noexcept{operator delete(p);}
void observe(const Map&m){mix(m.size());mix(m.bucket_count());mix(unsigned(m.max_load_factor()*100));for(auto it=m.begin();it!=m.end();++it){mix(unsigned(it->first));mix(unsigned(it->second));}for(Size i=0;i<m.bucket_count();++i)mix(m.bucket_size(i));}
int main(){
 {Map source;source[7]=17;source_value=&source[7];Map target;value_stage=1;target=source;
  if(target[7]!=43||source[7]!=59||value_stage!=3)return 1;value_stage=0;observe(target);observe(source);
  Map copied_source;copied_source[11]=23;source_value=&copied_source[11];value_stage=1;Map copy(copied_source);
  if(copy[11]!=43||copied_source[11]!=43||value_stage!=2)return 2;value_stage=0;observe(copy);
  Map moved; moved.reserve(17);moved[1]=2;moved[9]=10;Map from;from[20]=21;from[28]=29;from.max_load_factor(2.f);
  watched=&moved;other=&from;watch_delete=true;moved=static_cast<Map&&>(from);watch_delete=false;
  if(moved.size()!=2||moved[20]!=21||moved[28]!=29||moved.max_load_factor()!=2.f||from.size()!=0||from.bucket_count()!=0)return 3;observe(moved);observe(from);
  Map assign_target;assign_target.reserve(17);for(int i=0;i<5;++i)assign_target[i]=i+1;
  watched=&assign_target;other=&source;watch_delete=true;assign_target=source;watch_delete=false;
  if(assign_target.size()!=1||assign_target[7]!=59||source[7]!=59)return 4;observe(assign_target);
 }
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==9152292349188943039ULL?0:90;
}

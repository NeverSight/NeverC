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
using N=Map::node_type;N*watched_handle;unsigned observed_delete;
void operator delete(void*p)noexcept{
 if(watched_handle){if(!watched_handle->empty())++observed_delete;mix(unsigned(watched_handle->empty()));}
 for(unsigned i=0;i<48;++i)if(p==slots[i].bytes){mix(slots[i].size*2);slots[i].used=false;++deletions;return;}
 for(unsigned i=0;i<4;++i)if(p==wide[i].bytes){mix(wide[i].size*2);wide[i].used=false;++deletions;return;}
}
void operator delete(void*p,Size)noexcept{operator delete(p);}
void observe(const Map&m){mix(m.size());mix(m.bucket_count());mix(unsigned(m.max_load_factor()*100));for(auto it=m.begin();it!=m.end();++it){mix(unsigned(it->first));mix(unsigned(it->second));}for(Size i=0;i<m.bucket_count();++i)mix(m.bucket_size(i));}
unsigned trace;Map*active;
Map*receiver(Map*p){++effects;trace=trace*10+1;return p;}
int key_argument(){++effects;trace=trace*10+2;(*active)[1]=43;return 1;}
int main(){
 {Map m(7);m[1]=11;m[8]=21;m[2]=31;active=&m;
  effects=trace=0;unsigned a=allocations,d=deletions;N extracted=receiver(&m)->extract(key_argument());
  if(effects!=2||trace!=12||!extracted||extracted.key()!=1||extracted.mapped()!=43||allocations!=a||deletions!=d)return 1;
  mix(trace);
  {N target=m.extract(8);N source=m.extract(2);watched_handle=&target;observed_delete=0;d=deletions;
   target=static_cast<N&&>(source);if(source||!target||target.key()!=2||observed_delete!=1||deletions!=d+1)return 2;
   watched_handle=nullptr;
  }
  {N doomed=static_cast<N&&>(extracted);watched_handle=&doomed;observed_delete=0;}
  watched_handle=nullptr;if(observed_delete!=1||extracted)return 3;
  observe(m);
 }
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==9004411389003085914ULL?0:90;
}

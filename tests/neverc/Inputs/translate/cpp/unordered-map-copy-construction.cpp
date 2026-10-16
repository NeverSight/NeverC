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
Map copied(const Map&m){return Map(m);}
Map moved(Map&&m){return Map(static_cast<Map&&>(m));}
int value_parameter(Map m){return m[7];}
struct Holder{Map value;Holder(const Map&m):value(m){}};
int main(){
 {Map empty;empty.reserve(17);empty.max_load_factor(.5f);unsigned a=allocations;
  const Map copied(empty);if(copied.size()!=0||copied.bucket_count()!=empty.bucket_count()||copied.max_load_factor()!=.5f||allocations!=a+1)return 1;observe(copied);}
 {Map source;source.rehash(8);source.max_load_factor(2.f);
  for(int i=-5;i<7;++i)source[i*8]=i+31;
  Map copy(source);if(copy.size()!=source.size()||copy.bucket_count()!=source.bucket_count()||copy.max_load_factor()!=source.max_load_factor())return 2;
  for(auto it=source.begin();it!=source.end();++it){auto found=copy.find(it->first);if(found==copy.end()||found->second!=it->second||&found->second==&it->second)return 3;}
  copy[-40]=99;if(source[-40]!=26)return 4;observe(source);observe(copy);
  Map zeros[2]={source,copy};if(zeros[0].size()!=source.size()||zeros[1][-40]!=99)return 5;observe(zeros[0]);observe(zeros[1]);
 }
 {Map source;source[7]=41;Holder owner(source);Map a=copied(source);Map b=moved(static_cast<Map&&>(a));if(owner.value[7]!=41||b[7]!=41||value_parameter(source)!=41||a.size()!=0||a.bucket_count()!=0)return 6;observe(b);}
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==15116551699060599586ULL?0:90;
}

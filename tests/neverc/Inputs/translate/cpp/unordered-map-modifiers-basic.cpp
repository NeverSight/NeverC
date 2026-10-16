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
 {Map m;const Map&c=m;
  Size expected_max=Size(-1)/(sizeof(void*)*2+sizeof(int)*2);
  if(c.max_size()!=expected_max||c.max_bucket_count()!=expected_max)return 1;
  m.reserve(13);if(m.bucket_count()!=13||!m.empty())return 2;observe(m);
  for(int k=-15;k<=15;++k)m[k]=k*7;
  if(m.size()!=31)return 3;observe(m);
  for(int k=-15;k<=15;++k)if(c.count(k)!=1||m[k]!=k*7)return 4;
  Size before=m.size();int key=7;int&reference=m[key];if(reference!=49||m.size()!=before)return 5;
  reference=52;if(m.operator[](key)!=52||m.operator[](7)!=52)return 6;
  effects=0;receiver(&m)->operator[](key_argument(100))=81;if(effects!=12||m[100]!=81)return 7;
  if(c.bucket(100)>=m.bucket_count())return 8;
  for(int k=-15;k<=15;k+=2)if(m.erase(k)!=1||m.erase(k)!=0||c.count(k)!=0)return 9;observe(m);
  Size buckets=m.bucket_count();m.clear();if(!m.empty()||m.size()||m.load_factor()!=0.f||m.bucket_count()!=buckets)return 10;
  m.clear();observe(m);m[-1]=19;if(m[-1]!=19||m.size()!=1)return 11;observe(m);
 }
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==13093851379686410598ULL?0:90;
}

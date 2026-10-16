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
 {Map m;active=&m;change_limit=true;int k=4;
  effects=0;int&value=receiver(&m)->operator[](key_argument(k));value=17;
  if(effects!=12||change_limit||m.max_load_factor()!=.25f||m.bucket_count()!=4||m[k]!=17)return 1;observe(m);
  unsigned a=allocations,d=deletions;int*stable=&m[k];m[k]=19;
  if(allocations!=a||deletions!=d||&m[k]!=stable||*stable!=19)return 2;
  if(m.erase(99)!=0||allocations!=a||deletions!=d)return 3;
  if(m.erase(k)!=1||deletions!=d+1||!m.empty()||m.bucket_count()!=4)return 4;observe(m);
  m[k]=23;if(m[k]!=23||m.bucket_count()!=4)return 5;
  Map other;other.max_load_factor(2.f);effects=0;receiver(&m)->swap(*receiver(&other));
  if(effects!=2||m.max_load_factor()!=2.f||other.max_load_factor()!=.25f||other[k]!=23)return 6;observe(m);observe(other);
 }
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==9607971705541199710ULL?0:90;
}

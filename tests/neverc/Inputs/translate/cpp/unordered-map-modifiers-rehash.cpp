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
 {Map m,other;m[1]=11;m[-7]=22;int*stable=&m[1];
  Size requests[]={0,1,2,3,4,8,9,31,30,7,64,67,6,0};
  for(Size n:requests){m.rehash(n);if(&m[1]!=stable||*stable!=11||m[-7]!=22||m.size()!=2)return 1;observe(m);}
  m.rehash(16);m.max_load_factor(.25f);m.reserve(25);if(m.bucket_count()<100||&m[1]!=stable)return 2;observe(m);
  for(int k=2;k<22;++k)m[k]=k+30;observe(m);
  float current=m.load_factor();m.max_load_factor(.0001f);if(m.max_load_factor()!=current)return 3;
  m.rehash(0);if(&m[1]!=stable||*stable!=11)return 4;observe(m);
  other[81]=72;int*other_stable=&other[81];m.swap(other);
  if(&other[1]!=stable||&m[81]!=other_stable||m.size()!=1||other.size()!=22)return 5;observe(m);observe(other);
  m.swap(m);if(&m[81]!=other_stable||m[81]!=72)return 6;
  Map empty;m.swap(empty);if(!m.empty()||empty[81]!=72||&empty[81]!=other_stable)return 7;observe(empty);observe(m);
  other.clear();other.rehash(0);if(other.bucket_count()!=0||!other.empty())return 8;observe(other);
 }
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==3148771317059825357ULL?0:90;
}

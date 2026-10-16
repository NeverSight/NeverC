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
using A=Map::allocator_type;
unsigned calls;Map*receiver(Map*p){++calls;return p;}
A make_allocator(){++calls;return A{};}
int main(){
 {Map source;source.reserve(17);source.max_load_factor(.5f);for(int i=0;i<7;++i)source[i*8]=i+21;
  const Map&view=source;calls=0;A allocator=receiver(&source)->get_allocator();if(calls!=1||allocator!=view.get_allocator()||allocator!=A{})return 1;
  Map empty(allocator);if(!empty.empty()||empty.bucket_count()!=0||empty.max_load_factor()!=1.f)return 2;observe(empty);
  unsigned a=allocations;Map copy(source,allocator);if(copy.size()!=source.size()||copy.bucket_count()!=source.bucket_count()||copy.max_load_factor()!=.5f||allocations<=a)return 3;
  for(auto it=source.begin();it!=source.end();++it){auto found=copy.find(it->first);if(found==copy.end()||found->second!=it->second||&found->second==&it->second)return 4;}observe(copy);
  auto*p=&copy.find(16)->second;a=allocations;unsigned d=deletions;Map moved(static_cast<Map&&>(copy),allocator);
  if(allocations!=a||deletions!=d||copy.size()!=0||copy.bucket_count()!=0||copy.max_load_factor()!=.5f||&moved.find(16)->second!=p)return 5;observe(moved);observe(copy);
  calls=0;Map explicit_empty(make_allocator());if(calls!=1||explicit_empty.size()!=0||explicit_empty.bucket_count()!=0)return 6;
 }
 {A a;Map source(a);source.reserve(17);source.max_load_factor(3.f);unsigned n=allocations,d=deletions;Map moved(static_cast<Map&&>(source),a);if(allocations!=n||deletions!=d||moved.bucket_count()!=17||moved.max_load_factor()!=3.f||source.bucket_count()!=0||source.max_load_factor()!=3.f)return 7;observe(moved);}
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==3285900450422909849ULL?0:90;
}

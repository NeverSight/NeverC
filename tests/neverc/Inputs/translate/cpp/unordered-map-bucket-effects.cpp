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
using A=Map::allocator_type;using H=Map::hasher;using E=Map::key_equal;
Size wanted=7;unsigned trace;
Size count_argument(){trace=trace*10+1;++effects;return wanted;}
H hash_argument(){trace=trace*10+2;++effects;wanted=11;return H{};}
E equal_argument(){trace=trace*10+3;++effects;wanted=13;return E{};}
A allocator_argument(){trace=trace*10+4;++effects;wanted=17;return A{};}
int main(){
 {wanted=7;trace=effects=0;unsigned n=allocations;Map m(count_argument(),hash_argument(),equal_argument(),allocator_argument());
  if(trace!=1234||effects!=4||wanted!=17||m.bucket_count()!=7||allocations!=n+1||m.max_load_factor()!=1.f)return 1;
  mix(trace);observe(m);m[1]=31;observe(m);
 }
 {wanted=8;trace=effects=0;Map m(count_argument(),allocator_argument());if(trace!=14||effects!=2||m.bucket_count()!=8)return 2;mix(trace);observe(m);}
 {wanted=3;trace=effects=0;Map m(count_argument(),hash_argument(),allocator_argument());if(trace!=124||effects!=3||m.bucket_count()!=3)return 3;mix(trace);observe(m);}
 {wanted=0;trace=effects=0;unsigned n=allocations;Map m(count_argument());if(trace!=1||effects!=1||allocations!=n||m.bucket_count()!=0)return 4;mix(trace);observe(m);}
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==5932060772833200507ULL?0:90;
}

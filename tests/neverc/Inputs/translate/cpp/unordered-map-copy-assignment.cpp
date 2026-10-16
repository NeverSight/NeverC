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
unsigned order;Map*left(Map*p){order=order*10+1;return p;}const Map&right(const Map&p){order=order*10+2;return p;}
int main(){
 {Map source;source.rehash(8);source.max_load_factor(2.f);for(int i=0;i<9;++i)source[i*8]=i+20;
  Map target;target.rehash(8);for(int i=0;i<12;++i)target[100+i*8]=i;
  int*saved[12];unsigned n=0;for(auto it=target.begin();it!=target.end();++it)saved[n++]=&it->second;
  unsigned a=allocations,d=deletions;order=0;Map&alias=(*left(&target)=right(source));
  if(order!=21||&alias!=&target||allocations!=a||deletions!=d+3||target.size()!=9||target.max_load_factor()!=2.f)return 1;
  unsigned reused=0;for(auto it=target.begin();it!=target.end();++it){auto found=source.find(it->first);if(found==source.end()||found->second!=it->second||&found->second==&it->second)return 2;for(unsigned i=0;i<12;++i)if(&it->second==saved[i])++reused;}
  if(reused!=9)return 3;observe(target);observe(source);
  a=allocations;d=deletions;target=target;if(allocations!=a||deletions!=d||target.size()!=9)return 4;
  Map small;small[71]=72;a=allocations;small=source;if(small.size()!=9||small.find(64)->second!=28||allocations<=a)return 5;observe(small);
  Map blank;Size count=target.bucket_count();a=allocations;d=deletions;target.operator=(blank);if(!target.empty()||target.bucket_count()!=count||allocations!=a||deletions!=d+9||target.max_load_factor()!=1.f)return 6;observe(target);
  blank=source;if(blank.size()!=9||blank[64]!=28)return 7;observe(blank);
 }
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==375446344480745071ULL?0:90;
}

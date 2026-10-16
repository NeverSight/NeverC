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
using H=Map::hasher;using E=Map::key_equal;
Map*receiver(Map*p){++effects;return p;}
Map&owner(Map*p){++effects;(*p)[1]+=1;return *p;}
H hash_value(const Map&m){++effects;return m.hash_function();}
E equal_value(const Map&m){++effects;return m.key_eq();}
int main(){
 {Map m(7);m[1]=11;m[8]=21;const Map&view=m;unsigned a=allocations,d=deletions;
  H h=m.hash_function();const H ch=view.hash_function();E eq=m.key_eq();const E ce=view.key_eq();
  if(h(0)!=0||h(-7)!=Size(-7)||ch(29)!=29||!eq(1,1)||eq(1,2)||!ce(-9,-9)||ce(-9,9))return 1;
  H copied(h);copied=ch;E ec(eq);ec=ce;if(copied(71)!=71||!ec(3,3))return 2;
  effects=0;auto hr=receiver(&m)->hash_function();auto er=receiver(&m)->key_eq();if(effects!=2||hr(17)!=17||!er(2,2))return 3;
  effects=0;if(owner(&m).hash_function()(5)!=5||effects!=1||m[1]!=12)return 4;
  effects=0;H hs[2]={hash_value(view),hash_value(view)};E es[2]={equal_value(view),equal_value(view)};
  if(effects!=4||hs[0](31)!=31||hs[1](-1)!=Size(-1)||!es[0](7,7)||es[1](7,8))return 5;
  if(allocations!=a||deletions!=d||m.size()!=2||m.bucket_count()!=7)return 6;
  mix(h(-7));mix(ch(29));mix(eq(1,2));mix(ce(-9,-9));mix(effects);observe(m);
 }
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==12440267977761876040ULL?0:90;
}

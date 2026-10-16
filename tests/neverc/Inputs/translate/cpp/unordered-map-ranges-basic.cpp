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
  auto absent=m.equal_range(1);auto ca=c.equal_range(1);if(absent.first!=m.end()||absent.second!=m.end()||ca.first!=c.end()||ca.second!=c.end())return 1;
  for(int k=-7;k<=7;++k)m[k]=k*9;
  unsigned allocations_before=allocations,deletions_before=deletions;
  for(int k=-9;k<=9;++k){
   auto range=m.equal_range(k);auto cr=c.equal_range(k);
   if(k>=-7&&k<=7){auto it=m.find(k);auto next=it;++next;if(range.first!=it||range.second!=next||cr.first!=it||cr.second!=next||range.first->second!=k*9)return 2;range.first->second+=1;mix(unsigned(range.first->first));}
   else if(range.first!=m.end()||range.second!=m.end()||cr.first!=c.end()||cr.second!=c.end())return 3;
  }
  if(allocations!=allocations_before||deletions!=deletions_before)return 4;
  auto found=m.equal_range(3);auto after=found.second;auto erased=m.erase(found.first,found.second);if(erased!=after||m.count(3)!=0)return 5;
  for(auto it=m.begin();it!=m.end();++it){mix(unsigned(it->first));mix(unsigned(it->second));}observe(m);
 }
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==6165415705018848639ULL?0:90;
}

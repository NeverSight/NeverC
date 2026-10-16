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
void observe(const Map&m){mix(m.size());mix(m.bucket_count());mix(unsigned(m.max_load_factor()*100));for(auto it=m.begin();it!=m.end();++it){mix(unsigned(it->first));mix(unsigned(it->second));}}
int main(){
 {Map m;auto first=m.emplace();if(!first.second||first.first->first!=0||first.first->second!=0||m.size()!=1)return 1;
  auto p=&first.first->second;*p=19;unsigned a=allocations,d=deletions;auto duplicate=m.emplace();
  if(duplicate.second||duplicate.first!=first.first||&duplicate.first->second!=p||*p!=19||allocations!=a+1||deletions!=d+1)return 2;
  a=allocations;d=deletions;auto hinted=m.emplace_hint(m.cend());if(hinted!=first.first||*p!=19||allocations!=a+1||deletions!=d+1)return 3;observe(m);
  m.clear();auto empty=m.emplace_hint(m.cbegin());if(empty->first!=0||empty->second!=0||m.size()!=1)return 4;observe(m);
  m.erase(0);m[8]=18;m[16]=26;auto collision=m.emplace();if(!collision.second||collision.first->first!=0||collision.first->second!=0||m.size()!=3)return 5;observe(m);
 }
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==7328543925798695179ULL?0:90;
}

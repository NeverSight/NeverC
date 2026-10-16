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
  Map::iterator empty{},copy(empty);Map::const_iterator ce(empty),cc(ce);
  if(empty!=copy||ce!=cc||empty!=ce||ce!=empty||empty!=m.end())return 1;
  if(m.begin()!=m.end()||c.begin()!=c.end()||c.cbegin()!=c.cend())return 2;
  for(int k=-9;k<=9;++k)m[k]=k*13;
  unsigned count=0;int sum=0;
  for(auto it=m.begin();it!=m.end();++it){
   const int*key=&it->first;int*value=&it->second;
   if(*value!=*key*13||&(*it).second!=value||it.operator->()!=&*it)return 3;
   *value+=2;sum+=*value;++count;mix(unsigned(*key));mix(unsigned(*value));
  }
  if(count!=19||sum!=38)return 4;
  count=0;for(auto it=c.cbegin();it!=c.cend();it++){
   const auto&value=*it;Map::const_iterator stable(it),again{};again=stable;
   if(stable!=it||again!=stable||value.second!=value.first*13+2)return 5;
   ++count;mix(unsigned(value.first));mix(unsigned(value.second));
  }
  if(count!=19)return 6;
  int range_sum=0;for(auto&value:m){value.second+=1;range_sum+=value.second;}
  if(range_sum!=57)return 7;
  range_sum=0;for(const auto&value:c)range_sum+=value.second;if(range_sum!=57)return 8;
  for(int k=-9;k<=9;++k){
   auto it=m.find(k);auto ci=c.find(k);Map::const_iterator converted(it);
   if(it==m.end()||ci==c.end()||it!=ci||ci!=it||converted!=ci||it->second!=k*13+3)return 9;
  }
  if(m.find(100)!=m.end()||c.find(-100)!=c.end())return 10;
  auto old=m.begin();auto next=old;auto previous=next++;
  if(previous!=old||&++old!=&old||old!=next)return 11;
  auto named=next;auto named_old=named.operator++(7);if(named_old!=next)return 12;
  if(operator==(old,next)!=true||operator!=(old,next)!=false)return 13;
  observe(m);
 }
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==15430202511744716137ULL?0:90;
}

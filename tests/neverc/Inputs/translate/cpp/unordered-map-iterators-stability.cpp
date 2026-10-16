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
 {Map m,other;m.reserve(31);for(int k=0;k<11;++k)m[k]=k*5;
  auto held=m.find(3);auto*pair=&*held;int*value=&held->second;auto end=m.end();
  m[20]=91;if(&*held!=pair||&held->second!=value||*value!=15||m.end()!=end)return 1;
  if(m.erase(9)!=1||held!=m.find(3)||&*held!=pair)return 2;
  other[-2]=31;auto other_held=other.find(-2);auto*other_pair=&*other_held;
  m.swap(other);
  if(held!=other.find(3)||other_held!=m.find(-2)||&*held!=pair||&*other_held!=other_pair)return 3;
  held->second=19;if(other[3]!=19||*value!=19)return 4;
  Map::const_iterator converted(held);const Map&c=other;
  if(converted!=c.find(3)||&*converted!=pair)return 5;
  other.rehash(67);if(&*other.find(3)!=pair||&other.find(3)->second!=value||*value!=19)return 6;
  auto fresh=other.find(3);other.swap(other);if(fresh!=other.find(3)||&*fresh!=pair)return 7;
  unsigned count=0;for(auto it=other.begin();it!=other.end();++it){++count;mix(unsigned(it->first));mix(unsigned(it->second));}
  if(count!=other.size()||count!=11)return 8;
  observe(m);observe(other);
  auto first=other.begin();auto expected=first;++expected;
  auto next=other.erase(first);if(next!=expected||other.size()!=10)return 12;
  auto cf=c.cbegin();auto cn=cf;++cn;next=other.erase(cf);
  if(next!=cn||other.size()!=9)return 13;
  auto start=other.cbegin();auto stop=start;for(unsigned n=0;n<3;++n)++stop;
  auto after=other.erase(start,stop);if(after!=stop||other.size()!=6)return 14;
  if(other.erase(stop,stop)!=stop||other.size()!=6)return 15;
  other.erase(other.cbegin(),other.cend());if(!other.empty())return 16;
  other.clear();if(other.begin()!=other.end()||c.cbegin()!=c.cend())return 9;
  other[-7]=49;auto only=other.begin();if(only==other.end()||only->first!=-7||only->second!=49)return 10;
  auto old=only++;if(old==other.end()||only!=other.end()||old->second!=49)return 11;
 }
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==8955593830281507552ULL?0:90;
}

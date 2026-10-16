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
 {Map m;Map::value_type pair{1,11};std::pair<int,int> mutable_pair{2,22};
  auto a=m.insert(pair);if(!a.second||a.first->first!=1||a.first->second!=11)return 1;
  pair.second=77;auto b=m.insert(pair);if(b.second||b.first!=a.first||b.first->second!=11)return 2;
  auto c=m.insert(mutable_pair);if(!c.second||c.first->second!=22)return 3;
  auto d=m.insert(static_cast<std::pair<int,int>&&>(mutable_pair));if(d.second||d.first!=c.first)return 4;
  Map::value_type third{3,33};auto e=m.insert(static_cast<Map::value_type&&>(third));if(!e.second||e.first->second!=33)return 5;
  auto h=m.insert(m.cend(),pair);if(h!=m.find(1)||h->second!=11)return 6;
  Map::value_type fourth{4,44};auto h2=m.insert(m.cend(),static_cast<Map::value_type&&>(fourth));if(h2->second!=44)return 7;
  auto h3=m.insert(m.cend(),mutable_pair);if(h3!=m.find(2))return 8;
  int key=5,value=55;auto f=m.emplace(key,value);if(!f.second||f.first->second!=55)return 9;
  auto f2=m.emplace(key,99);if(f2.second||f2.first->second!=55)return 10;
  auto f3=m.emplace(pair);auto f4=m.emplace(mutable_pair);if(f3.second||f4.second)return 11;
  auto eh=m.emplace_hint(m.cend(),6,66);if(eh->first!=6||eh->second!=66)return 12;
  auto t=m.try_emplace(7);auto td=m.try_emplace(7,99);if(!t.second||td.second||t.first->second!=0)return 13;
  auto th=m.try_emplace(m.cend(),8,88);if(th->second!=88)return 14;
  auto as=m.insert_or_assign(key,value+1);if(as.second||as.first->second!=56)return 15;
  auto as2=m.insert_or_assign(9,99);if(!as2.second||as2.first->second!=99)return 16;
  auto ah=m.insert_or_assign(m.cend(),10,100);if(ah->second!=100)return 17;
  unsigned alloc=allocations,freed=deletions;
  for(int k=1;k<=10;++k){auto duplicate=m.try_emplace(k,1000);if(duplicate.second||duplicate.first==m.end())return 18;}
  if(allocations!=alloc||deletions!=freed||m.size()!=10)return 19;
  for(int k=-11;k<=0;++k){auto result=m.try_emplace(k,k*7);if(!result.second||result.first->second!=k*7)return 20;}
  if(m.size()!=22)return 21;observe(m);
  for(auto it=m.begin();it!=m.end();++it){mix(unsigned(it->first));mix(unsigned(it->second));}
 }
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==16052680581949533932ULL?0:90;
}

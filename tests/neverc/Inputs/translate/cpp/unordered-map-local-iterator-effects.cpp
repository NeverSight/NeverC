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
using I=Map::local_iterator;using CI=Map::const_local_iterator;
unsigned trace;I range_saved;Map*range_map;
I range_first(){++effects;return range_saved;}
I range_last(){++effects;range_saved=range_map->end(0);return range_map->end(0);}
Map*receiver(Map*p){++effects;trace=trace*10+1;return p;}
const Map*const_receiver(const Map*p){++effects;trace=trace*10+1;return p;}
Size bucket_argument(Map*p){++effects;trace=trace*10+2;p->rehash(8);return 0;}
int main(){
 {Map m(7);m[0]=11;m[8]=21;m[16]=31;m[1]=41;m[2]=51;int*stable=&m[8];
  effects=trace=0;I first=receiver(&m)->begin(bucket_argument(&m));if(effects!=2||trace!=12||m.bucket_count()!=8||&m[8]!=stable)return 1;
  effects=trace=0;I last=receiver(&m)->end(bucket_argument(&m));if(effects!=2||trace!=12)return 2;
  unsigned count=0;unsigned total=0;I copy=first;while(copy!=last){if(m.bucket(copy->first)!=0)return 3;total+=unsigned(copy->second);++copy;++count;}
  if(count!=3||total!=63)return 4;mix(count);mix(total);
  effects=trace=0;CI ci=const_receiver(&m)->cbegin(bucket_argument(&m));if(effects!=2||trace!=12)return 5;
  effects=trace=0;CI ce=const_receiver(&m)->cend(bucket_argument(&m));if(effects!=2||trace!=12)return 6;
  CI saved=ci;CI moved(static_cast<CI&&>(saved));unsigned cc=0;while(moved!=ce){++moved;++cc;}if(cc!=3)return 7;
  effects=trace=0;auto singleton=(++effects,m.begin(1));if(effects!=1||singleton==m.end(1)||singleton->first!=1)return 8;
  ++singleton;if(singleton!=m.end(1))return 9;observe(m);
  range_map=&m;range_saved=m.begin(0);effects=0;{Map copied(range_first(),range_last());if(effects!=2||copied.size()!=3)return 10;observe(copied);}
  range_saved=m.begin(0);effects=0;{Map inserted(7);inserted.insert(range_first(),range_last());if(effects!=2||inserted.size()!=3)return 11;observe(inserted);}
 }
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==337130681472028615ULL?0:90;
}

#include <unordered_map>
#include <new>
#include <iterator>
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

using I=Map::iterator;using LI=Map::local_iterator;using Diff=std::ptrdiff_t;
I& source(I&it){++effects;return it;}
I endpoint(I&it,Map&m){++effects;it=m.end();return m.end();}
Diff offset(I&it,Map&m){++effects;it=m.end();return 2;}
Diff replace(I&it,Map&m){++effects;it=m.begin();return 1;}
LI& local_source(LI&it){++effects;return it;}
Diff local_offset(LI&it,Map&m,Size bucket){++effects;it=m.begin(m.bucket(2));return 2;}
int main(){
 {Map m(7);m[1]=11;m[8]=21;m[15]=31;m[2]=41;m[9]=51;
  auto first=m.begin();unsigned old=effects;
  if(std::distance(source(first),endpoint(first,m))!=5||first!=m.end()||effects!=old+2)return 1;
  first=m.begin();auto expected=first;++expected;++expected;old=effects;
  auto copied=std::next(source(first),offset(first,m));if(copied!=expected||first!=m.end()||effects!=old+2)return 2;
  first=m.end();expected=m.begin();++expected;old=effects;
  std::advance(source(first),replace(first,m));if(first!=expected||effects!=old+2)return 3;
  Size bucket=m.bucket(1);if(m.bucket_size(bucket)!=3)return 4;
  auto local=m.begin(bucket);auto local_expected=local;++local_expected;++local_expected;old=effects;
  auto local_copy=std::next(local_source(local),local_offset(local,m,bucket));
  if(local_copy!=local_expected||local!=m.begin(m.bucket(2))||effects!=old+2)return 5;
  if(std::distance(local_copy,m.end(bucket))!=1)return 6;
  auto ci=Map::const_local_iterator(local_copy);std::advance(ci,1);if(ci!=m.cend(bucket))return 7;
 }
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return 0;
}

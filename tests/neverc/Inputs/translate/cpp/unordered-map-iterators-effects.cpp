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
using Iterator=Map::iterator;
Iterator*iterator_receiver(Iterator*p){effects=effects*10+1;return p;}
Iterator&iterator_left(Iterator&p){effects=effects*10+1;return p;}
Iterator&iterator_right(Iterator&p){effects=effects*10+2;return p;}
int postfix_argument(){effects=effects*10+2;return 47;}
int main(){
 {Map m;for(int k=0;k<5;++k)m[k]=k*3;
  effects=0;auto found=receiver(&m)->find(key_argument(2));
  if(effects!=12||found==m.end()||found->second!=6)return 1;
  auto i=m.begin();auto j=m.find(1);auto expected=j;
  effects=0;iterator_left(i)=iterator_right(j);if(effects!=21||i!=expected)return 2;
  effects=0;iterator_receiver(&i)->operator=(iterator_right(j));if(effects!=12||i!=expected)return 3;
  effects=0;Map::const_iterator ci(iterator_right(j));if(effects!=2||ci!=j)return 4;
  i=m.begin();auto previous=i;
  effects=0;auto old=iterator_receiver(&i)->operator++(postfix_argument());
  if(effects!=12||old!=previous||i==old)return 5;
  effects=0;auto&same=++*iterator_receiver(&i);if(effects!=1||&same!=&i)return 6;
  effects=0;auto&value=iterator_receiver(&j)->operator*();if(effects!=1||&value!=&*j)return 7;
  effects=0;auto*pointer=iterator_receiver(&j)->operator->();if(effects!=1||pointer!=&*j)return 8;
  unsigned a=allocations,d=deletions;ci=m.cbegin();++ci;ci++;i=j;auto miss=m.find(88);
  if(miss!=m.end()||allocations!=a||deletions!=d)return 9;
  mix(unsigned(i->first));mix(unsigned(ci->second));observe(m);
 }
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==4980349404801500056ULL?0:90;
}

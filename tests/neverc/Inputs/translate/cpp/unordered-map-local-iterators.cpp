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
I begin_value(Map&m,Size bucket){return m.begin(bucket);}
CI const_begin_value(const Map&m,Size bucket){return m.cbegin(bucket);}
struct Holder{I first;CI last;};
int main(){
 if(sizeof(I)!=3*sizeof(void*)||sizeof(CI)!=3*sizeof(void*)||alignof(I)!=alignof(void*)||alignof(CI)!=alignof(void*))return 1;
 I singular;CI const_singular;if(singular!=I{}||const_singular!=CI{})return 2;
 {Map empty(7);for(Size b=0;b<empty.bucket_count();++b){if(empty.begin(b)!=empty.end(b)||empty.cbegin(b)!=empty.cend(b))return 3;}}
 {Map m(7);m[1]=11;m[8]=21;m[15]=31;m[2]=41;m[-5]=51;const Map&view=m;unsigned seen=0;
  for(Size b=0;b<m.bucket_count();++b){
   I it=begin_value(m,b),end=m.end(b);CI converted=it;CI cend=view.cend(b);if((it==end)!=(m.bucket_size(b)==0)||converted!=const_begin_value(view,b))return 4;
   Holder holder{it,cend};I copies[2]={it,end};I assigned;assigned=copies[0];CI ca;ca=converted;
   if(holder.first!=assigned||holder.last!=cend||ca!=converted||copies[1]!=end)return 5;
   unsigned local=0;while(it!=end){int key=it->first;if(m.bucket(key)!=b||&it->second!=&m.find(key)->second)return 6;it->second+=10;
    I previous=it++;if(previous->first!=key||previous==it)return 7;++local;++seen;
   }
   if(local!=m.bucket_size(b)||it!=end)return 8;
   CI ci=view.begin(b),ce=view.end(b);unsigned count=0;while(ci!=ce){CI previous=ci++;if(m.bucket(previous->first)!=b)return 9;++count;}
   if(count!=local)return 10;mix(local);
   {Map copied(view.cbegin(b),view.cend(b),7);Map inserted(7);inserted.insert(m.begin(b),m.end(b));
    if(copied.size()!=local||inserted.size()!=local)return 12;
    for(auto item=view.cbegin(b);item!=view.cend(b);++item)
      if(copied[item->first]!=item->second||inserted[item->first]!=item->second)return 13;
    observe(copied);observe(inserted);}
  }
  if(seen!=m.size()||m[1]!=21||m[8]!=31||m[15]!=41||m[2]!=51||m[-5]!=61)return 11;observe(m);
 }
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==15544064501518499028ULL?0:90;
}

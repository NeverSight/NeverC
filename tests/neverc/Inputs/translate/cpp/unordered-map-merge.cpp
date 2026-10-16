#include <unordered_map>
#include <new>
using Map=std::unordered_map<int,int>;
using Size=std::size_t;
struct Slot{unsigned long long alignment;unsigned char bytes[32];Size size;bool used;};
struct Wide{unsigned long long alignment;unsigned char bytes[1024];Size size;bool used;};
Slot slots[48]{};Wide wide[4]{};unsigned allocations,deletions,effects;unsigned long long hash=1469598103934665603ULL;
void mix(unsigned long long value){hash=(hash^value)*1099511628211ULL;}

void allocation_observer(Size n);
void*operator new(Size n){
 allocation_observer(n);
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

Map*watched_source;unsigned observations,first_observed_size;int watched_key;
void allocation_observer(Size n){
 if(watched_source&&n>32){
  if(!observations)first_observed_size=unsigned(watched_source->size());
  ++observations;mix(watched_source->size());mix(watched_source->count(watched_key));
  auto found=watched_source->find(watched_key);
  if(found!=watched_source->end()){found->second+=1000;mix(unsigned(found->second));}
 }
}
int main(){
 {Map source(7);source[1]=11;source[8]=21;source[2]=31;source[9]=41;
  int*p1=&source[1];int*p2=&source[2];int*p9=&source[9];int*p8=&source[8];
  Map destination(2);destination.max_load_factor(0.5f);destination[8]=81;destination[15]=151;
  watched_key=source.begin()->first;watched_source=&source;observations=first_observed_size=0;
  destination.merge(source);watched_source=nullptr;
  if(!observations||first_observed_size!=4||source.size()!=1||source.count(8)!=1||destination.size()!=5)return 1;
  if(&destination[1]!=p1||&destination[2]!=p2||&destination[9]!=p9||&source[8]!=p8||destination[8]!=81)return 2;
  observe(source);observe(destination);
  Map rest(7);rest[3]=301;rest[4]=401;rest[8]=801;int*p3=&rest[3];int*p4=&rest[4];
  destination.merge(static_cast<Map&&>(rest));
  if(rest.size()!=1||rest[8]!=801||destination.size()!=7||&destination[3]!=p3||&destination[4]!=p4)return 3;
  unsigned a=allocations,d=deletions;Size n=destination.size();
  destination.merge(destination);if(destination.size()!=n||allocations!=a||deletions!=d)return 4;
  Map zero,empty;zero.merge(empty);destination.merge(empty);empty.merge(static_cast<Map&&>(zero));
  if(!zero.empty()||!empty.empty()||zero.bucket_count()||empty.bucket_count())return 5;
  observe(source);observe(destination);observe(rest);observe(zero);observe(empty);
 }
 if(allocations!=deletions)return 80;
 for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;
 for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==5572420199998372315ULL?0:90;
}

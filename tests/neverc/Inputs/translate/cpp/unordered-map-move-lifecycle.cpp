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
unsigned order;Map*left(Map*p){order=order*10+1;return p;}Map&right(Map&p){order=order*10+2;return p;}
int main(){
 {Map source;source.rehash(8);source.max_load_factor(.5f);for(int i=0;i<7;++i)source[i*8]=i+10;
  int*p=&source.find(16)->second;unsigned a=allocations,d=deletions;Size buckets=source.bucket_count();
  Map moved(static_cast<Map&&>(source));if(allocations!=a||deletions!=d||source.size()!=0||source.bucket_count()!=0||source.max_load_factor()!=.5f)return 1;
  if(moved.bucket_count()!=buckets||moved.find(16)==moved.end()||&moved.find(16)->second!=p||*p!=12)return 2;observe(moved);observe(source);
  source[91]=92;if(source[91]!=92)return 3;
  Map target;target.reserve(17);target[2]=3;target[5]=6;d=deletions;a=allocations;order=0;
  Map&alias=(*left(&target)=static_cast<Map&&>(right(moved)));
  if(order!=21||&alias!=&target||allocations!=a||deletions!=d+3||moved.size()!=0||moved.bucket_count()!=0)return 4;
  if(target.size()!=7||&target.find(16)->second!=p)return 5;observe(target);observe(moved);
  Map empty;empty.reserve(23);empty.max_load_factor(3.f);buckets=empty.bucket_count();a=allocations;d=deletions;
  Map empty_move(static_cast<Map&&>(empty));if(empty_move.bucket_count()!=buckets||empty_move.size()!=0||empty.bucket_count()!=0||empty.max_load_factor()!=3.f||allocations!=a||deletions!=d)return 6;
  target.operator=(static_cast<Map&&>(empty_move));if(!target.empty()||target.bucket_count()!=buckets||target.max_load_factor()!=3.f||empty_move.bucket_count()!=0)return 7;observe(target);
  Map self;self[1]=2;self[9]=3;self=static_cast<Map&&>(self);if(!self.empty())return 8;observe(self);self[42]=43;if(self[42]!=43)return 9;observe(self);
 }
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==18183366606552956659ULL?0:90;
}

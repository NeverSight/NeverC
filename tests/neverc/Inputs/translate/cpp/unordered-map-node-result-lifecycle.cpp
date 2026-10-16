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
void deletion_observer();
void operator delete(void*p)noexcept{
 deletion_observer();
 for(unsigned i=0;i<48;++i)if(p==slots[i].bytes){mix(slots[i].size*2);slots[i].used=false;++deletions;return;}
 for(unsigned i=0;i<4;++i)if(p==wide[i].bytes){mix(wide[i].size*2);wide[i].used=false;++deletions;return;}
}
void operator delete(void*p,Size)noexcept{operator delete(p);}
void observe(const Map&m){mix(m.size());mix(m.bucket_count());mix(unsigned(m.max_load_factor()*100));for(auto it=m.begin();it!=m.end();++it){mix(unsigned(it->first));mix(unsigned(it->second));}for(Size i=0;i<m.bucket_count();++i)mix(m.bucket_size(i));}

using N=Map::node_type;using R=Map::insert_return_type;
R*watched_result;R*watched_source;Map*destination;unsigned observations;
void deletion_observer(){
 if(watched_result){
  ++observations;mix(watched_result->inserted);mix(watched_result->position->first);
  mix(watched_result->node.empty());
  if(watched_source){
   watched_source->position=destination->find(99);watched_source->inserted=true;
   watched_source->node.mapped()+=100;
  }
 }
}
int main(){
 {Map map(7);map[1]=101;map[2]=202;map[99]=999;destination=&map;
  Map origin(7);origin[1]=11;origin[2]=22;
  R first=map.insert(origin.extract(1));R second=map.insert(origin.extract(2));
  int*address=&second.node.mapped();
  R moved(static_cast<R&&>(second));
  if(second.node||!moved.node||moved.node.key()!=2||moved.node.mapped()!=22||&moved.node.mapped()!=address||moved.position!=second.position||moved.inserted!=second.inserted)return 1;
  watched_result=&first;watched_source=&moved;observations=0;unsigned before=deletions;
  R&assigned=(first=static_cast<R&&>(moved));
  watched_result=watched_source=nullptr;
  if(&assigned!=&first||observations!=1||deletions!=before+1||first.position->first!=2||first.inserted||first.node.key()!=2||first.node.mapped()!=122||&first.node.mapped()!=address)return 2;
  if(moved.node||!moved.inserted||moved.position->first!=99)return 3;
  R empty=map.insert(N{});before=deletions;std::swap(first,empty);
  if(first.node||first.position!=map.end()||first.inserted||empty.node.mapped()!=122||&empty.node.mapped()!=address||deletions!=before)return 4;
  std::swap(empty,empty);
  if(!empty.node||&empty.node.mapped()!=address||deletions!=before)return 5;
  R&self=(empty=static_cast<R&&>(empty));
  if(&self!=&empty||empty.node||deletions!=before+1)return 6;
  observe(map);observe(origin);destination=nullptr;
 }
 if(allocations!=deletions)return 80;
 for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;
 for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==3422073626532709988ULL?0:90;
}

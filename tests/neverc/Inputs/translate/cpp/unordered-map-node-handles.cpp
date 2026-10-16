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
using N=Map::node_type;
int main(){
 static_assert(sizeof(N)==2*sizeof(void*));
 {Map m(7);m[1]=11;m[8]=21;m[2]=31;Map dest(7);dest[15]=71;
  N empty;if(empty||!empty.empty())return 1;
  int*address=&m[8];unsigned a=allocations,d=deletions;N handle=m.extract(8);
  if(!handle||handle.empty()||handle.key()!=8||handle.mapped()!=21||&handle.mapped()!=address||m.count(8)||allocations!=a||deletions!=d)return 2;
  N moved(static_cast<N&&>(handle));if(handle||!moved||moved.get_allocator()!=m.get_allocator())return 3;
  const N&view=moved;view.key()=15;view.mapped()=41;
  auto rejected=dest.insert(static_cast<N&&>(moved));
  if(moved||rejected.inserted||rejected.position->second!=71||!rejected.node||rejected.node.key()!=15||&rejected.node.mapped()!=address)return 4;
  auto&[duplicate_position,duplicate_inserted,duplicate_node]=rejected;
  if(duplicate_position->second!=71||duplicate_inserted||!duplicate_node)return 16;
  duplicate_node.key()=16;auto [position,inserted,remaining]=dest.insert(static_cast<N&&>(duplicate_node));
  if(duplicate_node||!inserted||remaining||position->first!=16||position->second!=41||&position->second!=address)return 5;
  auto it=m.begin();int key=it->first;int*second=&it->second;N extracted=m.extract(it);if(extracted.key()!=key||&extracted.mapped()!=second||m.count(key))return 6;
  N assigned;assigned=static_cast<N&&>(extracted);if(extracted||!assigned)return 7;
  N swapped;swap(assigned,swapped);if(assigned||!swapped)return 8;swapped.swap(swapped);if(!swapped||&swapped.mapped()!=second)return 9;
  unsigned before=deletions;swapped=static_cast<N&&>(swapped);if(swapped||deletions!=before+1)return 10;
  N hint=m.extract(1);if(!hint)return 11;int*hint_address=&hint.mapped();dest[1]=101;
  auto duplicate=dest.insert(dest.cend(),static_cast<N&&>(hint));if(!hint||duplicate->second!=101)return 12;
  hint.key()=3;auto accepted=dest.insert(dest.cend(),static_cast<N&&>(hint));if(hint||accepted->first!=3||accepted->second!=11||&accepted->second!=hint_address)return 13;
  auto none=m.extract(99);if(none)return 14;auto empty_result=dest.insert(static_cast<N&&>(none));if(empty_result.inserted||empty_result.position!=dest.end()||empty_result.node)return 15;
  observe(m);observe(dest);
 }
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==9217354440604074625ULL?0:90;
}

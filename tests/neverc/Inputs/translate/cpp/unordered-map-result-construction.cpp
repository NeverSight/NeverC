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

using R=Map::insert_return_type;using I=Map::iterator;using N=Map::node_type;
struct Holder{R result;Holder():result(){}};
R empty_result(){return R();}
unsigned order;
I position(Map&m){if(order!=0)order=99;else order=1;++effects;return m.begin();}
bool inserted(){if(order!=1)order=99;else order=2;++effects;return true;}
N node(Map&m){if(order!=2)order=99;else order=3;++effects;return m.extract(2);}
int main(){
 {R value=R();if(value.inserted||value.node||value.position!=I{})return 1;
  R ordinary;ordinary.inserted=false;if(ordinary.node||ordinary.position!=I{}||ordinary.inserted)return 2;
  R array[3];for(unsigned i=0;i<3;++i){array[i].inserted=false;if(array[i].node||array[i].position!=I{}||array[i].inserted)return 3;}
  R values[2]{};for(unsigned i=0;i<2;++i)if(values[i].node||values[i].inserted||values[i].position!=I{})return 4;
  Holder holder;if(holder.result.node||holder.result.inserted||holder.result.position!=I{})return 5;
  auto returned=empty_result();if(returned.node||returned.inserted||returned.position!=I{})return 6;
  Map map(7);map[1]=11;map[2]=21;map[3]=31;
  R aggregate{position(map),inserted(),node(map)};
  if(order!=3||effects!=3||!aggregate.inserted||!aggregate.node||aggregate.node.key()!=2||aggregate.node.mapped()!=21||aggregate.position==map.end())return 7;
  R partial{map.end()};if(partial.inserted||partial.node||partial.position!=map.end())return 8;
  array[0].node=map.extract(1);array[1].node=map.extract(3);
  if(map.size()||array[0].node.key()!=1||array[1].node.key()!=3)return 9;
  R moved(static_cast<R&&>(value));if(value.node||moved.node||moved.inserted||moved.position!=I{})return 10;
 }
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return 0;
}

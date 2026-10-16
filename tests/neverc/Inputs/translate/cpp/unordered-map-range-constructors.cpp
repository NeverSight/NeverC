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
using A=Map::allocator_type;using H=Map::hasher;using E=Map::key_equal;using P=std::pair<int,int>;using V=Map::value_type;using L=std::initializer_list<V>;
int check(Map&m,unsigned size,int value){if(m.size()!=size||m[1]!=value||m[2]!=41)return 1;observe(m);return 0;}
int main(){
 P input[4]={{1,11},{8,21},{1,31},{2,41}};H h{};E e{};A a{};
{Map m(input,input+4);if(check(m,3,11))return 1;}
{Map m(input,input+4,7);if(check(m,3,11))return 2;}
{Map m(input,input+4,7,h);if(check(m,3,11))return 3;}
{Map m(input,input+4,7,h,e);if(check(m,3,11))return 4;}
{Map m(input,input+4,7,h,e,a);if(check(m,3,11))return 5;}
{Map m(input,input+4,7,a);if(check(m,3,11))return 6;}
{Map m(input,input+4,7,h,a);if(check(m,3,11))return 7;}
 const P readonly[3]={{1,17},{1,31},{2,41}};
 {Map m(readonly,readonly+3,8);if(check(m,2,17)||m.bucket_count()!=8)return 10;}
 V values[3]={{1,19},{1,31},{2,41}};
 {Map m(values,values+3);if(check(m,2,19))return 11;}
 {Map source;source[1]=23;source[2]=41;source[8]=71;
  Map m(source.begin(),source.end(),7);if(check(m,3,23)||&m[1]==&source[1])return 12;
  const Map&view=source;Map n(view.cbegin(),view.cend(),8,h,e,a);if(check(n,3,23)||n.bucket_count()!=8)return 13;}
 L list={{1,29},{1,31},{2,41}};
{Map m(list);if(check(m,2,29))return 21;}
{Map m(list,7);if(check(m,2,29))return 22;}
{Map m(list,7,h);if(check(m,2,29))return 23;}
{Map m(list,7,h,e);if(check(m,2,29))return 24;}
{Map m(list,7,h,e,a);if(check(m,2,29))return 25;}
{Map m(list,7,a);if(check(m,2,29))return 26;}
{Map m(list,7,h,a);if(check(m,2,29))return 27;}
 {Map m({{1,37},{1,31},{2,41}});if(check(m,2,37))return 30;}
 {Map m(L{});if(m.size()!=0||m.bucket_count()!=0)return 31;observe(m);}
 {Map m(L{},7);if(m.size()!=0||m.bucket_count()!=7)return 32;observe(m);}
 {P*nil=nullptr;Map m(nil,nil,8);if(m.size()!=0||m.bucket_count()!=8)return 33;observe(m);}
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==18076049901143885620ULL?0:90;
}

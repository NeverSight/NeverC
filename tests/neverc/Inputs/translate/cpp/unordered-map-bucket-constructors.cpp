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
using A=Map::allocator_type;using H=Map::hasher;using E=Map::key_equal;
Map make_map(Size n){Map result(n);result[7]=19;return result;}
int main(){
 {Map m(0);if(m.size()!=0||m.bucket_count()!=0||m.max_load_factor()!=1.f)return 1;observe(m);}
 {Map m(1);if(m.bucket_count()!=2)return 2;m[7]=19;observe(m);}
 H h{};E e{};A a{};
 {Map m(7,h);if(m.bucket_count()!=7)return 3;m[1]=11;m[8]=21;observe(m);}
 {Map m(4,h,e);if(m.bucket_count()!=4)return 4;m[3]=31;m[7]=41;observe(m);}
 {Map m(9,h,e,a);if(m.bucket_count()!=11||m.get_allocator()!=a)return 5;m[2]=51;observe(m);}
 {Map m(8,a);if(m.bucket_count()!=8)return 6;m[2]=61;observe(m);}
 {Map m(3,h,a);if(m.bucket_count()!=3)return 7;m[2]=71;observe(m);}
 {Map objects[2]={Map(7),make_map(8)};if(objects[0].bucket_count()!=7||objects[1][7]!=19)return 8;observe(objects[0]);observe(objects[1]);}
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return sizeof(void*)!=8||hash==15518385948717157300ULL?0:90;
}

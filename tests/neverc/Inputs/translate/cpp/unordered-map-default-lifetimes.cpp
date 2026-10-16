#include <unordered_map>
#include <new>
using Map=std::unordered_map<int,int>;
struct Slot{unsigned long long alignment;unsigned char bytes[256];};
Slot slots[8]{};unsigned allocations,deletions,effects;std::size_t last_size;void*last_deleted;
void*operator new(std::size_t n){last_size=n;return slots[allocations++].bytes;}
void operator delete(void*p)noexcept{last_deleted=p;++deletions;}
void operator delete(void*p,std::size_t)noexcept{operator delete(p);}
Map*receiver(Map*p){++effects;return p;}
float argument(){effects=effects*10+2;return .5f;}
bool valid(const Map&v,float load=1.f){return v.empty()&&v.size()==0&&v.bucket_count()==0&&v.load_factor()==0.f&&v.max_load_factor()==load;}
struct Holder{Map first,second;};
int early(){Map local;local.max_load_factor(.75f);return valid(local,.75f)?0:1;}
int main(){
 {Map values[3];for(int i=0;i<3;++i)if(!valid(values[i]))return 1;
  values[1].max_load_factor(.25f);if(!valid(values[0])||!valid(values[1],.25f)||!valid(values[2]))return 2;
  receiver(&values[0])->max_load_factor(argument());if(effects!=12||!valid(values[0],.5f))return 3;}
 {Holder h;if(!valid(h.first)||!valid(h.second))return 4;h.second.max_load_factor(2.f);if(!valid(h.first)||!valid(h.second,2.f))return 5;}
 if(early())return 6;
 for(int i=0;i<2;++i){Map local;if(!valid(local))return 7;if(i)break;}
 Map*p=new Map;if(!valid(*p)||allocations!=1||last_size!=sizeof(Map)||deletions)return 8;
 void*expected=p;delete p;if(deletions!=1||last_deleted!=expected)return 9;
 return allocations!=deletions?10:0;
}

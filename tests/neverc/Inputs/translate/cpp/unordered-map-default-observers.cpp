#include <unordered_map>
#include <new>
using Map=std::unordered_map<int,int>;
unsigned allocations,deletions,effects;
struct Storage{unsigned long long alignment;unsigned char bytes[2048];};
Storage arena{};
void*operator new(std::size_t){++allocations;return arena.bytes;}
void operator delete(void*)noexcept{++deletions;}
void operator delete(void*,std::size_t)noexcept{++deletions;}
float requested(){++effects;return .25f;}
bool empty(const Map& value){return value.empty()&&value.size()==0&&value.bucket_count()==0&&value.load_factor()==0.f;}
int main(){
  if(sizeof(Map)!=sizeof(void*)*5||alignof(Map)!=alignof(void*))return 1;
  {Map a,b;if(!empty(a)||!empty(b)||a.max_load_factor()!=1.f||b.max_load_factor()!=1.f)return 2;
   a.max_load_factor(requested());if(effects!=1||a.max_load_factor()!=.25f||b.max_load_factor()!=1.f)return 3;
   Map*pointer=&a;pointer->max_load_factor(2.5f);if(pointer->max_load_factor()!=2.5f||!empty(*pointer))return 4;
   {Map inner;inner.max_load_factor(3.f);if(inner.max_load_factor()!=3.f||!empty(inner))return 5;}
   if(allocations||deletions)return 6;}
  return allocations||deletions?7:0;
}

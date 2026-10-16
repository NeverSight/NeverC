#include <unordered_map>
#include <new>
using Map=std::unordered_map<int,int>;
using Size=std::size_t;
struct Slot{unsigned long long alignment;unsigned char bytes[32];Size size;bool used;};
struct Wide{unsigned long long alignment;unsigned char bytes[1024];Size size;bool used;};
Slot slots[48]{};Wide wide[4]{};unsigned allocations,deletions,effects;unsigned long long hash=1469598103934665603ULL;
void mix(unsigned long long value){hash=(hash^value)*1099511628211ULL;}
Map*active;bool change_limit;
void*operator new(Size n){
 if(change_limit){change_limit=false;active->max_load_factor(.25f);}
 if(n<=32)for(unsigned i=0;i<48;++i)if(!slots[i].used){slots[i].used=true;slots[i].size=n;++allocations;mix(n*2+1);return slots[i].bytes;}
 if(n<=1024)for(unsigned i=0;i<4;++i)if(!wide[i].used){wide[i].used=true;wide[i].size=n;++allocations;mix(n*2+1);return wide[i].bytes;}
 for(;;){}
}
void operator delete(void*p)noexcept{
 for(unsigned i=0;i<48;++i)if(p==slots[i].bytes){mix(slots[i].size*2);slots[i].used=false;++deletions;return;}
 for(unsigned i=0;i<4;++i)if(p==wide[i].bytes){mix(wide[i].size*2);wide[i].used=false;++deletions;return;}
}
void operator delete(void*p,Size)noexcept{operator delete(p);}
Map*receiver(Map*p){++effects;return p;}
int key_argument(int k){effects=effects*10+2;return k;}
void observe(const Map&m){mix(m.size());mix(m.bucket_count());for(Size i=0;i<m.bucket_count();++i)mix(m.bucket_size(i));}
using Iterator=Map::iterator;using ConstIterator=Map::const_iterator;
struct Holder{Iterator it;ConstIterator ci;};
Iterator global_iterators[2]{};
Iterator identity(Iterator value){return value;}
ConstIterator converted(Iterator value){return value;}
int main(){
 static_assert(sizeof(Iterator)==sizeof(void*));
 static_assert(alignof(ConstIterator)==alignof(void*));
 {Map m;m[1]=7;m[2]=9;
  Iterator values[2]{};ConstIterator constants[2]{};Holder holder{};
  if(values[0]!=values[1]||constants[0]!=constants[1]||holder.it!=holder.ci)return 1;
  values[0]=m.begin();values[1]=m.end();constants[0]=values[0];holder.it=identity(values[0]);holder.ci=converted(holder.it);
  if(holder.it!=values[0]||holder.ci!=constants[0]||holder.ci->second!=holder.it->second)return 2;
  Iterator*heap=new Iterator(values[0]);ConstIterator*immutable=new ConstIterator(*heap);
  Holder*owned=new Holder{*heap,*immutable};
  if(*heap!=values[0]||*immutable!=constants[0]||owned->it!=owned->ci)return 3;
  auto [key,value]=**heap;if(value!=key*2+5)return 4;
  int sum=0;for(auto&[k,v]:m){sum+=k;v+=1;}if(sum!=3||m[1]!=8||m[2]!=10)return 5;
  mix(unsigned(owned->it->first));mix(unsigned(owned->ci->second));
  delete owned;delete immutable;heap->~Iterator();::operator delete(static_cast<void*>(heap));
  global_iterators[0]=m.end();global_iterators[1]=m.end();
 }
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 // Replaceable new expressions may be elided by native C++ optimization.
 // Value and balanced lifetime checks apply independently of that choice.
 return 0;
}

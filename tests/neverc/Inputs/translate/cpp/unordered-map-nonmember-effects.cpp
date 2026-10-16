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

unsigned left_calls,right_calls;
const Map& compare_left(Map&m){++left_calls;++effects;m[5]=55;return m;}
const Map& compare_right(Map&m){++right_calls;++effects;m[5]=55;return m;}
Map& swap_left(Map&m){++left_calls;++effects;return m;}
Map& swap_right(Map&m){++right_calls;++effects;return m;}
int main(){
 {Map a(7),b(7);a[1]=11;b[1]=11;
  unsigned old=effects;if(!(compare_left(a)==compare_right(b)))return 1;
  if(left_calls!=1||right_calls!=1||effects!=old+2)return 2;
  old=effects;if(std::operator!=(compare_left(a),compare_right(b)))return 3;
  if(left_calls!=2||right_calls!=2||effects!=old+2)return 4;
  b[8]=81;int*address=&b[8];Size olda=a.bucket_count(),oldb=b.bucket_count();
  unsigned alloc=allocations,del=deletions;old=effects;
  std::swap(swap_left(a),swap_right(b));
  if(left_calls!=3||right_calls!=3||effects!=old+2||allocations!=alloc||deletions!=del)return 5;
  if(&a[8]!=address||b.count(8)||a.bucket_count()!=oldb||b.bucket_count()!=olda)return 6;
  old=effects;swap(swap_left(a),swap_right(a));
  if(left_calls!=4||right_calls!=4||effects!=old+2||&a[8]!=address)return 7;
 }
 if(allocations!=deletions)return 80;
 for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;
 for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return 0;
}

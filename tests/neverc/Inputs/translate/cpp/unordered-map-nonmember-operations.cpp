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

int main(){
 {Map empty, allocated(13);allocated.max_load_factor(2);
  if(!(empty==allocated)||empty!=allocated||!std::operator==(empty,allocated)||std::operator!=(empty,allocated))return 1;
  Map a(7),b(17);a.max_load_factor(2);b.max_load_factor(3);
  const int keys[]={1,8,15,-1,-8,2,9};
  for(unsigned i=0;i<7;++i)a[keys[i]]=int(i*13+5);
  for(unsigned i=7;i!=0;--i)b[keys[i-1]]=int((i-1)*13+5);
  const Map&ca=a;const Map&cb=b;
  unsigned alloc=allocations,del=deletions;
  if(!(ca==cb)||ca!=b||!(a==a)||a!=a||!(std::operator==(ca,b))||std::operator!=(a,cb))return 2;
  if(allocations!=alloc||deletions!=del)return 3;
  b[8]=99;if(a==b||!(a!=b))return 4;b[8]=18;
  b.erase(8);b[100]=18;if(a==b||!(a!=b)||a.size()!=b.size())return 5;
  b.erase(100);if(a==b||!(a!=b))return 6;b[8]=18;
  b.rehash(31);if(!(a==b)||a!=b)return 7;
  int*pa=&a[1];int*pb=&b[-8];auto ai=a.begin();int saved_key=ai->first;int*saved_value=&ai->second;
  Size ac=a.bucket_count(),bc=b.bucket_count();float al=a.max_load_factor(),bl=b.max_load_factor();
  std::swap(a,b);
  if(a.bucket_count()!=bc||b.bucket_count()!=ac||a.max_load_factor()!=bl||b.max_load_factor()!=al||&b[1]!=pa||&a[-8]!=pb)return 8;
  if(&b.find(saved_key)->second!=saved_value||ai->first!=saved_key||&ai->second!=saved_value)return 9;
  swap(a,a);if(&a[-8]!=pb||a.bucket_count()!=bc||a.max_load_factor()!=bl)return 10;
  swap(a,empty);if(!a.empty()||a.bucket_count()!=0||empty.size()!=7||&empty[-8]!=pb)return 11;
  a[77]=701;empty.erase(-8);empty[78]=702;if(empty.count(-8)||a[77]!=701||empty[78]!=702)return 12;
  swap(a,empty);if(a[78]!=702||empty[77]!=701)return 13;
  allocated.clear();if(allocated!=Map{})return 14;
 }
 if(allocations!=deletions)return 80;
 for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;
 for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return 0;
}

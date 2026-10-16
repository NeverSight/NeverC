#include <unordered_map>
#include <new>
#include <iterator>
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

using N=Map::node_type;using I=Map::iterator;using L=Map::local_iterator;
Map& touch_map(Map&m){++effects;return m;}
N& touch_node(N&n){++effects;return n;}
I& touch_iterator(I&i){++effects;return i;}
L& touch_local(L&i){++effects;return i;}
int main(){
 {Map a(7),b(13);a.max_load_factor(2);b.max_load_factor(3);
  a[1]=11;a[8]=81;a[15]=151;b[2]=21;b[15]=152;b[28]=281;
  int*one=&a[1];int*two=&b[2];Size ac=a.bucket_count(),bc=b.bucket_count();
  unsigned alloc=allocations,del=deletions,old=effects;
  std::swap<Map>(touch_map(a),touch_map(b));
  if(effects!=old+2||allocations!=alloc||deletions!=del||a.bucket_count()!=bc||b.bucket_count()!=ac||a.max_load_factor()!=3||b.max_load_factor()!=2||&a[2]!=two||&b[1]!=one)return 1;
  std::swap<Map>(a,a);if(&a[2]!=two||a.bucket_count()!=bc)return 2;
  N na=a.extract(2),nb=b.extract(1);alloc=allocations;del=deletions;old=effects;
  std::swap(touch_node(na),touch_node(nb));
  if(effects!=old+2||na.key()!=1||&na.mapped()!=one||nb.key()!=2||&nb.mapped()!=two||allocations!=alloc||deletions!=del)return 3;
  N empty;std::swap(na,empty);if(na||!empty||empty.key()!=1)return 4;
  std::swap(empty,empty);if(empty.key()!=1||&empty.mapped()!=one)return 5;
  std::swap(empty,nb);if(empty.key()!=2||nb.key()!=1)return 6;
  auto ar=a.insert(static_cast<N&&>(nb));auto br=b.insert(static_cast<N&&>(empty));
  if(!ar.inserted||!br.inserted||nb||empty||&ar.position->second!=one||&br.position->second!=two)return 7;
  auto ia=a.begin(),ib=b.begin();auto ea=ia,eb=ib;old=effects;
  std::swap(touch_iterator(ia),touch_iterator(ib));if(ia!=eb||ib!=ea||effects!=old+2)return 8;
  std::swap(ia,ia);if(ia!=eb)return 9;
  I singular;std::swap(singular,ia);if(singular!=eb||ia!=I{})return 10;
  Map::const_iterator ca=a.cbegin(),cb=b.cbegin();auto eca=ca,ecb=cb;std::swap(ca,cb);if(ca!=ecb||cb!=eca)return 11;
  Size ba=a.bucket(15),bb=b.bucket(15);auto la=a.begin(ba),lb=b.begin(bb);auto ela=la,elb=lb;
  Size sa=a.bucket_size(ba),sb=b.bucket_size(bb);old=effects;alloc=allocations;del=deletions;
  std::swap(touch_local(la),touch_local(lb));
  if(effects!=old+2||la!=elb||lb!=ela||allocations!=alloc||deletions!=del)return 12;
  if(std::distance(la,b.end(bb))!=std::ptrdiff_t(sb)||std::distance(lb,a.end(ba))!=std::ptrdiff_t(sa))return 13;
  if(std::next(la,std::ptrdiff_t(sb))!=b.end(bb)||std::next(lb,std::ptrdiff_t(sa))!=a.end(ba))return 14;
  std::swap(la,la);if(la!=elb||std::distance(la,b.end(bb))!=std::ptrdiff_t(sb))return 15;
  Map::const_local_iterator cla=a.cbegin(ba),clb=b.cbegin(bb);std::swap(cla,clb);
  if(std::distance(cla,b.cend(bb))!=std::ptrdiff_t(sb)||std::distance(clb,a.cend(ba))!=std::ptrdiff_t(sa))return 16;
 }
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return 0;
}

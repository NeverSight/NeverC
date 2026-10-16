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

using Diff=std::ptrdiff_t;
int main(){
 {Map m(7);const int keys[]={1,8,15,2,9,3,-1};for(unsigned i=0;i<7;++i)m[keys[i]]=int(i*19+1);
  const Map&c=m;unsigned a=allocations,d=deletions;
  auto first=m.begin();auto last=m.end();if(std::distance(first,last)!=7||first!=m.begin()||last!=m.end())return 1;
  auto manual=first;
  for(Diff n=0;n<=7;++n){
   auto it=std::next(first,n);if(it!=manual||std::distance(first,it)!=n||std::next(it,0)!=it)return 2;
   auto ci=c.begin();std::advance(ci,n);if(ci!=it||std::distance(c.begin(),ci)!=n)return 3;
   if(n!=7)++manual;
  }
  auto one=std::next(first);auto expected=first;++expected;if(one!=expected)return 4;
  auto i=first;std::advance(i,(unsigned short)2);if(i!=std::next(first,2))return 5;
  std::advance(i,true);if(i!=std::next(first,3))return 6;std::advance(i,0LL);if(i!=std::next(first,3))return 7;
  for(Size bucket=0;bucket<m.bucket_count();++bucket){
   auto begin=m.begin(bucket),end=m.end(bucket);auto size=m.bucket_size(bucket);
   if(std::distance(begin,end)!=Diff(size)||begin!=m.begin(bucket))return 8;
   auto cursor=begin;
   for(Size n=0;n<=size;++n){
    auto it=std::next(begin,Diff(n));if(it!=cursor||std::distance(begin,it)!=Diff(n))return 9;
    auto ci=c.begin(bucket);std::advance(ci,n);if(ci!=it)return 10;
    if(n!=size)++cursor;
   }
   auto ci=c.begin(bucket);std::advance(ci,Diff(size));if(ci!=c.end(bucket)||std::next(c.begin(bucket),Diff(size))!=c.end(bucket))return 11;
  }
  if(allocations!=a||deletions!=d)return 12;
  Map empty;if(std::distance(empty.begin(),empty.end())!=0||std::next(empty.begin(),0)!=empty.end())return 13;
 }
 if(allocations!=deletions)return 80;for(unsigned i=0;i<48;++i)if(slots[i].used)return 81;for(unsigned i=0;i<4;++i)if(wide[i].used)return 82;
 return 0;
}

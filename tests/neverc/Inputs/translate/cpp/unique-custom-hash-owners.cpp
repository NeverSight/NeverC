#include <memory>
#include <functional>
#include <utility>
#include <cstddef>
int deleted=0,receivers=0,arguments=0,total=0;
struct D{void operator()(int*p)const noexcept{++deleted;total+=*p;}};
struct CD{void operator()(const int*p)const noexcept{++deleted;total+=*p;}};
using P=std::unique_ptr<int,D>;using A=std::unique_ptr<int[],D>;
using H=std::hash<P>;H*hash_slot=nullptr;P*owner_slot=nullptr;
H&receiver(){++receivers;return *hash_slot;}P&argument(){++arguments;return *owner_slot;}
int main(){
 int values[3]={3,5,7};
 {
  P p(&values[0]);H h;hash_slot=&h;owner_slot=&p;
  auto n=receiver()(argument());
  if(receivers!=1||arguments!=1||deleted||p.get()!=&values[0]||n!=std::hash<int*>{}(p.get()))return 1;
  if(h(std::move(p))!=n||p.get()!=&values[0]||deleted)return 2;
  const P&c=p;if(h(c)!=n)return 3;
  P moved(std::move(p));if(p.get()!=nullptr||h(p)!=std::hash<int*>{}(nullptr)||h(moved)!=n||deleted)return 4;
 }
 if(deleted!=1||total!=3)return 5;
 {
  A p(values);std::hash<A> h;
  auto n=h(p);if(deleted!=1||n!=std::hash<int*>{}(values)||p[1]!=5)return 6;
  A moved(std::move(p));if(h(p)!=std::hash<int*>{}(nullptr)||h(moved)!=n||deleted!=1)return 7;
  h(moved);if(deleted!=1)return 8;
 }
 if(deleted!=2||total!=6)return 9;
 {
  std::unique_ptr<const int,CD> p(&values[1]);
  auto n=std::hash<std::unique_ptr<const int,CD>>{}(p);
  if(n!=std::hash<const int*>{}(p.get())||deleted!=2||*p!=5)return 10;
 }
 return deleted==3&&total==11?0:11;
}

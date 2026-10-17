#include <memory>
#include <functional>
#include <utility>
using P=std::unique_ptr<int>;
using H=std::hash<P>;
P*owner_slot=nullptr;
H*hasher_slot=nullptr;
int receivers,arguments;
H& receiver(){++receivers;return *hasher_slot;}
const P& argument(){++arguments;return *owner_slot;}
int main(){
  P owner;H h;owner_slot=&owner;hasher_slot=&h;
  if(h(owner)!=std::hash<int*>{}(nullptr))return 1;
  owner=std::make_unique<int>(7);
  int*raw=owner.get();
  if(receiver()(argument())!=std::hash<int*>{}(raw))return 2;
  if(receivers!=1||arguments!=1||owner.get()!=raw||*owner!=7)return 3;
  P moved=std::move(owner);
  if(owner||h(moved)!=std::hash<int*>{}(raw))return 4;
  const auto& frozen=moved;
  const H immutable;
  if(immutable(frozen)!=std::hash<int*>{}(raw))return 5;
  std::unique_ptr<const int> qualified(new int(11));
  if(std::hash<std::unique_ptr<const int>>{}(qualified)!=std::hash<const int*>{}(qualified.get()))return 6;
  return *moved==7&&*qualified==11?0:7;
}

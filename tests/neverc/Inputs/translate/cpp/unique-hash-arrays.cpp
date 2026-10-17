#include <memory>
#include <functional>
#include <utility>
int destroyed;
struct Item{int value;Item()noexcept:value(3){}~Item()noexcept{++destroyed;}};
int main(){
  using P=std::unique_ptr<Item[]>;using H=std::hash<P>;
  {
    P p=std::make_unique<Item[]>(2);Item*raw=p.get();
    if(H{}(p)!=std::hash<Item*>{}(raw)||destroyed||p[0].value!=3)return 1;
    auto moved=std::move(p);
    if(p||H{}(moved)!=std::hash<Item*>{}(raw)||destroyed)return 2;
    H{}(moved);
    if(destroyed||moved.get()!=raw)return 3;
  }
  if(destroyed!=2)return 4;
  std::unique_ptr<int[]> empty;
  if(std::hash<std::unique_ptr<int[]>>{}(empty)!=std::hash<int*>{}(nullptr))return 5;
  return 0;
}

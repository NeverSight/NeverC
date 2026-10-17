#include <memory>
#include <functional>
int live,destroyed,converted,source_destroyed,trace;
struct Item{Item()noexcept{++live;}~Item()noexcept{--live;++destroyed;trace=trace*10+1;}};
using P=std::unique_ptr<Item>;
using H=std::hash<P>;
struct Source{
  operator P()const{++converted;return std::make_unique<Item>();}
  ~Source()noexcept{++source_destroyed;trace=trace*10+2;}
};
bool observe(std::size_t){return live==1&&destroyed==0;}
bool observe_conversion(std::size_t){return live==1&&destroyed==1&&converted==1&&source_destroyed==0;}
int main(){
  if(!observe(H{}(std::make_unique<Item>())))return 1;
  if(live||destroyed!=1||trace!=1)return 2;
  if(!observe_conversion(H{}(Source{})))return 3;
  if(live||destroyed!=2||converted!=1||source_destroyed!=1||trace!=112)return 4;
  return 0;
}

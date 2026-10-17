#include <memory>
#include <functional>
int live=0,trace=0;
struct Item{Item()noexcept{++live;}~Item(){--live;trace=trace*10+1;}};
struct D{void operator()(Item*p)const noexcept{delete p;trace=trace*10+2;}};
struct DA{void operator()(Item*p)const noexcept{delete[]p;trace=trace*10+4;}};
using P=std::unique_ptr<Item,D>;using H=std::hash<P>;
using A=std::unique_ptr<Item[],DA>;
struct Source{operator P()const{return P(new Item);}~Source(){trace=trace*10+3;}};
int main(){
 bool observed=false;
 (H{}(P(new Item)),observed=live==1&&trace==0);
 if(!observed||live!=0||trace!=12)return 1;
 (H{}(Source{}),observed=live==1&&trace==12);
 if(!(observed&&live==0&&trace==12123))return 2;
 trace=0;
 (std::hash<A>{}(A(new Item[2])),observed=live==2&&trace==0);
 return observed&&live==0&&trace==114?0:3;
}

#include <unordered_map>
using M=std::unordered_map<int,int>;
bool equal(const M&a,const M&b){return a==b;}
bool different(const M*a,const M*b){return std::operator!=(*a,*b);}
void exchange(M&a,M&b){std::swap(a,b);}
void exchange_adl(M*a,M*b){swap(*a,*b);}

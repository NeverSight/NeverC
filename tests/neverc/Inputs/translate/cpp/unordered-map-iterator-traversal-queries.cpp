#include <unordered_map>
#include <iterator>
using M=std::unordered_map<int,int>;
std::ptrdiff_t count(M::const_iterator a,M::const_iterator b){return std::distance(a,b);}
void skip(M::iterator&i,unsigned n){std::advance(i,n);}
M::iterator one(M::iterator i){return std::next(i);}
std::ptrdiff_t local_count(M::const_local_iterator a,M::const_local_iterator b){return std::distance(a,b);}
M::local_iterator local_skip(M::local_iterator i,std::ptrdiff_t n){return std::next(i,n);}

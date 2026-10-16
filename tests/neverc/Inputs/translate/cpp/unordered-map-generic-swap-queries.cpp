#include <unordered_map>
using M=std::unordered_map<int,int>;
void maps(M&a,M&b){std::swap<M>(a,b);}
void nodes(M::node_type&a,M::node_type&b){std::swap(a,b);}
void iterators(M::iterator&a,M::iterator&b){std::swap(a,b);}
void locals(M::const_local_iterator&a,M::const_local_iterator&b){std::swap(a,b);}

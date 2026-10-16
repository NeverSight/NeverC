#include <unordered_map>
#include <type_traits>
using M=std::unordered_map<int,int>;
using N=M::node_type;
using R=M::insert_return_type;
using I=M::iterator;
using L=M::local_iterator;
using NodeReference=N&;
using NodePointer=N*;
using NodeArray=N[2];
using ResultArray=R[3];
static_assert(std::is_same_v<N,M::node_type>);
static_assert(std::is_same_v<R,M::insert_return_type>);
bool empty(const M&m){return m.empty();}

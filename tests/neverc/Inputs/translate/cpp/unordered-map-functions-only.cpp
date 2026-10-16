#include <unordered_map>
using M=std::unordered_map<int,int>;
bool same(M*a,M*b){return a==b;}
bool empty(M const&r){return r.empty();}
M::size_type count(M const&r){return r.size()+r.bucket_count();}
float load(M const&r){return r.load_factor()+r.max_load_factor();}
void limit(M&r,float v){r.max_load_factor(v);}
int main(){return same(nullptr,nullptr)?0:1;}

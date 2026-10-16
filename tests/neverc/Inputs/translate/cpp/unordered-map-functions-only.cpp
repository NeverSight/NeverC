#include <unordered_map>
using M=std::unordered_map<int,int>;
bool same(M*a,M*b){return a==b;}
bool empty(M const&r){return r.empty();}
M::size_type count(M const&r){return r.size()+r.bucket_count();}
float load(M const&r){return r.load_factor()+r.max_load_factor();}
void limit(M&r,float v){r.max_load_factor(v);}
int main(){return same(nullptr,nullptr)?0:1;}

M::size_type entries(M const&m,int k){return m.count(k);}
M::size_type limits(M const&m){return m.max_size()+m.max_bucket_count();}
M::size_type buckets(M const&m,int k,M::size_type i){return m.bucket(k)+m.bucket_size(i);}
void exchange(M&a,M&b){a.swap(b);}
